// 文件：src/Resource/AssetStreaming.cpp
// 作用：根据依赖、距离和内存预算调度大型资产的分块流式加载。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：实现文件。

#include "MiniEngine/Resource/AssetStreaming.hpp"

#include <algorithm>
#include <filesystem>

namespace MiniEngine
{
namespace
{
std::string Normalize(const std::string &path)
{
    return std::filesystem::path(path).lexically_normal().generic_string();
}
} // namespace

void AssetDependencyGraph::Build(const AssetRegistry &registry)
{
    m_dependencies.clear();
    m_knownAssets.clear();
    m_hasCycles = false;
    for (const auto &asset : registry.GetAssets())
    {
        const std::string path = Normalize(asset.path);
        m_knownAssets.insert(path);
        auto &dependencies = m_dependencies[path];
        dependencies.reserve(asset.dependencies.size());
        for (const auto &dependency : asset.dependencies)
            dependencies.push_back(Normalize(dependency));
    }
    for (const auto &asset : m_knownAssets)
    {
        std::unordered_set<std::string> visiting, visited;
        std::vector<std::string> ignored;
        if (!Visit(asset, visiting, visited, ignored, nullptr))
            m_hasCycles = true;
    }
}

std::vector<std::string> AssetDependencyGraph::GetDependencies(const std::string &path) const
{
    const auto found = m_dependencies.find(Normalize(path));
    return found == m_dependencies.end() ? std::vector<std::string>{} : found->second;
}

// 闭包按依赖优先顺序返回，并用访问集合同时防止重复项与循环递归。
std::vector<std::string> AssetDependencyGraph::ResolveClosure(const std::string &path, std::vector<std::string> *missing) const
{
    std::unordered_set<std::string> visiting, visited;
    std::vector<std::string> result;
    Visit(Normalize(path), visiting, visited, result, missing);
    return result;
}

bool AssetDependencyGraph::HasCycles() const
{
    return m_hasCycles;
}

bool AssetDependencyGraph::Visit(const std::string &path, std::unordered_set<std::string> &visiting,
                                 std::unordered_set<std::string> &visited, std::vector<std::string> &output,
                                 std::vector<std::string> *missing) const
{
    if (visited.count(path))
        return true;
    if (!visiting.insert(path).second)
        return false;
    if (!m_knownAssets.count(path))
    {
        if (missing)
            missing->push_back(path);
        visiting.erase(path);
        return true;
    }
    bool acyclic = true;
    const auto dependencies = m_dependencies.find(path);
    if (dependencies != m_dependencies.end())
        for (const auto &dependency : dependencies->second)
            acyclic = Visit(dependency, visiting, visited, output, missing) && acyclic;
    visiting.erase(path);
    if (visited.insert(path).second)
        output.push_back(path);
    return acyclic;
}

void AssetStreamingManager::Initialize(ResourceManager *resources, const std::size_t memoryBudgetBytes)
{
    m_resources = resources;
    m_budget = memoryBudgetBytes;
    m_tick = 0;
    m_entries.clear();
    RebuildDependencyGraph();
}

void AssetStreamingManager::Shutdown()
{
    m_entries.clear();
    m_resources = nullptr;
}

void AssetStreamingManager::Track(const std::string &path, const std::shared_ptr<Resource> &resource, const bool pinned)
{
    const std::string key = Normalize(path);
    auto &entry = m_entries[key];
    entry.path = key;
    entry.resource = resource;
    entry.lastTouched = ++m_tick;
    entry.pinned = entry.pinned || pinned;
}

void AssetStreamingManager::Pin(const std::string &path, const bool pinned)
{
    const auto found = m_entries.find(Normalize(path));
    if (found != m_entries.end())
        found->second.pinned = pinned;
}

void AssetStreamingManager::Touch(const std::string &path)
{
    const auto found = m_entries.find(Normalize(path));
    if (found != m_entries.end())
        found->second.lastTouched = ++m_tick;
}

// 超预算时按“未固定且最久未使用”顺序回收；仅移除管理器持有的引用，不强制破坏业务引用。
void AssetStreamingManager::Update()
{
    if (!m_resources)
        return;
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (it->second.resource.expired() && !it->second.pinned)
            it = m_entries.erase(it);
        else
            ++it;
    }
    std::vector<Entry *> candidates;
    for (auto &pair : m_entries)
        if (!pair.second.pinned && pair.second.resource.use_count() == 1)
            candidates.push_back(&pair.second);
    std::sort(candidates.begin(), candidates.end(),
              [](const Entry *left, const Entry *right) { return left->lastTouched < right->lastTouched; });
    std::size_t resident = m_resources->GetCachedByteSize();
    for (const auto *candidate : candidates)
    {
        if (resident <= m_budget)
            break;
        auto resource = candidate->resource.lock();
        const std::size_t bytes = resource ? resource->GetByteSize() : 0;
        resource.reset();
        if (m_resources->UnloadUnused(candidate->path))
            resident = bytes > resident ? 0 : resident - bytes;
    }
}

void AssetStreamingManager::SetMemoryBudget(const std::size_t bytes)
{
    m_budget = bytes;
}

std::size_t AssetStreamingManager::GetMemoryBudget() const
{
    return m_budget;
}

std::size_t AssetStreamingManager::GetResidentBytes() const
{
    return m_resources ? m_resources->GetCachedByteSize() : 0;
}

std::size_t AssetStreamingManager::GetTrackedAssetCount() const
{
    return m_entries.size();
}

const AssetDependencyGraph &AssetStreamingManager::GetDependencyGraph() const
{
    return m_graph;
}

void AssetStreamingManager::RebuildDependencyGraph()
{
    if (m_resources)
        m_graph.Build(m_resources->GetAssetRegistry());
}
} // namespace MiniEngine
