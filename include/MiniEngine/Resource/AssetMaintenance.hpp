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
    static std::vector<MissingAssetDependency> FindMissingDependencies(const AssetRegistry &registry);
    static bool RepairDependency(AssetRegistry &registry, const std::string &owner, const std::string &missingPath,
                                 const std::string &replacement, std::string *error = nullptr);
    static bool MoveAsset(AssetRegistry &registry, const std::filesystem::path &source, const std::filesystem::path &destination,
                          std::string *error = nullptr);
    static bool GenerateThumbnail(const AssetInfo &asset, const std::filesystem::path &output, int size = 128,
                                  std::string *error = nullptr);
    static bool ChunkAsset(const std::filesystem::path &source, const std::filesystem::path &manifest,
                           std::size_t chunkBytes = 4 * 1024 * 1024, std::string *error = nullptr);
};

class ChunkedAssetStream
{
  public:
    bool Open(const std::filesystem::path &manifest);
    std::vector<std::uint8_t> Read(std::uint64_t offset, std::size_t size) const;
    std::uint64_t GetSize() const;
    std::size_t GetChunkSize() const;

  private:
    std::filesystem::path m_directory;
    std::vector<std::string> m_chunks;
    std::uint64_t m_size = 0;
    std::size_t m_chunkSize = 0;
};
} // namespace MiniEngine
