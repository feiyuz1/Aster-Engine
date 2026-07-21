// 文件：include/MiniEngine/Task/JobSystem.hpp
// 作用：实现带优先级与依赖关系的工作线程池和作业同步。
// 约定：作业函数可在任意工作线程执行，不得依赖主线程局部状态。
// 类型：公开声明。

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

// 可等待的作业节点。依赖计数归零后才入队，异常在 Wait 调用线程重新抛出。
class Job
{
  public:
    /// @brief 构造 `Job` 对象。
    /// @param task 作业系统执行的任务函数。
    /// @param priority 任务或资源请求优先级。
    explicit Job(std::function<void()> task, JobPriority priority = JobPriority::Normal);

    /// @brief 等待操作完成，并在失败时传播错误。
    void Wait() const;
    /// @brief 判断是否满足 `Finished` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsFinished() const;
    /// @brief 获取 `Priority`。
    /// @return 返回接口计算或查询得到的结果。
    JobPriority GetPriority() const;

  private:
    friend class JobSystem;
    friend class WorkerThread;
    /// @brief 执行已准备好的操作。
    void Execute();
    /// @brief 添加 `Continuation`。
    /// @param continuation 满足条件时调用的回调函数。
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

// 四级优先队列；PopBlocking 总是优先取高优先级任务，并由条件变量避免空转。
class JobQueue
{
  public:
    /// @brief 把数据压入队列或缓冲区。
    /// @param job 目标作业句柄。
    void Push(const JobHandle &job);
    /// @brief 取出 `Blocking`。
    /// @param runningFlag 由接口读取并可能修改的输入/输出参数。
    /// @return 返回接口计算或查询得到的结果。
    JobHandle PopBlocking(std::atomic<bool> &runningFlag);
    /// @brief 判断容器当前是否为空。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool Empty() const;
    /// @brief 唤醒全部等待线程。
    void NotifyAll();

  private:
    /// @brief 判断是否包含 `JobsUnsafe` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool HasJobsUnsafe() const;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::array<std::deque<JobHandle>, 4> m_queues;
};

// WorkerThread 只拥有线程本身，Job 和队列生命周期由 JobSystem 统一管理。
class WorkerThread
{
  public:
    /// @brief 构造 `WorkerThread` 对象。
    /// @param queue 接口所需的输入参数。
    explicit WorkerThread(JobQueue *queue);
    /// @brief 析构 `WorkerThread` 对象并释放其持有资源。
    ~WorkerThread();
    /// @brief 启动对应任务或服务。
    void Start();
    /// @brief 停止对应任务或服务。
    void Stop();
    /// @brief 等待工作线程结束。
    void Join();

  private:
    /// @brief 执行 `RunLoop` 所表示的操作。
    void RunLoop();
    JobQueue *m_queue = nullptr;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

// 通用工作线程池。Shutdown 停止接收新任务并等待已提交任务结束后再回收线程。
class JobSystem
{
  public:
    /// @brief 初始化对象及其依赖资源。
    /// @param workerCount 目标尺寸、数量、索引或限制值。
    void Initialize(std::uint32_t workerCount);
    /// @brief 停止服务并释放对象持有的资源。
    void Shutdown();

    /// @brief 创建 `Job`。
    /// @param func 工作线程或遍历过程调用的函数。
    /// @param priority 任务或资源请求优先级。
    /// @return 返回接口计算或查询得到的结果。
    JobHandle CreateJob(std::function<void()> func, JobPriority priority = JobPriority::Normal);
    /// @brief 添加 `Dependency`。
    /// @param job 目标作业句柄。
    /// @param dependency 接口所需的输入参数。
    void AddDependency(const JobHandle &job, const JobHandle &dependency);
    /// @brief 提交任务并返回跟踪信息。
    /// @param job 目标作业句柄。
    void Submit(const JobHandle &job);
    /// @brief 等待操作完成，并在失败时传播错误。
    /// @param job 目标作业句柄。
    void Wait(const JobHandle &job) const;
    /// @brief 等待所有已提交工作完成。
    void WaitIdle() const;
    /// @brief 分派待处理事件或任务。
    /// @param count 要处理或生成的元素数量。
    /// @param task 作业系统执行的任务函数。
    void Dispatch(std::size_t count, const std::function<void(std::size_t)> &task);
    /// @brief 获取 `OutstandingJobCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetOutstandingJobCount() const;

  private:
    /// @brief 加入队列 `WhenReady`。
    /// @param job 目标作业句柄。
    void QueueWhenReady(const JobHandle &job);
    /// @brief 响应 `JobCompleted`。
    void OnJobCompleted();

    JobQueue m_globalQueue;
    std::vector<std::unique_ptr<WorkerThread>> m_workers;
    std::atomic<std::size_t> m_outstanding{0};
    mutable std::mutex m_idleMutex;
    mutable std::condition_variable m_idleCv;
    std::atomic<bool> m_accepting{false};
};
} // namespace MiniEngine
