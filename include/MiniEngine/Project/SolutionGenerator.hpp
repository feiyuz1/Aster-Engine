#pragma once

#include <filesystem>
#include <string>

namespace MiniEngine
{
struct SolutionResult
{
    bool success = false;
    std::filesystem::path solutionFile;
    std::string output;
};

// 用 CMake 生成 VS 工程；调用者可把同步调用放进工作线程。
class SolutionGenerator
{
  public:
    static SolutionResult Generate(const std::filesystem::path &projectDirectory,
                                   const std::filesystem::path &cmakeExecutable = "cmake");
};
} // 结束 MiniEngine 命名空间
