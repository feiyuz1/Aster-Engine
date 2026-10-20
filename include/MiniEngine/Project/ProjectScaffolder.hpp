#pragma once

#include <filesystem>
#include <string>

namespace MiniEngine
{
struct ProjectCreateRequest
{
    std::string name;
    std::filesystem::path parentDirectory;
    std::filesystem::path sdkRoot;
};

struct ProjectOperationResult
{
    bool success = false;
    std::filesystem::path projectFile;
    std::string error;
};

// 只负责磁盘上的项目结构，不依赖编辑器窗口。
class ProjectScaffolder
{
  public:
    static ProjectOperationResult Create(const ProjectCreateRequest &request);
    static ProjectOperationResult ValidateExisting(const std::filesystem::path &projectFile);
    // 从安装版编辑器的位置向上查找 SDK，不依赖源码目录或当前工作目录。
    static std::filesystem::path FindSDKRoot(std::filesystem::path directory);
};
} // 结束 MiniEngine 命名空间
