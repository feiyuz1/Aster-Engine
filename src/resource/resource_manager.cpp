// 文件：src/resource/resource_manager.cpp
// 作用：统一资源缓存、同步/异步加载、加载器扩展和回收。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：实现文件。

#include "tinyengine/resource/resource_manager.h"

#include "tinyengine/platform/platform.h"
#include "tinyengine/resource/material_definition_resource.h"

#include <filesystem>

namespace tinyengine
{
bool ResourceManager::loadResource(Resource& resource, const std::string& path) const
{
    if (auto* material = dynamic_cast<MaterialResource*>(&resource))
        return material->loadWithResolver(path, [this](const auto& reference, const auto& owner)
                                          { return resolveAssetPath(reference, owner); });
    return resource.load(path);
}
void ResourceManager::initialize(JobSystem* jobSystem, std::filesystem::path assetRoot,
                                 std::filesystem::path logicalAssetRoot)
{
    m_jobSystem = jobSystem;
    m_assetRoot = std::filesystem::absolute(assetRoot).lexically_normal();
    m_logicalAssetRoot = logicalAssetRoot.empty() || logicalAssetRoot.is_absolute()
                             ? m_assetRoot.filename()
                             : logicalAssetRoot.lexically_normal();
    m_registry.scan(m_assetRoot);
}

// 先停止异步入口的使用者，再清理缓存；ResourceManager 本身不拥有外部 JobSystem。
void ResourceManager::shutdown()
{
    if (m_jobSystem)
        m_jobSystem->waitIdle();
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_cache)
        if (pair.second && pair.second->isReady())
            pair.second->Unload();
    m_cache.clear();
    m_loaders.clear();
    m_jobSystem = nullptr;
}

void ResourceManager::registerLoader(std::shared_ptr<IAssetLoader> loader)
{
    if (!loader)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_loaders.push_back(std::move(loader));
}

// 仅回收缓存独占且未处于 Loading 的对象，避免任务线程仍写入已经析构的 Resource。
void ResourceManager::garbageCollect()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_cache.begin(); it != m_cache.end();)
    {
        if (it->second.use_count() <= 1 && it->second->getState() != ResourceState::Loading)
        {
            it->second->Unload();
            it = m_cache.erase(it);
        }
        else
            ++it;
    }
}

bool ResourceManager::unloadUnused(const std::string& path)
{
    const std::string key = normalizePath(path);
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_cache.find(key);
    if (found == m_cache.end())
        return true;
    if (found->second.use_count() > 1 || found->second->getState() == ResourceState::Loading)
        return false;
    found->second->Unload();
    m_cache.erase(found);
    return true;
}

bool ResourceManager::reload(const std::string& path)
{
    const std::string key = normalizePath(path);
    std::shared_ptr<Resource> resource;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_cache.find(key);
        if (found != m_cache.end())
            resource = found->second;
        else
            for (const auto& entry : m_cache)
                if (resolvePath(entry.first) == resolvePath(key))
                {
                    resource = entry.second;
                    break;
                }
        if (!resource)
            return false;
    }
    if (!dynamic_cast<MaterialResource*>(resource.get()) && !dynamic_cast<MaterialDefinitionResource*>(resource.get()))
        resource->Unload();
    return loadResource(*resource, resolvePath(key));
}

std::size_t ResourceManager::getCachedResourceCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cache.size();
}

std::size_t ResourceManager::getCachedByteSize() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::size_t total = 0;
    for (const auto& pair : m_cache)
        if (pair.second && pair.second->isReady())
            total += pair.second->getByteSize();
    return total;
}

AssetRegistry& ResourceManager::getAssetRegistry()
{
    return m_registry;
}
const AssetRegistry& ResourceManager::getAssetRegistry() const
{
    return m_registry;
}

std::string ResourceManager::normalizePath(const std::string& path)
{
    return toUtf8GenericPath(fromUtf8Path(path).lexically_normal());
}

std::string ResourceManager::resolvePath(const std::string& path) const
{
    const std::filesystem::path logical = fromUtf8Path(path);
    if (logical.is_absolute())
        return toUtf8GenericPath(logical.lexically_normal());
    // 已发布的相对引用必须继续从包内读取，不能转成项目目录中的散文件。
    if (PlatformFile::getReadPolicy() == PlatformFile::ReadPolicy::MountedOnly)
    {
        const auto first = logical.begin();
        if (first != logical.end() && *first == "assets")
            return toUtf8GenericPath((m_logicalAssetRoot / logical.lexically_relative("assets")).lexically_normal());
        if (first != logical.end() && *first == m_assetRoot.filename() && m_logicalAssetRoot != m_assetRoot.filename())
            return toUtf8GenericPath(
                (m_logicalAssetRoot / logical.lexically_relative(m_assetRoot.filename())).lexically_normal());
        return toUtf8GenericPath(logical.lexically_normal());
    }
    const auto root = std::filesystem::absolute(m_assetRoot).lexically_normal();
    std::filesystem::path candidate = root / logical;
    const auto first = logical.begin();
    const auto suffix = logical.lexically_relative(m_logicalAssetRoot);
    if (m_logicalAssetRoot != "." && !suffix.empty() && !suffix.is_absolute() && *suffix.begin() != "..")
        candidate = root / suffix;
    if (first != logical.end() && *first == "assets" && root.filename() != "assets")
        candidate = root / logical.lexically_relative("assets");
    if (first != logical.end() && *first == root.filename())
        candidate = root.parent_path() / logical;
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate, error))
        return toUtf8GenericPath(candidate.lexically_normal());
    // 包内资源通过 PlatformFile 的逻辑 URI 查找，不能强制转成不存在的物理绝对路径。
    if (PlatformFile::exists(logical))
        return toUtf8GenericPath(logical.lexically_normal());
    return toUtf8GenericPath(candidate.lexically_normal());
}
std::string ResourceManager::resolveAssetPath(const std::string& path, const std::string& referencingPath) const
{
    if (path.empty() || path == "builtin:PBR")
        return path;
    const std::filesystem::path reference = fromUtf8Path(path);
    if (reference.is_absolute())
        return toUtf8GenericPath(reference.lexically_normal());
    const auto first = reference.begin();
    const auto suffix = reference.lexically_relative(m_logicalAssetRoot);
    const bool configuredPrefix =
        m_logicalAssetRoot != "." && !suffix.empty() && !suffix.is_absolute() && *suffix.begin() != "..";
    const bool rootRelative =
        configuredPrefix || (first != reference.end() && (*first == "assets" || *first == m_assetRoot.filename()));
    if (!referencingPath.empty() && !rootRelative)
        return toUtf8GenericPath((fromUtf8Path(referencingPath).parent_path() / reference).lexically_normal());
    return resolvePath(path);
}

std::shared_ptr<Resource> ResourceManager::LoadWithRegisteredLoaders(const std::string& path)
{
    std::vector<std::shared_ptr<IAssetLoader>> loaders;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        loaders = m_loaders;
    }
    const std::string extension = fromUtf8Path(path).extension().string();
    for (const auto& loader : loaders)
        if (loader && loader->canLoad(extension))
            return loader->load(resolvePath(path));
    return nullptr;
}
} // namespace tinyengine
