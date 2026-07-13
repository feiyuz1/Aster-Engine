#pragma once

#include "MiniEngine/Resource/AssetRegistry.hpp"
#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Task/JobSystem.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
    class IAssetLoader
    {
    public:
        virtual ~IAssetLoader() = default;
        virtual bool CanLoad(const std::string& extension) const = 0;
        virtual std::shared_ptr<Resource> Load(const std::string& path) = 0;
    };

    class ResourceManager
    {
    public:
        void Initialize(JobSystem* jobSystem = nullptr, std::filesystem::path assetRoot = "assets");
        void Shutdown();

        template<typename T>
        std::shared_ptr<T> Load(const std::string& path)
        {
            const std::string key = NormalizePath(path);
            std::shared_ptr<T> resource;
            bool created = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                const auto found = m_cache.find(key);
                if (found != m_cache.end()) resource = std::dynamic_pointer_cast<T>(found->second);
                if (!resource)
                {
                    resource = std::make_shared<T>();
                    resource->MarkLoading(key);
                    m_cache[key] = resource;
                    created = true;
                }
            }
            if (!created && resource->GetState() == ResourceState::Loading)
                return resource->Wait() ? resource : nullptr;
            if (resource->IsReady()) return resource;
            if (!resource->Load(key))
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                const auto found = m_cache.find(key);
                if (found != m_cache.end() && found->second == resource) m_cache.erase(found);
                return nullptr;
            }
            return resource;
        }

        template<typename T>
        std::shared_ptr<T> LoadAsync(const std::string& path)
        {
            const std::string key = NormalizePath(path);
            std::shared_ptr<T> resource;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                const auto found = m_cache.find(key);
                if (found != m_cache.end())
                {
                    resource = std::dynamic_pointer_cast<T>(found->second);
                    if (resource && resource->GetState() != ResourceState::Failed) return resource;
                    m_cache.erase(found);
                }
                resource = std::make_shared<T>();
                resource->MarkLoading(key);
                m_cache[key] = resource;
            }
            if (!m_jobSystem)
            {
                resource->Load(key);
                return resource;
            }
            auto job = m_jobSystem->CreateJob([resource, key]() { resource->Load(key); }, JobPriority::High);
            m_jobSystem->Submit(job);
            return resource;
        }

        void RegisterLoader(std::shared_ptr<IAssetLoader> loader);
        void GarbageCollect();
        bool Reload(const std::string& path);
        std::size_t GetCachedResourceCount() const;
        AssetRegistry& GetAssetRegistry();
        const AssetRegistry& GetAssetRegistry() const;

    private:
        static std::string NormalizePath(const std::string& path);
        std::shared_ptr<Resource> LoadWithRegisteredLoaders(const std::string& path);

        std::unordered_map<std::string, std::shared_ptr<Resource>> m_cache;
        std::vector<std::shared_ptr<IAssetLoader>> m_loaders;
        mutable std::mutex m_mutex;
        JobSystem* m_jobSystem = nullptr;
        std::filesystem::path m_assetRoot;
        AssetRegistry m_registry;
    };
}
