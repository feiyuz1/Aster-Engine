// 文件：include/MiniEngine/Build/AssetCompiler.hpp
// 作用：把源资产转换为目标平台可直接加载的运行时格式。
// 约定：烘焙输出必须可重复，并且不能依赖编辑器进程内的临时状态。
// 类型：公开声明。

#pragma once

#include <filesystem>
#include <string>

namespace MiniEngine
{
enum class CompiledAssetType
{
    Passthrough,
    Texture,
    Mesh
};

struct AssetCompileResult
{
    bool success = false;
    CompiledAssetType type = CompiledAssetType::Passthrough;
    std::uintmax_t sourceBytes = 0;
    std::uintmax_t outputBytes = 0;
    std::string error;
};

class PlatformAssetCompiler
{
  public:
    /// @brief 编译当前描述并生成可执行结果。
    /// @param source 源数据、源资源或源位置。
    /// @param destination 目标路径、对象或输出缓冲区。
    /// @return 返回查询或处理得到的结果集合。
    static AssetCompileResult Compile(const std::filesystem::path &source, const std::filesystem::path &destination);
    /// @brief 判断是否满足 `Texture` 条件。
    /// @param path 文件、资源或目录路径。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    static bool IsTexture(const std::filesystem::path &path);
    /// @brief 判断是否满足 `Mesh` 条件。
    /// @param path 文件、资源或目录路径。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    static bool IsMesh(const std::filesystem::path &path);
};
} // 结束 MiniEngine 命名空间
