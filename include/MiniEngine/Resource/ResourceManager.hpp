// 文件：include/MiniEngine/Resource/ResourceManager.hpp
// 作用：统一资源缓存、同步/异步加载、加载器扩展和回收。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：公开声明。

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
// 自定义资产加载器扩展点；CanLoad 接收规范化的小写扩展名。
class IAssetLoader
{
  public:
    /// @brief 析构 `IAssetLoader` 对象并释放其持有资源。
    virtual ~IAssetLoader() = default;
    /// @brief 判断是否可以 `Load` 条件。
    /// @param extension 用于查找或标识对象的名称。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    virtual bool CanLoad(const std::string &extension) const = 0;
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 返回接口计算或查询得到的结果。
    virtual std::shared_ptr<Resource> Load(const std::string &path) = 0;
};

// 线程安全资源缓存。同一路径在并发加载时只创建一个 Resource 实例，调用方共享其状态机。
class ResourceManager
{
  public:
    /// @brief 初始化对象及其依赖资源。
    /// @param jobSystem 接口所需的输入参数。
    /// @param assetRoot 相关文件、资源或目录路径。
    void Initialize(JobSystem *jobSystem = nullptr, std::filesystem::path assetRoot = "assets");
    /// @brief 停止服务并释放对象持有的资源。
    void Shutdown();

    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 返回接口计算或查询得到的结果。
    template <typename T> std::shared_ptr<T> Load(const std::string &path)
    {
        const std::string key = NormalizePath(path);
        std::shared_ptr<T> resource;
        bool created = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto found = m_cache.find(key);
            if (found != m_cache.end())
                resource = std::dynamic_pointer_cast<T>(found->second);
            if (!resource)
            {
                resource = std::make_shared<T>();
                resource->MarkLoading(key);
                m_cache[key] = resource;
                created = true;
            }
        }
        // 已有线程负责加载时只等待状态变化，不能在当前线程重复执行 Load。
        if (!created && resource->GetState() == ResourceState::Loading)
            return resource->Wait() ? resource : nullptr;
        if (resource->IsReady())
            return resource;
        if (!resource->Load(key))
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto found = m_cache.find(key);
            if (found != m_cache.end() && found->second == resource)
                m_cache.erase(found);
            return nullptr;
        }
        return resource;
    }

    /// @brief 加载 `Async`。
    /// @param path 文件、资源或目录路径。
    /// @return 返回接口计算或查询得到的结果。
    template <typename T> std::shared_ptr<T> LoadAsync(const std::string &path)
    {
        const std::string key = NormalizePath(path);
        std::shared_ptr<T> resource;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto found = m_cache.find(key);
            if (found != m_cache.end())
            {
                resource = std::dynamic_pointer_cast<T>(found->second);
                if (resource && resource->GetState() != ResourceState::Failed)
                    return resource;
                m_cache.erase(found);
            }
            resource = std::make_shared<T>();
            resource->MarkLoading(key);
            m_cache[key] = resource;
        }
        // 无 JobSystem 的工具环境退化为同步加载，但仍保持相同的资源状态语义。
        if (!m_jobSystem)
        {
            resource->Load(key);
            return resource;
        }
        auto job = m_jobSystem->CreateJob([resource, key]() { resource->Load(key); }, JobPriority::High);
        m_jobSystem->Submit(job);
        return resource;
    }

    /// @brief 注册 `Loader`。
    /// @param loader 接口所需的输入参数。
    void RegisterLoader(std::shared_ptr<IAssetLoader> loader);
    /// @brief 回收不再被外部引用的缓存对象。
    void GarbageCollect();
    /// @brief 卸载 `Unused`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool UnloadUnused(const std::string &path);
    /// @brief 重新加载资源并更新缓存。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Reload(const std::string &path);
    /// @brief 获取 `CachedResourceCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetCachedResourceCount() const;
    /// @brief 获取 `CachedByteSize`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetCachedByteSize() const;
    /// @brief 获取 `AssetRegistry`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    AssetRegistry &GetAssetRegistry();
    /// @brief 获取 `AssetRegistry`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const AssetRegistry &GetAssetRegistry() const;

  private:
    /// @brief 规范化 `Path`。
    /// @param path 文件、资源或目录路径。
    /// @return 返回对应的文本或路径。
    static std::string NormalizePath(const std::string &path);
    /// @brief 加载 `WithRegisteredLoaders`。
    /// @param path 文件、资源或目录路径。
    /// @return 返回接口计算或查询得到的结果。
    std::shared_ptr<Resource> LoadWithRegisteredLoaders(const std::string &path);

    std::unordered_map<std::string, std::shared_ptr<Resource>> m_cache;
    std::vector<std::shared_ptr<IAssetLoader>> m_loaders;
    mutable std::mutex m_mutex;
    JobSystem *m_jobSystem = nullptr;
    std::filesystem::path m_assetRoot;
    AssetRegistry m_registry;
};
} // namespace MiniEngine
