// 文件：include/MiniEngine/Resource/PakArchive.hpp
// 作用：读取和写入项目 Pak 归档，并提供虚拟文件查找。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：公开声明。

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace MiniEngine
{
struct PakEntry
{
    std::string path;
    std::uint64_t byteSize = 0;
};

class PakArchive
{
  public:
    /// @brief 打包 `Directory`。
    /// @param inputDirectory 相关文件、资源或目录路径。
    /// @param outputFile 相关文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool PackDirectory(const std::filesystem::path &inputDirectory, const std::filesystem::path &outputFile);
    /// @brief 列出归档中包含的全部逻辑路径。
    /// @param archive 目标归档路径或归档对象。
    /// @return 返回查询或处理得到的结果集合。
    static std::vector<PakEntry> List(const std::filesystem::path &archive);
    /// @brief 读取 `File`。
    /// @param archive 目标归档路径或归档对象。
    /// @param path 文件、资源或目录路径。
    /// @return 返回查询或处理得到的结果集合。
    static std::optional<std::vector<std::uint8_t>> ReadFile(const std::filesystem::path &archive, const std::string &path);
};
} // 结束 MiniEngine 命名空间
