#include "MiniEngine/Resource/ResourceManager.hpp"

#include <filesystem>

namespace MiniEngine
{
    void ResourceManager::Initialize(JobSystem* jobSystem, std::filesystem::path assetRoot)
    {
        m_jobSystem = jobSystem;
        m_assetRoot = std::move(assetRoot);
        m_registry.Scan(m_assetRoot);
    }

    void ResourceManager::Shutdown()
    {
        if (m_jobSystem) m_jobSystem->WaitIdle();
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pair : m_cache)
            if (pair.second && pair.second->IsReady()) pair.second->Unload();
        m_cache.clear();
        m_loaders.clear();
        m_jobSystem = nullptr;
    }

    void ResourceManager::RegisterLoader(std::shared_ptr<IAssetLoader> loader)
    {
        if (!loader) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_loaders.push_back(std::move(loader));
    }

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
            else ++it;
        }
    }

    bool ResourceManager::Reload(const std::string& path)
    {
        const std::string key = NormalizePath(path);
        std::shared_ptr<Resource> resource;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto found = m_cache.find(key);
            if (found == m_cache.end()) return false;
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

    AssetRegistry& ResourceManager::GetAssetRegistry() { return m_registry; }
    const AssetRegistry& ResourceManager::GetAssetRegistry() const { return m_registry; }

    std::string ResourceManager::NormalizePath(const std::string& path)
    {
        return std::filesystem::path(path).lexically_normal().generic_string();
    }

    std::shared_ptr<Resource> ResourceManager::LoadWithRegisteredLoaders(const std::string& path)
    {
        std::vector<std::shared_ptr<IAssetLoader>> loaders;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            loaders = m_loaders;
        }
        const std::string extension = std::filesystem::path(path).extension().string();
        for (const auto& loader : loaders)
            if (loader && loader->CanLoad(extension)) return loader->Load(path);
        return nullptr;
    }
}
