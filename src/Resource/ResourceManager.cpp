// 文件：src/Resource/ResourceManager.cpp
// 作用：统一资源缓存、同步/异步加载、加载器扩展和回收。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：实现文件。

#include "MiniEngine/Resource/ResourceManager.hpp"

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/MaterialDefinitionResource.hpp"

#include <filesystem>

namespace MiniEngine
{
bool ResourceManager::LoadResource(Resource &resource, const std::string &path) const
{
    if (auto *material = dynamic_cast<MaterialResource *>(&resource))
        return material->LoadWithResolver(path, [this](const auto &reference, const auto &owner) { return ResolveAssetPath(reference, owner); });
    return resource.Load(path);
}
void ResourceManager::Initialize(JobSystem *jobSystem, std::filesystem::path assetRoot)
{
    m_jobSystem = jobSystem;
    m_assetRoot = std::filesystem::absolute(assetRoot).lexically_normal();
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
        if (found != m_cache.end()) resource=found->second;
        else for (const auto &entry:m_cache) if (ResolvePath(entry.first)==ResolvePath(key)) { resource=entry.second; break; }
        if (!resource) return false;
    }
    if (!dynamic_cast<MaterialResource *>(resource.get()) && !dynamic_cast<MaterialDefinitionResource *>(resource.get())) resource->Unload();
    return LoadResource(*resource,ResolvePath(key));
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
    return std::filesystem::u8path(path).lexically_normal().generic_u8string();
}

std::string ResourceManager::ResolvePath(const std::string &path) const
{
    const std::filesystem::path logical = std::filesystem::u8path(path);
    if (logical.is_absolute())
        return logical.lexically_normal().generic_u8string();
    // 已发布的相对引用必须继续从包内读取，不能转成项目目录中的散文件。
    if (PlatformFile::GetReadPolicy() == PlatformFile::ReadPolicy::MountedOnly)
    {
        const auto first=logical.begin();
        if (first!=logical.end() && *first=="assets")
            return (m_assetRoot.filename()/logical.lexically_relative("assets")).lexically_normal().generic_u8string();
        return logical.lexically_normal().generic_u8string();
    }
    const auto root = std::filesystem::absolute(m_assetRoot).lexically_normal();
    std::filesystem::path candidate = root / logical;
    const auto first = logical.begin();
    if (first!=logical.end() && *first=="assets" && root.filename()!="assets") candidate=root/logical.lexically_relative("assets");
    if (first != logical.end() && *first == root.filename())
        candidate = root.parent_path() / logical;
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate, error))
        return candidate.lexically_normal().generic_u8string();
    // 包内资源通过 PlatformFile 的逻辑 URI 查找，不能强制转成不存在的物理绝对路径。
    if (PlatformFile::Exists(logical))
        return logical.lexically_normal().generic_u8string();
    return candidate.lexically_normal().generic_u8string();
}
std::string ResourceManager::ResolveAssetPath(const std::string &path, const std::string &referencingPath) const
{
    if (path.empty() || path == "builtin:PBR") return path;
    const std::filesystem::path reference = std::filesystem::u8path(path);
    if (reference.is_absolute()) return reference.lexically_normal().generic_u8string();
    const auto first = reference.begin();
    const bool rootRelative = first != reference.end() && (*first == "assets" || *first == m_assetRoot.filename());
    if (!referencingPath.empty() && !rootRelative)
        return (std::filesystem::u8path(referencingPath).parent_path() / reference).lexically_normal().generic_u8string();
    return ResolvePath(path);
}

std::shared_ptr<Resource> ResourceManager::LoadWithRegisteredLoaders(const std::string &path)
{
    std::vector<std::shared_ptr<IAssetLoader>> loaders;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        loaders = m_loaders;
    }
    const std::string extension = std::filesystem::u8path(path).extension().string();
    for (const auto &loader : loaders)
        if (loader && loader->CanLoad(extension))
            return loader->Load(ResolvePath(path));
    return nullptr;
}
} // 结束 MiniEngine 命名空间
