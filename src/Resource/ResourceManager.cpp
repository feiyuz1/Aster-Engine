// 文件：src/Resource/ResourceManager.cpp
// 作用：统一资源缓存、同步/异步加载、加载器扩展和回收。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：实现文件。

#include "MiniEngine/Resource/ResourceManager.hpp"

#include <filesystem>

namespace MiniEngine
{
void ResourceManager::Initialize(JobSystem *jobSystem, std::filesystem::path assetRoot)
{
    m_jobSystem = jobSystem;
    m_assetRoot = std::move(assetRoot);
    m_registry.Scan(m_assetRoot);
}

// 先停止异步入口的使用者，再清理缓存；ResourceManager 本身不拥有外部 JobSystem。
void ResourceManager::Shutdown()
{
    if (m_jobSystem)
        m_jobSystem->WaitIdle();
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &pair : m_cache)
        if (pair.second && pair.second->IsReady())
            pair.second->Unload();
    m_cache.clear();
    m_loaders.clear();
    m_jobSystem = nullptr;
}

void ResourceManager::RegisterLoader(std::shared_ptr<IAssetLoader> loader)
{
    if (!loader)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_loaders.push_back(std::move(loader));
}

// 仅回收缓存独占且未处于 Loading 的对象，避免任务线程仍写入已经析构的 Resource。
void ResourceManager::GarbageCollect()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_cache.begin(); it != m_cache.end();)
    {
        if (it->second.use_count() <= 1 && it->second->GetState() != ResourceState::Loading)
        {
            it->second->Unload();
            it = m_cache.erase(it);
        }
        else
            ++it;
    }
}

bool ResourceManager::UnloadUnused(const std::string &path)
{
    const std::string key = NormalizePath(path);
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_cache.find(key);
    if (found == m_cache.end())
        return true;
    if (found->second.use_count() > 1 || found->second->GetState() == ResourceState::Loading)
        return false;
    found->second->Unload();
    m_cache.erase(found);
    return true;
}

bool ResourceManager::Reload(const std::string &path)
{
    const std::string key = NormalizePath(path);
    std::shared_ptr<Resource> resource;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_cache.find(key);
        if (found == m_cache.end())
            return false;
        resource = found->second;
    }
    resource->Unload();
    return resource->Load(key);
}

std::size_t ResourceManager::GetCachedResourceCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cache.size();
}

std::size_t ResourceManager::GetCachedByteSize() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::size_t total = 0;
    for (const auto &pair : m_cache)
        if (pair.second && pair.second->IsReady())
            total += pair.second->GetByteSize();
    return total;
}

AssetRegistry &ResourceManager::GetAssetRegistry()
{
    return m_registry;
}
const AssetRegistry &ResourceManager::GetAssetRegistry() const
{
    return m_registry;
}

std::string ResourceManager::NormalizePath(const std::string &path)
{
    return std::filesystem::path(path).lexically_normal().generic_string();
}

std::shared_ptr<Resource> ResourceManager::LoadWithRegisteredLoaders(const std::string &path)
{
    std::vector<std::shared_ptr<IAssetLoader>> loaders;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        loaders = m_loaders;
    }
    const std::string extension = std::filesystem::path(path).extension().string();
    for (const auto &loader : loaders)
        if (loader && loader->CanLoad(extension))
            return loader->Load(path);
    return nullptr;
}
} // namespace MiniEngine
