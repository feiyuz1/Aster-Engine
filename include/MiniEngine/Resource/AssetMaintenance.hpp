// 文件：include/MiniEngine/Resource/AssetMaintenance.hpp
// 作用：执行资源重命名、迁移、引用更新、缺失依赖检测和修复。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Resource/AssetRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
struct MissingAssetDependency
{
    std::string owner;
    std::string missingPath;
    std::vector<std::string> candidates;
};

class AssetMaintenance
{
  public:
    /// @brief 查找 `MissingDependencies`。
    /// @param registry 要查询或更新的资产注册表。
    /// @return 返回查询或处理得到的结果集合。
    static std::vector<MissingAssetDependency> FindMissingDependencies(const AssetRegistry &registry);
    /// @brief 修复 `Dependency`。
    /// @param registry 要查询或更新的资产注册表。
    /// @param owner 相关实体句柄。
    /// @param missingPath 相关文件、资源或目录路径。
    /// @param replacement 接口所需的输入参数。
    /// @param error 可选的错误信息输出；可为空。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool RepairDependency(AssetRegistry &registry, const std::string &owner, const std::string &missingPath,
                                 const std::string &replacement, std::string *error = nullptr);
    /// @brief 移动 `Asset`。
    /// @param registry 要查询或更新的资产注册表。
    /// @param source 源数据、源资源或源位置。
    /// @param destination 目标路径、对象或输出缓冲区。
    /// @param error 可选的错误信息输出；可为空。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool MoveAsset(AssetRegistry &registry, const std::filesystem::path &source, const std::filesystem::path &destination,
                          std::string *error = nullptr);
    /// @brief 生成 `Thumbnail`。
    /// @param asset 要加载、维护或放置的资产信息。
    /// @param output 接口所需的输入参数。
    /// @param size 数据、内存或目标区域的尺寸。
    /// @param error 可选的错误信息输出；可为空。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool GenerateThumbnail(const AssetInfo &asset, const std::filesystem::path &output, int size = 128,
                                  std::string *error = nullptr);
    /// @brief 分块处理 `Asset`。
    /// @param source 源数据、源资源或源位置。
    /// @param manifest 清单文件路径或清单数据。
    /// @param chunkBytes 目标尺寸、数量、索引或限制值。
    /// @param error 可选的错误信息输出；可为空。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool ChunkAsset(const std::filesystem::path &source, const std::filesystem::path &manifest,
                           std::size_t chunkBytes = 4 * 1024 * 1024, std::string *error = nullptr);
};

class ChunkedAssetStream
{
  public:
    /// @brief 打开资源或服务。
    /// @param manifest 清单文件路径或清单数据。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Open(const std::filesystem::path &manifest);
    /// @brief 读取指定范围的数据。
    /// @param offset 数据、文件或滚动位置偏移。
    /// @param size 数据、内存或目标区域的尺寸。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<std::uint8_t> Read(std::uint64_t offset, std::size_t size) const;
    /// @brief 获取 `Size`。
    /// @return 返回计算得到的数值、数量或索引。
    std::uint64_t GetSize() const;
    /// @brief 获取 `ChunkSize`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetChunkSize() const;

  private:
    std::filesystem::path m_directory;
    std::vector<std::string> m_chunks;
    std::uint64_t m_size = 0;
    std::size_t m_chunkSize = 0;
};
} // namespace MiniEngine
