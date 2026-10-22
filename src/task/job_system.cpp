// 文件：src/task/job_system.cpp
// 作用：实现带优先级与依赖关系的工作线程池和作业同步。
// 约定：作业函数可在任意工作线程执行，不得依赖主线程局部状态。
// 类型：实现文件。

#include "tinyengine/task/job_system.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace tinyengine
{
Job::Job(std::function<void()> task, const JobPriority priority) : m_task(std::move(task)), m_priority(priority) {}

// 任务异常保存在 Job 内，由 Wait 线程观察；无论成功失败都必须完成延续任务和唤醒。
void Job::execute()
{
    std::vector<std::function<void()>> continuations;
    try
    {
        if (m_task)
            m_task();
    }
    catch (...)
    {
        m_exception = std::current_exception();
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_finished = true;
        continuations = std::move(m_continuations);
    }
    m_cv.notify_all();
    for (auto& continuation : continuations)
        continuation();
    if (m_onCompleted)
        m_onCompleted();
}

void Job::wait() const
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]() { return m_finished; });
    if (m_exception)
        std::rethrow_exception(m_exception);
}

bool Job::isFinished() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_finished;
}

JobPriority Job::getPriority() const
{
    return m_priority;
}

void Job::addContinuation(std::function<void()> continuation)
{
    bool runNow = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_finished)
            runNow = true;
        else
            m_continuations.push_back(continuation);
    }
    if (runNow)
        continuation();
}

void JobQueue::push(const JobHandle& job)
{
    if (!job)
        return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queues[static_cast<std::size_t>(job->getPriority())].push_back(job);
    }
    m_cv.notify_one();
}

JobHandle JobQueue::popBlocking(std::atomic<bool>& runningFlag)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this, &runningFlag]() { return hasJobsUnsafe() || !runningFlag.load(); });
    for (std::size_t priority = m_queues.size(); priority-- > 0;)
    {
        if (!m_queues[priority].empty())
        {
            auto result = m_queues[priority].front();
            m_queues[priority].pop_front();
            return result;
        }
    }
    return nullptr;
}

bool JobQueue::empty() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !hasJobsUnsafe();
}

bool JobQueue::hasJobsUnsafe() const
{
    for (const auto& queue : m_queues)
        if (!queue.empty())
            return true;
    return false;
}

void JobQueue::notifyAll()
{
    // Pair stop notifications with PopBlocking's predicate lock. Otherwise a
    // worker can miss the notification between checking its flag and waiting.
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cv.notify_all();
}

WorkerThread::WorkerThread(JobQueue* queue) : m_queue(queue) {}
WorkerThread::~WorkerThread()
{
    stop();
    join();
}

void WorkerThread::start()
{
    if (m_running.exchange(true))
        return;
    m_thread = std::thread(&WorkerThread::RunLoop, this);
}

void WorkerThread::stop()
{
    m_running.store(false);
    if (m_queue)
        m_queue->notifyAll();
}

void WorkerThread::join()
{
    if (m_thread.joinable())
        m_thread.join();
}

void WorkerThread::RunLoop()
{
    while (m_running.load())
    {
        auto job = m_queue->popBlocking(m_running);
        if (job)
            job->execute();
    }
}

void JobSystem::initialize(std::uint32_t workerCount)
{
    if (m_accepting.exchange(true))
        return;
    workerCount = std::max(1u, workerCount);
    m_workers.reserve(workerCount);
    for (std::uint32_t index = 0; index < workerCount; ++index)
    {
        auto worker = std::make_unique<WorkerThread>(&m_globalQueue);
        worker->start();
        m_workers.push_back(std::move(worker));
    }
}

// 禁止新提交后等待 outstanding 归零，再广播停止信号，确保队列中的任务不会被静默丢弃。
void JobSystem::shutdown()
{
    if (!m_accepting.exchange(false))
        return;
    waitIdle();
    for (const auto& worker : m_workers)
        worker->stop();
    m_globalQueue.notifyAll();
    for (const auto& worker : m_workers)
        worker->join();
    m_workers.clear();
}

JobHandle JobSystem::createJob(std::function<void()> func, const JobPriority priority)
{
    return std::make_shared<Job>(std::move(func), priority);
}

// 延续回调只递减依赖计数；真正入队由 QueueWhenReady 统一处理，避免重复提交。
void JobSystem::addDependency(const JobHandle& job, const JobHandle& dependency)
{
    if (!job || !dependency)
        throw std::invalid_argument("Job dependency cannot be null");
    if (job->m_submitted.load())
        throw std::logic_error("Dependencies must be added before Submit");
    job->m_dependencyCount.fetch_add(1);
    const std::weak_ptr<Job> weakJob = job;
    dependency->addContinuation(
        [this, weakJob]()
        {
            if (const auto dependent = weakJob.lock())
            {
                if (dependent->m_dependencyCount.fetch_sub(1) == 1 && dependent->m_submitted.load())
                    queueWhenReady(dependent);
            }
        });
}

void JobSystem::submit(const JobHandle& job)
{
    if (!job)
        return;
    if (!m_accepting.load())
        throw std::logic_error("JobSystem is not accepting jobs");
    {
        std::lock_guard<std::mutex> lock(job->m_submissionMutex);
        if (job->m_submitted.load())
            return;
        job->m_onCompleted = [this]() { onJobCompleted(); };
        m_outstanding.fetch_add(1);
        job->m_submitted.store(true);
    }
    if (job->m_dependencyCount.load() == 0)
        queueWhenReady(job);
}

void JobSystem::queueWhenReady(const JobHandle& job)
{
    if (m_workers.empty())
        job->execute();
    else
        m_globalQueue.push(job);
}

void JobSystem::wait(const JobHandle& job) const
{
    if (job)
        job->wait();
}

void JobSystem::waitIdle() const
{
    std::unique_lock<std::mutex> lock(m_idleMutex);
    m_idleCv.wait(lock, [this]() { return m_outstanding.load() == 0; });
}

void JobSystem::dispatch(const std::size_t count, const std::function<void(std::size_t)>& task)
{
    if (!task || count == 0)
        return;
    std::vector<JobHandle> jobs;
    jobs.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        auto job = createJob([task, index]() { task(index); });
        jobs.push_back(job);
        submit(job);
    }
    for (const auto& job : jobs)
        wait(job);
}

std::size_t JobSystem::getOutstandingJobCount() const
{
    return m_outstanding.load();
}

void JobSystem::onJobCompleted()
{
    // The zero transition and WaitIdle's predicate must use the same mutex;
    // an atomic counter alone does not prevent a lost condition-variable wake.
    std::lock_guard<std::mutex> lock(m_idleMutex);
    if (m_outstanding.fetch_sub(1) == 1)
        m_idleCv.notify_all();
}
} // namespace tinyengine
