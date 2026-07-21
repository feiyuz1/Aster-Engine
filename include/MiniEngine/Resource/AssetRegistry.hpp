// 文件：include/MiniEngine/Resource/AssetRegistry.hpp
// 作用：扫描资产元数据，维护 GUID、类型、源文件和依赖索引。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：公开声明。

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
