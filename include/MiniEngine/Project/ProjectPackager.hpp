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
    std::filesystem::path buildInfo;
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
    /// @brief 根据项目设置生成可分发的运行时目录。
    /// @param options 控制本次操作的选项。
    /// @return 返回接口计算或查询得到的结果。
    static PackageResult Package(const PackageOptions &options);
};
} // namespace MiniEngine
