// 文件：include/MiniEngine/Resource/AssetStreaming.hpp
// 作用：根据依赖、距离和内存预算调度大型资产的分块流式加载。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Resource/AssetRegistry.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MiniEngine
{
template <typename T> class AssetHandle
{
  public:
    AssetHandle() = default;
    AssetHandle(std::string path, std::shared_ptr<T> resource) : m_path(std::move(path)), m_resource(std::move(resource))
    {
    }
    const std::string &GetPath() const
    {
        return m_path;
    }
    bool IsValid() const
    {
        return static_cast<bool>(m_resource);
    }
    bool IsReady() const
    {
        return m_resource && m_resource->IsReady();
    }
    bool Wait() const
    {
        return m_resource && m_resource->Wait();
    }
    T *Get() const
    {
        return IsReady() ? m_resource.get() : nullptr;
    }
    const std::shared_ptr<T> &GetShared() const
    {
        return m_resource;
    }

  private:
    std::string m_path;
    std::shared_ptr<T> m_resource;
};

// 从 Registry 构建只读依赖图，并能计算传递闭包及报告循环依赖。
class AssetDependencyGraph
{
  public:
    void Build(const AssetRegistry &registry);
    std::vector<std::string> GetDependencies(const std::string &path) const;
    std::vector<std::string> ResolveClosure(const std::string &path, std::vector<std::string> *missing = nullptr) const;
    bool HasCycles() const;

  private:
    bool Visit(const std::string &path, std::unordered_set<std::string> &visiting, std::unordered_set<std::string> &visited,
               std::vector<std::string> &output, std::vector<std::string> *missing) const;
    std::unordered_map<std::string, std::vector<std::string>> m_dependencies;
    std::unordered_set<std::string> m_knownAssets;
    bool m_hasCycles = false;
};

// 基于请求优先级和预算推进流式状态机；Update 只提交有限工作，避免单帧加载尖峰。
class AssetStreamingManager
{
  public:
    void Initialize(ResourceManager *resources, std::size_t memoryBudgetBytes = 256 * 1024 * 1024);
    void Shutdown();

    template <typename T> AssetHandle<T> Request(const std::string &path, const bool pinned = false)
    {
        if (!m_resources)
            return {};
        auto resource = m_resources->LoadAsync<T>(path);
        Track(path, resource, pinned);
        return {path, std::move(resource)};
    }

    void Pin(const std::string &path, bool pinned = true);
    void Touch(const std::string &path);
    void Update();
    void SetMemoryBudget(std::size_t bytes);
    std::size_t GetMemoryBudget() const;
    std::size_t GetResidentBytes() const;
    std::size_t GetTrackedAssetCount() const;
    const AssetDependencyGraph &GetDependencyGraph() const;
    void RebuildDependencyGraph();

  private:
    struct Entry
    {
        std::string path;
        std::weak_ptr<Resource> resource;
        std::uint64_t lastTouched = 0;
        bool pinned = false;
    };
    void Track(const std::string &path, const std::shared_ptr<Resource> &resource, bool pinned);

    ResourceManager *m_resources = nullptr;
    std::unordered_map<std::string, Entry> m_entries;
    AssetDependencyGraph m_graph;
    std::size_t m_budget = 256 * 1024 * 1024;
    std::uint64_t m_tick = 0;
};
} // namespace MiniEngine
