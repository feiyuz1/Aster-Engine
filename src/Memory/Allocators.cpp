// 文件：src/Memory/Allocators.cpp
// 作用：提供线性、池式分配器和引擎内存统计服务。
// 约定：分配器返回内存的有效期由所属 Arena 或 Pool 决定。
// 类型：实现文件。

#include "MiniEngine/Memory/Allocators.hpp"

#include <algorithm>

namespace MiniEngine
{
namespace
{
std::uintptr_t AlignForward(const std::uintptr_t address, const std::size_t alignment)
{
    const std::size_t mask = alignment - 1;
    return (address + mask) & ~mask;
}
} // namespace

LinearAllocator::LinearAllocator(const std::size_t sizeBytes)
    : m_storage(std::make_unique<std::uint8_t[]>(sizeBytes)), m_begin(m_storage.get()), m_current(m_storage.get()), m_size(sizeBytes)
{
}

void *LinearAllocator::Allocate(const std::size_t size, const std::size_t alignment)
{
    if (size == 0 || alignment == 0)
    {
        return nullptr;
    }

    const std::uintptr_t currentAddress = reinterpret_cast<std::uintptr_t>(m_current);
    const std::uintptr_t alignedAddress = AlignForward(currentAddress, alignment);
    const std::size_t usedSize = static_cast<std::size_t>(alignedAddress - reinterpret_cast<std::uintptr_t>(m_begin));

    if (usedSize + size > m_size)
    {
        return nullptr;
    }

    m_current = reinterpret_cast<std::uint8_t *>(alignedAddress + size);
    return reinterpret_cast<void *>(alignedAddress);
}

void LinearAllocator::Free(void *ptr)
{
    (void)ptr;
}

void LinearAllocator::Reset()
{
    m_current = m_begin;
}

PoolAllocator::PoolAllocator(const std::size_t blockSize, const std::size_t blockCount)
    : m_storage(std::make_unique<std::uint8_t[]>(
          ((std::max(blockSize, sizeof(void *)) + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1)) * blockCount)),
      m_blockSize((std::max(blockSize, sizeof(void *)) + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1)),
      m_blockCount(blockCount)
{
    Reset();
}

void *PoolAllocator::Allocate(const std::size_t size, const std::size_t alignment)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (size > m_blockSize || alignment > alignof(std::max_align_t) || m_freeList == nullptr)
    {
        return nullptr;
    }

    void *block = m_freeList;
    m_freeList = *reinterpret_cast<void **>(m_freeList);
    return block;
}

void PoolAllocator::Free(void *ptr)
{
    if (ptr == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    *reinterpret_cast<void **>(ptr) = m_freeList;
    m_freeList = ptr;
}

void PoolAllocator::Reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::uint8_t *current = m_storage.get();
    for (std::size_t i = 0; i < m_blockCount; ++i)
    {
        void *next = (i + 1 < m_blockCount) ? static_cast<void *>(current + m_blockSize) : nullptr;
        *reinterpret_cast<void **>(current) = next;
        current += m_blockSize;
    }
    m_freeList = m_storage.get();
}

void MemoryManager::Initialize()
{
    m_frameAllocator = std::make_unique<LinearAllocator>(1024 * 1024);
    m_smallObjectAllocator = std::make_unique<PoolAllocator>(256, 2048);
}

void MemoryManager::Shutdown()
{
    m_smallObjectAllocator.reset();
    m_frameAllocator.reset();
}

IAllocator *MemoryManager::GetFrameAllocator()
{
    return m_frameAllocator.get();
}

IAllocator *MemoryManager::GetSmallObjectAllocator()
{
    return m_smallObjectAllocator.get();
}

void MemoryManager::ResetFrame()
{
    if (m_frameAllocator != nullptr)
    {
        m_frameAllocator->Reset();
    }
}
} // namespace MiniEngine
