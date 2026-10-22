#pragma once

#include <filesystem>
#include <string>

namespace tinyengine
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
    static SolutionResult generate(const std::filesystem::path& projectDirectory,
                                   const std::filesystem::path& cmakeExecutable = "cmake",
                                   const std::filesystem::path& sdkRoot = {});
    static SolutionResult build(const std::filesystem::path& projectDirectory, const std::filesystem::path& sdkRoot,
                                const std::string& configuration,
                                const std::filesystem::path& cmakeExecutable = "cmake");
};
} // namespace tinyengine
