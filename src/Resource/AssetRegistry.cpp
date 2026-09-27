// 文件：src/Resource/AssetRegistry.cpp
// 作用：扫描资产元数据，维护 GUID、类型、源文件和依赖索引。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：实现文件。

#include "MiniEngine/Resource/AssetRegistry.hpp"
#include "MiniEngine/Core/Config.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <optional>
#include <random>
#include <sstream>

namespace MiniEngine
{
namespace
{
std::string NewId()
{
    std::random_device device;
    std::mt19937_64 random(device());
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << random() << std::setw(16) << random();
    return out.str();
}
std::uint64_t Stamp(const std::filesystem::file_time_type time)
{
    return static_cast<std::uint64_t>(time.time_since_epoch().count());
}
std::optional<std::string> AssetType(const std::string &extension)
{
    if (extension == ".mesh" || extension == ".gltf" || extension == ".glb")
        return "Mesh";
    if (extension == ".ppm" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
        extension == ".tif" || extension == ".tiff" || extension == ".hdr" || extension == ".dds" || extension == ".ktx2")
        return "Texture";
    if (extension == ".mat")
        return "Material";
    if (extension == ".vert" || extension == ".frag" || extension == ".glsl")
        return "Shader";
    if (extension == ".scene")
        return "Scene";
    if (extension == ".prefab")
        return "Prefab";
    if (extension == ".wav" || extension == ".ogg")
        return "Audio";
    if (extension == ".anim" || extension == ".skanim")
        return "Animation";
    if (extension == ".skeleton")
        return "Skeleton";
    if (extension == ".blendtree")
        return "BlendTree";
    if (extension == ".animgraph")
        return "AnimationGraph";
    if (extension == ".env")
        return "Environment";
    if (extension == ".matgraph")
        return "MaterialGraph";
    if (extension == ".input")
        return "Input";
    if (extension == ".vfx")
        return "VFX";
    if (extension == ".btgraph")
        return "BehaviorTree";
    if (extension == ".ttf" || extension == ".otf" || extension == ".ttc")
        return "Font";
    // 建模源文件和显式通用数据即使不由运行时直接加载，也属于有效资产。
    if (extension == ".fbx" || extension == ".obj" || extension == ".dae" || extension == ".3ds" || extension == ".blend" ||
        extension == ".mtl" || extension == ".bin")
        return "Binary";
    return std::nullopt;
}

std::vector<std::string> InferStructuredDependencies(const std::filesystem::path &path, const std::string &type)
{
    std::vector<std::string> dependencies;
    if (type != "BlendTree" && type != "AnimationGraph")
        return dependencies;
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string token;
    while (input >> token)
    {
        std::string dependency;
        if (type == "BlendTree" && token == "sample")
        {
            float threshold = 0.0f;
            input >> threshold >> std::quoted(dependency);
        }
        else if (type == "AnimationGraph" && token == "state")
        {
            std::string state;
            input >> std::quoted(state) >> std::quoted(dependency);
        }
        if (!dependency.empty() && std::find(dependencies.begin(), dependencies.end(), dependency) == dependencies.end())
            dependencies.push_back(std::move(dependency));
    }
    return dependencies;
}
} // 匿名命名空间

// 扫描时以 .meta 中的 GUID 为权威；移动资产只要携带 meta 文件就能保持引用稳定。
void AssetRegistry::Scan(const std::filesystem::path &root, const bool createMetadata)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_assets.clear();
    m_byId.clear();
    if (!std::filesystem::exists(root))
        return;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
        ScanAsset(entry, createMetadata);
}

// 调用方已持有 m_mutex；仅刷新当前文件，不丢弃索引中的其他记录。
void AssetRegistry::ScanAsset(const std::filesystem::directory_entry &entry, const bool createMetadata)
{
    if (!entry.is_regular_file())
        return;
    auto extension = entry.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == ".meta" || extension == ".tmp" || extension == ".bak")
        return;
    const auto metaPath = entry.path().lexically_normal().generic_string() + ".meta";
    const auto inferredType = AssetType(extension);
    // 未知文件默认不属于资产；已有的 sidecar 元数据仍可显式启用自定义资产格式。
    if (!inferredType && !std::filesystem::is_regular_file(metaPath))
        return;
    AssetInfo info;
    info.path = entry.path().lexically_normal().generic_string();
    info.extension = extension;
    info.type = inferredType.value_or("Binary");
    info.byteSize = entry.file_size();
    info.modified = entry.last_write_time();
    Config meta;
    bool migrateMetadata = false;
    if (meta.Load(metaPath))
    {
        info.id = meta.GetString("asset.id");
        const std::string storedType = meta.GetString("asset.type", info.type);
        // 正式扩展名是类型真相来源；旧版 Binary 不能永久覆盖后来补齐的推断规则。
        if (!inferredType || *inferredType == "Binary")
            info.type = storedType;
        else
        {
            info.type = *inferredType;
            migrateMetadata = storedType != *inferredType;
        }
        try
        {
            info.importedStamp = std::stoull(meta.GetString("asset.importedStamp", "0"));
        }
        catch (...)
        {
            info.importedStamp = 0;
        }
        const int dependencyCount = meta.GetInt("dependencies.count", 0);
        for (int i = 0; i < dependencyCount; ++i)
            info.dependencies.push_back(meta.GetString("dependencies." + std::to_string(i)));
        for (const auto &pair : meta.GetValues())
            if (pair.first.rfind("import.", 0) == 0)
                info.importSettings[pair.first.substr(7)] = pair.second;
    }
    if (info.type == "BlendTree" || info.type == "AnimationGraph")
    {
        auto inferredDependencies = InferStructuredDependencies(info.path, info.type);
        if (info.dependencies != inferredDependencies)
        {
            info.dependencies = std::move(inferredDependencies);
            migrateMetadata = true;
        }
    }
    if (info.id.empty())
        info.id = NewId();
    if (createMetadata && (!std::filesystem::exists(metaPath) || migrateMetadata))
        SaveMeta(info);
    m_byId[info.id] = info.path;
    m_assets[info.path] = info;
}

void AssetRegistry::UpdateMovedAsset(const std::filesystem::path &source, const std::filesystem::path &destination,
                                     const std::vector<std::filesystem::path> &rewrittenAssets)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_assets.find(source.lexically_normal().generic_string());
    if (found != m_assets.end())
    {
        m_byId.erase(found->second.id);
        m_assets.erase(found);
    }
    ScanAsset(std::filesystem::directory_entry(destination), true);
    for (const auto &path : rewrittenAssets)
        ScanAsset(std::filesystem::directory_entry(path), true);
}

std::optional<AssetInfo> AssetRegistry::FindById(const std::string &id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto indexed = m_byId.find(id);
    if (indexed == m_byId.end())
        return std::nullopt;
    const auto found = m_assets.find(indexed->second);
    return found == m_assets.end() ? std::nullopt : std::optional<AssetInfo>(found->second);
}

std::optional<AssetInfo> AssetRegistry::Find(const std::string &path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_assets.find(std::filesystem::path(path).lexically_normal().generic_string());
    return found == m_assets.end() ? std::nullopt : std::optional<AssetInfo>(found->second);
}

std::vector<AssetInfo> AssetRegistry::GetAssets() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AssetInfo> result;
    result.reserve(m_assets.size());
    for (const auto &pair : m_assets)
        result.push_back(pair.second);
    return result;
}

bool AssetRegistry::SaveMeta(const AssetInfo &info) const
{
    Config meta;
    meta.Set("asset.id", info.id);
    meta.Set("asset.schemaVersion", "2");
    meta.Set("asset.type", info.type);
    meta.Set("asset.importedStamp", std::to_string(info.importedStamp));
    meta.Set("dependencies.count", std::to_string(info.dependencies.size()));
    for (std::size_t i = 0; i < info.dependencies.size(); ++i)
        meta.Set("dependencies." + std::to_string(i), info.dependencies[i]);
    for (const auto &setting : info.importSettings)
        meta.Set("import." + setting.first, setting.second);
    return meta.Save(info.path + ".meta");
}

bool AssetRegistry::SetImportSetting(const std::string &path, const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_assets.find(std::filesystem::path(path).lexically_normal().generic_string());
    if (found == m_assets.end())
        return false;
    found->second.importSettings[key] = value;
    return SaveMeta(found->second);
}

bool AssetRegistry::SetDependencies(const std::string &path, std::vector<std::string> dependencies)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_assets.find(std::filesystem::path(path).lexically_normal().generic_string());
    if (found == m_assets.end())
        return false;
    found->second.dependencies = std::move(dependencies);
    return SaveMeta(found->second);
}

bool AssetRegistry::NeedsReimport(const std::string &path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_assets.find(std::filesystem::path(path).lexically_normal().generic_string());
    return found != m_assets.end() && found->second.importedStamp != Stamp(found->second.modified);
}

bool AssetRegistry::MarkImported(const std::string &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_assets.find(std::filesystem::path(path).lexically_normal().generic_string());
    if (found == m_assets.end())
        return false;
    found->second.importedStamp = Stamp(found->second.modified);
    return SaveMeta(found->second);
}
} // 结束 MiniEngine 命名空间
