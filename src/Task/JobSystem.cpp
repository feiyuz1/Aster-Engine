#include "MiniEngine/Task/JobSystem.hpp"

#include <algorithm>
#include <utility>

namespace MiniEngine
{
    Job::Job(std::function<void()> inTask, const JobPriority inPriority)
        : task(std::move(inTask)),
          priority(inPriority)
    {
    }

    void Job::Execute()
    {
        try
        {
            if (task)
            {
                task();
            }
        }
        catch (...)
        {
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_finished = true;
        }
        m_cv.notify_all();
    }

    void Job::Wait() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this]() { return m_finished; });
    }

    bool Job::IsFinished() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_finished;
    }

    void JobQueue::Push(const JobHandle& job)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_jobs.push_back(job);
        }
        m_cv.notify_one();
    }

    JobHandle JobQueue::PopBlocking(std::atomic<bool>& runningFlag)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this, &runningFlag]()
        {
            return !m_jobs.empty() || !runningFlag.load();
        });

        if (m_jobs.empty())
        {
            return nullptr;
        }

        JobHandle job = m_jobs.front();
        m_jobs.pop_front();
        return job;
    }

    bool JobQueue::Empty() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_jobs.empty();
    }

    void JobQueue::NotifyAll()
    {
        m_cv.notify_all();
    }

    WorkerThread::WorkerThread(JobQueue* queue)
        : m_queue(queue)
    {
    }

    WorkerThread::~WorkerThread()
    {
        Stop();
        Join();
    }

    void WorkerThread::Start()
    {
        if (m_running.load())
        {
            return;
        }

        m_running.store(true);
        m_thread = std::thread(&WorkerThread::RunLoop, this);
    }

    void WorkerThread::Stop()
    {
        m_running.store(false);
        if (m_queue != nullptr)
        {
            m_queue->NotifyAll();
        }
    }

    void WorkerThread::Join()
    {
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    void WorkerThread::RunLoop()
    {
        while (m_running.load())
        {
            JobHandle job = m_queue->PopBlocking(m_running);
            if (job == nullptr)
            {
                continue;
            }

            if (job->dependencyCount.load() <= 0)
            {
                job->Execute();
            }
        }
    }

    void JobSystem::Initialize(std::uint32_t workerCount)
    {
        if (workerCount == 0)
        {
            workerCount = 1;
        }

        m_workers.reserve(workerCount);
        for (std::uint32_t i = 0; i < workerCount; ++i)
        {
            auto worker = std::make_unique<WorkerThread>(&m_globalQueue);
            worker->Start();
            m_workers.push_back(std::move(worker));
        }
    }

    void JobSystem::Shutdown()
    {
        for (const auto& worker : m_workers)
        {
            worker->Stop();
        }

        m_globalQueue.NotifyAll();

        for (const auto& worker : m_workers)
        {
            worker->Join();
        }

        m_workers.clear();
    }

    JobHandle JobSystem::CreateJob(std::function<void()> func, const JobPriority priority)
    {
        return std::make_shared<Job>(std::move(func), priority);
    }

    void JobSystem::Submit(const JobHandle& job)
    {
        if (job == nullptr)
        {
            return;
        }

        if (m_workers.empty())
        {
            job->Execute();
            return;
        }

        m_globalQueue.Push(job);
    }

    void JobSystem::Wait(const JobHandle& job)
    {
        if (job == nullptr)
        {
            return;
        }

        job->Wait();
    }

    void JobSystem::Dispatch(const std::size_t count, const std::function<void(std::size_t)>& task)
    {
        if (count == 0 || !task)
        {
            return;
        }

        std::vector<JobHandle> jobs;
        jobs.reserve(count);

        for (std::size_t i = 0; i < count; ++i)
        {
            JobHandle job = CreateJob([task, i]()
            {
                task(i);
            });
            jobs.push_back(job);
            Submit(job);
        }

        for (const auto& job : jobs)
        {
            Wait(job);
        }
    }
}
