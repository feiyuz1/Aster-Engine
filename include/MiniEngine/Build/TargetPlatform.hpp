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
    /// @brief 获取目标对象或值。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    static const TargetPlatformRegistry &Get();
    /// @brief 查找匹配的对象。
    /// @param name 对象名称或注册键。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const TargetPlatform *Find(const std::string &name) const;
    /// @brief 获取 `Platforms`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<TargetPlatform> &GetPlatforms() const;

  private:
    /// @brief 构造 `TargetPlatformRegistry` 对象。
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
    /// @brief 将源资产烘焙为目标平台格式。
    /// @param options 控制本次操作的选项。
    /// @return 返回接口计算或查询得到的结果。
    static PlatformCookResult Cook(const PlatformCookOptions &options);
    /// @brief 解析 `Configuration`。
    /// @param value 要写入、比较或计算的值。
    /// @return 返回接口计算或查询得到的结果。
    static std::optional<BuildConfiguration> ParseConfiguration(const std::string &value);
    /// @brief 将枚举或状态转换为可读文本。
    /// @param configuration 控制本次操作行为的配置。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    static const char *ToString(BuildConfiguration configuration);
};
} // namespace MiniEngine
