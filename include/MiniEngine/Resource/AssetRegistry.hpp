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
    /// @brief 扫描输入范围并重建索引。
    /// @param root 根目录、根节点或根实体。
    /// @param createMetadata 是否启用对应行为。
    void Scan(const std::filesystem::path &root, bool createMetadata = true);
    /// @brief 查找匹配的对象。
    /// @param path 文件、资源或目录路径。
    /// @return 返回查询或处理得到的结果集合。
    std::optional<AssetInfo> Find(const std::string &path) const;
    /// @brief 查找 `ById`。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 返回查询或处理得到的结果集合。
    std::optional<AssetInfo> FindById(const std::string &id) const;
    /// @brief 获取 `Assets`。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<AssetInfo> GetAssets() const;
    /// @brief 设置 `ImportSetting`。
    /// @param path 文件、资源或目录路径。
    /// @param key 用于查找数据的键。
    /// @param value 要写入、比较或计算的值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SetImportSetting(const std::string &path, const std::string &key, const std::string &value);
    /// @brief 设置 `Dependencies`。
    /// @param path 文件、资源或目录路径。
    /// @param dependencies 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SetDependencies(const std::string &path, std::vector<std::string> dependencies);
    /// @brief 判断是否需要 `Reimport`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool NeedsReimport(const std::string &path) const;
    /// @brief 标记 `Imported`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool MarkImported(const std::string &path);

  private:
    friend class AssetMaintenance;
    void ScanAsset(const std::filesystem::directory_entry &entry, bool createMetadata);
    void UpdateMovedAsset(const std::filesystem::path &source, const std::filesystem::path &destination,
                          const std::vector<std::filesystem::path> &rewrittenAssets);
    /// @brief 保存 `Meta`。
    /// @param info 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SaveMeta(const AssetInfo &info) const;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, AssetInfo> m_assets;
    std::unordered_map<std::string, std::string> m_byId;
};
} // MiniEngine 命名空间
