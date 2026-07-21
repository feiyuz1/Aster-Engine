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
    static bool PackDirectory(const std::filesystem::path &inputDirectory, const std::filesystem::path &outputFile);
    static std::vector<PakEntry> List(const std::filesystem::path &archive);
    static std::optional<std::vector<std::uint8_t>> ReadFile(const std::filesystem::path &archive, const std::string &path);
};
} // namespace MiniEngine
