#pragma once

#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Task/JobSystem.hpp"

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
        void Initialize(JobSystem* jobSystem = nullptr);
        void Shutdown();

        template<typename T>
        std::shared_ptr<T> Load(const std::string& path)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                const auto found = m_cache.find(path);
                if (found != m_cache.end())
                {
                    return std::dynamic_pointer_cast<T>(found->second);
                }
            }

            auto fromLoader = std::dynamic_pointer_cast<T>(LoadWithRegisteredLoaders(path));
            if (fromLoader != nullptr)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_cache[path] = fromLoader;
                return fromLoader;
            }

            auto resource = std::make_shared<T>();
            if (!resource->Load(path))
            {
                return nullptr;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            m_cache[path] = resource;
            return resource;
        }

        template<typename T>
        std::shared_ptr<T> LoadAsync(const std::string& path)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                const auto found = m_cache.find(path);
                if (found != m_cache.end())
                {
                    return std::dynamic_pointer_cast<T>(found->second);
                }
            }

            auto resource = std::make_shared<T>();
            resource->MarkLoading();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_cache[path] = resource;
            }

            if (m_jobSystem == nullptr)
            {
                resource->Load(path);
                return resource;
            }

            auto job = m_jobSystem->CreateJob([resource, path]()
            {
                resource->Load(path);
            });
            m_jobSystem->Submit(job);
            return resource;
        }

        void RegisterLoader(std::shared_ptr<IAssetLoader> loader);
        void GarbageCollect();

    private:
        std::shared_ptr<Resource> LoadWithRegisteredLoaders(const std::string& path);

    private:
        std::unordered_map<std::string, std::shared_ptr<Resource>> m_cache;
        std::vector<std::shared_ptr<IAssetLoader>> m_loaders;
        mutable std::mutex m_mutex;
        JobSystem* m_jobSystem = nullptr;
    };
}
