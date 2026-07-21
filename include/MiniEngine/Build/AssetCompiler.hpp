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
    static AssetCompileResult Compile(const std::filesystem::path &source, const std::filesystem::path &destination);
    static bool IsTexture(const std::filesystem::path &path);
    static bool IsMesh(const std::filesystem::path &path);
};
} // namespace MiniEngine
