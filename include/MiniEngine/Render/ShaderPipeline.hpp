// 文件：include/MiniEngine/Render/ShaderPipeline.hpp
// 作用：管理着色器源码、参数反射、编译诊断以及热重载流程。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：公开声明。

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
