#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace MiniEngine
{
enum class JobPriority : std::uint8_t
{
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3
};

class Job
{
  public:
    explicit Job(std::function<void()> task, JobPriority priority = JobPriority::Normal);

    void Wait() const;
    bool IsFinished() const;
    JobPriority GetPriority() const;

  private:
    friend class JobSystem;
    friend class WorkerThread;
    void Execute();
    void AddContinuation(std::function<void()> continuation);

    std::function<void()> m_task;
    JobPriority m_priority = JobPriority::Normal;
    std::atomic<int> m_dependencyCount{0};
    std::atomic<bool> m_submitted{false};
    std::mutex m_submissionMutex;
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    bool m_finished = false;
    std::exception_ptr m_exception;
    std::vector<std::function<void()>> m_continuations;
    std::function<void()> m_onCompleted;
};

using JobHandle = std::shared_ptr<Job>;

class JobQueue
{
  public:
    void Push(const JobHandle &job);
    JobHandle PopBlocking(std::atomic<bool> &runningFlag);
    bool Empty() const;
    void NotifyAll();

  private:
    bool HasJobsUnsafe() const;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::array<std::deque<JobHandle>, 4> m_queues;
};

class WorkerThread
{
  public:
    explicit WorkerThread(JobQueue *queue);
    ~WorkerThread();
    void Start();
    void Stop();
    void Join();

  private:
    void RunLoop();
    JobQueue *m_queue = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

class JobSystem
{
  public:
    void Initialize(std::uint32_t workerCount);
    void Shutdown();

    JobHandle CreateJob(std::function<void()> func, JobPriority priority = JobPriority::Normal);
    void AddDependency(const JobHandle &job, const JobHandle &dependency);
    void Submit(const JobHandle &job);
    void Wait(const JobHandle &job) const;
    void WaitIdle() const;
    void Dispatch(std::size_t count, const std::function<void(std::size_t)> &task);
    std::size_t GetOutstandingJobCount() const;

  private:
    void QueueWhenReady(const JobHandle &job);
    void OnJobCompleted();

    JobQueue m_globalQueue;
    std::vector<std::unique_ptr<WorkerThread>> m_workers;
    std::atomic<std::size_t> m_outstanding{0};
    mutable std::mutex m_idleMutex;
    mutable std::condition_variable m_idleCv;
    std::atomic<bool> m_accepting{false};
};
} // namespace MiniEngine
