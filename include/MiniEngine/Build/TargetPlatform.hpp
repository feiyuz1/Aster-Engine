// 文件：include/MiniEngine/Build/TargetPlatform.hpp
// 作用：描述目标平台能力、纹理格式和构建输出约束。
// 约定：烘焙输出必须可重复，并且不能依赖编辑器进程内的临时状态。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Resource/AssetCooker.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace MiniEngine
{
enum class BuildConfiguration
{
    Debug,
    Development,
    Shipping
};

struct TargetPlatform
{
    std::string name;
    std::string executableSuffix;
    std::string textureFormat;
    std::string shaderFormat;
    bool caseSensitivePaths = false;
    std::vector<std::string> excludedExtensions;
};

class TargetPlatformRegistry
{
  public:
    static const TargetPlatformRegistry &Get();
    const TargetPlatform *Find(const std::string &name) const;
    const std::vector<TargetPlatform> &GetPlatforms() const;

  private:
    TargetPlatformRegistry();
    std::vector<TargetPlatform> m_platforms;
};

struct PlatformCookOptions
{
    std::filesystem::path projectFile;
    std::filesystem::path outputDirectory;
    std::string targetPlatform = "Windows";
    BuildConfiguration configuration = BuildConfiguration::Development;
    bool incremental = true;
};

struct PlatformCookResult
{
    bool success = false;
    std::filesystem::path outputDirectory;
    std::filesystem::path archive;
    std::filesystem::path manifest;
    CookResult assets;
    std::size_t shaderVariants = 0;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

class TargetPlatformCooker
{
  public:
    static PlatformCookResult Cook(const PlatformCookOptions &options);
    static std::optional<BuildConfiguration> ParseConfiguration(const std::string &value);
    static const char *ToString(BuildConfiguration configuration);
};
} // namespace MiniEngine
