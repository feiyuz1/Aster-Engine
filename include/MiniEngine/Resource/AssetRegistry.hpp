#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
    struct AssetInfo
    {
        std::string path;
        std::string extension;
        std::uintmax_t byteSize = 0;
        std::filesystem::file_time_type modified{};
    };

    class AssetRegistry
    {
    public:
        void Scan(const std::filesystem::path& root);
        const AssetInfo* Find(const std::string& path) const;
        std::vector<AssetInfo> GetAssets() const;
    private:
        mutable std::mutex m_mutex;
        std::unordered_map<std::string, AssetInfo> m_assets;
    };
}
