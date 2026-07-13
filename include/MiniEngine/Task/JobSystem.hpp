#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace MiniEngine
{
    enum class JobPriority
    {
        Low,
        Normal,
        High,
        Critical
    };

    class Job
    {
    public:
        explicit Job(std::function<void()> inTask, JobPriority inPriority = JobPriority::Normal);

        void Execute();
        void Wait() const;
        bool IsFinished() const;

    public:
        std::function<void()> task;
        std::atomic<int> dependencyCount{0};
        JobPriority priority = JobPriority::Normal;

    private:
        mutable std::mutex m_mutex;
        mutable std::condition_variable m_cv;
        bool m_finished = false;
    };

    using JobHandle = std::shared_ptr<Job>;

    class JobQueue
    {
    public:
        void Push(const JobHandle& job);
        JobHandle PopBlocking(std::atomic<bool>& runningFlag);
        bool Empty() const;
        void NotifyAll();

    private:
        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::deque<JobHandle> m_jobs;
    };

    class WorkerThread
    {
    public:
        explicit WorkerThread(JobQueue* queue);
        ~WorkerThread();

        void Start();
        void Stop();
        void Join();
        void RunLoop();

    private:
        JobQueue* m_queue = nullptr;
        std::atomic<bool> m_running{false};
        std::thread m_thread;
    };

    class JobSystem
    {
    public:
        void Initialize(std::uint32_t workerCount);
        void Shutdown();

        JobHandle CreateJob(std::function<void()> func, JobPriority priority = JobPriority::Normal);
        void Submit(const JobHandle& job);
        void Wait(const JobHandle& job);
        void Dispatch(std::size_t count, const std::function<void(std::size_t)>& task);

    private:
        JobQueue m_globalQueue;
        std::vector<std::unique_ptr<WorkerThread>> m_workers;
    };
}
