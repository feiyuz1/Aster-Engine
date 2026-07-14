#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
struct AssetInfo
{
    std::string id;
    std::string path;
    std::string extension;
    std::string type;
    std::uintmax_t byteSize = 0;
    std::filesystem::file_time_type modified{};
    std::uint64_t importedStamp = 0;
    std::vector<std::string> dependencies;
    std::unordered_map<std::string, std::string> importSettings;
};

class AssetRegistry
{
  public:
    void Scan(const std::filesystem::path &root, bool createMetadata = true);
    std::optional<AssetInfo> Find(const std::string &path) const;
    std::optional<AssetInfo> FindById(const std::string &id) const;
    std::vector<AssetInfo> GetAssets() const;
    bool SetImportSetting(const std::string &path, const std::string &key, const std::string &value);
    bool SetDependencies(const std::string &path, std::vector<std::string> dependencies);
    bool NeedsReimport(const std::string &path) const;
    bool MarkImported(const std::string &path);

  private:
    bool SaveMeta(const AssetInfo &info) const;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, AssetInfo> m_assets;
    std::unordered_map<std::string, std::string> m_byId;
};
} // namespace MiniEngine
