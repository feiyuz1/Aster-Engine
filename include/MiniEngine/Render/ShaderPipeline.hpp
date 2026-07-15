#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
enum class ShaderParameterType
{
    Float,
    Vec2,
    Vec3,
    Vec4,
    Texture
};

struct ShaderParameterInfo
{
    std::string name;
    ShaderParameterType type = ShaderParameterType::Float;
    float minimum = 0.0f;
    float maximum = 1.0f;
    std::array<float, 4> defaultValue{};
    bool hasRange = false;
};

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
    static std::vector<ShaderParameterInfo> ReflectParameters(const std::filesystem::path &path);
};
} // namespace MiniEngine
