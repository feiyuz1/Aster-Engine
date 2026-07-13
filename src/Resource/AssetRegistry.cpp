#include "MiniEngine/Resource/AssetRegistry.hpp"

namespace MiniEngine
{
    void AssetRegistry::Scan(const std::filesystem::path& root)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_assets.clear();
        if (!std::filesystem::exists(root)) return;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file()) continue;
            AssetInfo info;
            info.path = entry.path().lexically_normal().generic_string();
            info.extension = entry.path().extension().string();
            info.byteSize = entry.file_size();
            info.modified = entry.last_write_time();
            m_assets[info.path] = info;
        }
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
}
