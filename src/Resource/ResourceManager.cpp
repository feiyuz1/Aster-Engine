#include "MiniEngine/Resource/ResourceManager.hpp"

#include <algorithm>
#include <filesystem>

namespace MiniEngine
{
    void ResourceManager::Initialize(JobSystem* jobSystem)
    {
        m_jobSystem = jobSystem;
    }

    void ResourceManager::Shutdown()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cache.clear();
        m_loaders.clear();
        m_jobSystem = nullptr;
    }

    void ResourceManager::RegisterLoader(std::shared_ptr<IAssetLoader> loader)
    {
        if (loader == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_loaders.push_back(std::move(loader));
    }

    void ResourceManager::GarbageCollect()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_cache.begin(); it != m_cache.end();)
        {
            const auto& resource = it->second;
            if (resource.use_count() <= 1 && resource->GetState() != ResourceState::Loading)
            {
                it = m_cache.erase(it);
                continue;
            }

            ++it;
        }
    }

    std::shared_ptr<Resource> ResourceManager::LoadWithRegisteredLoaders(const std::string& path)
    {
        std::vector<std::shared_ptr<IAssetLoader>> loadersCopy;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            loadersCopy = m_loaders;
        }

        const std::string extension = std::filesystem::path(path).extension().string();
        for (const auto& loader : loadersCopy)
        {
            if (loader != nullptr && loader->CanLoad(extension))
            {
                return loader->Load(path);
            }
        }

        return nullptr;
    }
}
