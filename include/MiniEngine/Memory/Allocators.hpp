// 文件：include/MiniEngine/Memory/Allocators.hpp
// 作用：提供线性、池式分配器和引擎内存统计服务。
// 约定：分配器返回内存的有效期由所属 Arena 或 Pool 决定。
// 类型：公开声明。

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace MiniEngine
{
class IAllocator
{
  public:
    /// @brief 析构 `IAllocator` 对象并释放其持有资源。
    virtual ~IAllocator() = default;
    /// @brief 按指定尺寸和对齐方式分配内存。
    /// @param size 数据、内存或目标区域的尺寸。
    /// @param alignment 内存对齐字节数，必须为有效的二次幂。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    virtual void *Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) = 0;
    /// @brief 释放先前分配的内存。
    /// @param ptr 要释放或操作的内存指针。
    virtual void Free(void *ptr) = 0;
    /// @brief 将对象恢复为初始状态。
    virtual void Reset() = 0;
};

class LinearAllocator final : public IAllocator
{
  public:
    /// @brief 构造 `LinearAllocator` 对象。
    /// @param sizeBytes 目标尺寸、数量、索引或限制值。
    explicit LinearAllocator(std::size_t sizeBytes);

    /// @brief 按指定尺寸和对齐方式分配内存。
    /// @param size 数据、内存或目标区域的尺寸。
    /// @param alignment 内存对齐字节数，必须为有效的二次幂。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    void *Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    /// @brief 释放先前分配的内存。
    /// @param ptr 要释放或操作的内存指针。
    void Free(void *ptr) override;
    /// @brief 将对象恢复为初始状态。
    void Reset() override;

  private:
    std::unique_ptr<std::uint8_t[]> m_storage;
    std::uint8_t *m_begin = nullptr;
    std::uint8_t *m_current = nullptr;
    std::size_t m_size = 0;
};

class PoolAllocator final : public IAllocator
{
  public:
    /// @brief 构造 `PoolAllocator` 对象。
    /// @param blockSize 目标尺寸、数量、索引或限制值。
    /// @param blockCount 目标尺寸、数量、索引或限制值。
    PoolAllocator(std::size_t blockSize, std::size_t blockCount);

    /// @brief 按指定尺寸和对齐方式分配内存。
    /// @param size 数据、内存或目标区域的尺寸。
    /// @param alignment 内存对齐字节数，必须为有效的二次幂。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    void *Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    /// @brief 释放先前分配的内存。
    /// @param ptr 要释放或操作的内存指针。
    void Free(void *ptr) override;
    /// @brief 将对象恢复为初始状态。
    void Reset() override;

  private:
    std::unique_ptr<std::uint8_t[]> m_storage;
    void *m_freeList = nullptr;
    std::size_t m_blockSize = 0;
    std::size_t m_blockCount = 0;
    std::mutex m_mutex;
};

class MemoryManager
{
  public:
    /// @brief 初始化对象及其依赖资源。
    void Initialize();
    /// @brief 停止服务并释放对象持有的资源。
    void Shutdown();

    /// @brief 获取 `FrameAllocator`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    IAllocator *GetFrameAllocator();
    /// @brief 获取 `SmallObjectAllocator`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    IAllocator *GetSmallObjectAllocator();
    /// @brief 重置 `Frame`。
    void ResetFrame();

  private:
    std::unique_ptr<LinearAllocator> m_frameAllocator;
    std::unique_ptr<PoolAllocator> m_smallObjectAllocator;
};
} // 结束 MiniEngine 命名空间
