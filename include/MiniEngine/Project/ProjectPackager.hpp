// 文件：include/MiniEngine/Project/ProjectPackager.hpp
// 作用：把烘焙资源、运行时文件和项目配置组装成可分发目录。
// 约定：编辑态和运行态场景相互隔离，运行结束后恢复进入前快照。
// 类型：公开声明。

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
struct PackageOptions
{
    std::filesystem::path projectFile;
    std::filesystem::path outputDirectory;
    std::filesystem::path gameExecutable;
};

struct PackageResult
{
    bool success = false;
    std::filesystem::path outputDirectory;
    std::filesystem::path executable;
    std::filesystem::path projectFile;
    std::filesystem::path archive;
    std::size_t stagedFiles = 0;
    std::uint64_t archiveBytes = 0;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

class ProjectPackager
{
  public:
    static PackageResult Package(const PackageOptions &options);
};
} // namespace MiniEngine
