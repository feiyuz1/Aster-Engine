#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
struct ShaderPreprocessResult
{
    bool success = false;
    std::string source;
    std::string error;
    std::vector<std::filesystem::path> dependencies;
    std::unordered_map<std::string, std::string> defines;
    std::uint64_t hash = 0;
};

class ShaderPipeline
{
  public:
    static ShaderPreprocessResult Preprocess(const std::filesystem::path &path,
                                             const std::unordered_map<std::string, std::string> &defines = {});
    static std::vector<ShaderPreprocessResult> BuildVariants(const std::filesystem::path &path, std::size_t maximumVariants = 64);
    static bool WriteCache(const ShaderPreprocessResult &result, const std::filesystem::path &cacheDirectory);
};
} // namespace MiniEngine
