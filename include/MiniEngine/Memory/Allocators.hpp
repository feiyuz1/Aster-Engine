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
    virtual ~IAllocator() = default;
    virtual void *Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) = 0;
    virtual void Free(void *ptr) = 0;
    virtual void Reset() = 0;
};

class LinearAllocator final : public IAllocator
{
  public:
    explicit LinearAllocator(std::size_t sizeBytes);

    void *Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    void Free(void *ptr) override;
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
    PoolAllocator(std::size_t blockSize, std::size_t blockCount);

    void *Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
    void Free(void *ptr) override;
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
    void Initialize();
    void Shutdown();

    IAllocator *GetFrameAllocator();
    IAllocator *GetSmallObjectAllocator();
    void ResetFrame();

  private:
    std::unique_ptr<LinearAllocator> m_frameAllocator;
    std::unique_ptr<PoolAllocator> m_smallObjectAllocator;
};
} // namespace MiniEngine
