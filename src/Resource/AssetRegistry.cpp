#include "MiniEngine/Resource/AssetRegistry.hpp"
#include "MiniEngine/Core/Config.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

namespace MiniEngine
{
    namespace
    {
        std::string NewId()
        {
            std::random_device device; std::mt19937_64 random(device());
            std::ostringstream out; out << std::hex << std::setfill('0') << std::setw(16) << random() << std::setw(16) << random();
            return out.str();
        }
        std::uint64_t Stamp(const std::filesystem::file_time_type time)
        {
            return static_cast<std::uint64_t>(time.time_since_epoch().count());
        }
        std::string AssetType(const std::string& extension)
        {
            if (extension == ".mesh" || extension == ".gltf" || extension == ".glb") return "Mesh";
            if (extension == ".ppm" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".hdr") return "Texture";
            if (extension == ".mat") return "Material";
            if (extension == ".vert" || extension == ".frag" || extension == ".glsl") return "Shader";
            if (extension == ".scene") return "Scene";
            if (extension == ".prefab") return "Prefab";
            if (extension == ".wav" || extension == ".ogg") return "Audio";
            if (extension == ".anim") return "Animation";
            if (extension == ".input") return "Input";
            if (extension == ".ttf" || extension == ".otf") return "Font";
            return "Binary";
        }
    }

    void AssetRegistry::Scan(const std::filesystem::path& root, const bool createMetadata)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_assets.clear();
        m_byId.clear();
        if (!std::filesystem::exists(root)) return;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".meta") continue;
            AssetInfo info;
            info.path = entry.path().lexically_normal().generic_string();
            info.extension = entry.path().extension().string();
            std::transform(info.extension.begin(), info.extension.end(), info.extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            info.type = AssetType(info.extension);
            info.byteSize = entry.file_size();
            info.modified = entry.last_write_time();
            Config meta;
            const auto metaPath = info.path + ".meta";
            if (meta.Load(metaPath))
            {
                info.id = meta.GetString("asset.id");
                info.type = meta.GetString("asset.type", info.type);
                try { info.importedStamp = std::stoull(meta.GetString("asset.importedStamp", "0")); } catch (...) { info.importedStamp = 0; }
                const int dependencyCount = meta.GetInt("dependencies.count", 0);
                for (int i = 0; i < dependencyCount; ++i) info.dependencies.push_back(meta.GetString("dependencies." + std::to_string(i)));
                for (const auto& pair : meta.GetValues())
                    if (pair.first.rfind("import.", 0) == 0) info.importSettings[pair.first.substr(7)] = pair.second;
            }
            if (info.id.empty()) info.id = NewId();
            if (createMetadata && !std::filesystem::exists(metaPath)) SaveMeta(info);
            m_byId[info.id] = info.path;
            m_assets[info.path] = info;
        }
    }

    std::optional<AssetInfo> AssetRegistry::FindById(const std::string& id) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto indexed = m_byId.find(id);
        if (indexed == m_byId.end()) return std::nullopt;
        const auto found = m_assets.find(indexed->second);
        return found == m_assets.end() ? std::nullopt : std::optional<AssetInfo>(found->second);
    }

    std::optional<AssetInfo> AssetRegistry::Find(const std::string& path) const
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
        for (const auto& pair : m_assets) result.push_back(pair.second);
        return result;
    }

    bool AssetRegistry::SaveMeta(const AssetInfo& info) const
    {
        Config meta;
        meta.Set("asset.id", info.id); meta.Set("asset.type", info.type); meta.Set("asset.importedStamp", std::to_string(info.importedStamp));
        meta.Set("dependencies.count", std::to_string(info.dependencies.size()));
        for (std::size_t i = 0; i < info.dependencies.size(); ++i) meta.Set("dependencies." + std::to_string(i), info.dependencies[i]);
        for (const auto& setting : info.importSettings) meta.Set("import." + setting.first, setting.second);
        return meta.Save(info.path + ".meta");
    }

    bool AssetRegistry::SetImportSetting(const std::string& path, const std::string& key, const std::string& value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_assets.find(std::filesystem::path(path).lexically_normal().generic_string());
        if (found == m_assets.end()) return false;
        found->second.importSettings[key] = value; return SaveMeta(found->second);
    }

    bool AssetRegistry::SetDependencies(const std::string& path, std::vector<std::string> dependencies)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_assets.find(std::filesystem::path(path).lexically_normal().generic_string());
        if (found == m_assets.end()) return false;
        found->second.dependencies = std::move(dependencies); return SaveMeta(found->second);
    }

    bool AssetRegistry::NeedsReimport(const std::string& path) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_assets.find(std::filesystem::path(path).lexically_normal().generic_string());
        return found != m_assets.end() && found->second.importedStamp != Stamp(found->second.modified);
    }

    bool AssetRegistry::MarkImported(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto found = m_assets.find(std::filesystem::path(path).lexically_normal().generic_string());
        if (found == m_assets.end()) return false;
        found->second.importedStamp = Stamp(found->second.modified); return SaveMeta(found->second);
    }
}
