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
    /// @brief 展开着色器包含文件和宏定义。
    /// @param path 文件、资源或目录路径。
    /// @param defines 接口所需的输入参数。
    /// @return 返回接口计算或查询得到的结果。
    static ShaderPreprocessResult Preprocess(const std::filesystem::path &path,
                                             const std::unordered_map<std::string, std::string> &defines = {});
    /// @brief 构建 `Variants`。
    /// @param path 文件、资源或目录路径。
    /// @param maximumVariants 目标尺寸、数量、索引或限制值。
    /// @return 返回查询或处理得到的结果集合。
    static std::vector<ShaderPreprocessResult> BuildVariants(const std::filesystem::path &path, std::size_t maximumVariants = 64);
    /// @brief 写入 `Cache`。
    /// @param result 接口所需的输入参数。
    /// @param cacheDirectory 相关文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool WriteCache(const ShaderPreprocessResult &result, const std::filesystem::path &cacheDirectory);
    /// @brief 反射 `Parameters`。
    /// @param path 文件、资源或目录路径。
    /// @return 返回查询或处理得到的结果集合。
    static std::vector<ShaderParameterInfo> ReflectParameters(const std::filesystem::path &path);
};
} // 结束 MiniEngine 命名空间
