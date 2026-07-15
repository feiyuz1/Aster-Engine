#pragma once

#include <filesystem>
#include <string>

namespace MiniEngine
{
struct EnvironmentBakeResult
{
    bool success = false;
    std::filesystem::path environmentPath;
    std::filesystem::path irradiancePath;
    std::filesystem::path prefilterPath;
    std::filesystem::path brdfLutPath;
    std::string error;
};

class EnvironmentBaker
{
  public:
    static EnvironmentBakeResult Bake(const std::filesystem::path &source, const std::filesystem::path &outputDirectory,
                                      int sampleCount = 64);
};
} // namespace MiniEngine
