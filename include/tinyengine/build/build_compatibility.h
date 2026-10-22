// 文件：include/tinyengine/build/build_compatibility.h
// 作用：定义编辑器、打包器与 Game 运行时之间的可验证构建兼容标识。
// 约定：发布打包必须携带并校验 Game 可执行文件旁的 .buildinfo，不接受无来源二进制。

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace tinyengine
{
struct RuntimeBuildInfo
{
    std::string engineVersion;
    std::uint32_t engineAbi = 0;
    std::uint32_t sceneFormat = 0;
    std::string buildId;
    std::string configuration;
    std::string compiler;
};

class BuildCompatibility
{
  public:
    static constexpr std::uint32_t kEngineAbiVersion = 1;
    static constexpr std::uint32_t kSceneFormatVersion = 11;

    /// @brief 返回当前库编译时嵌入的兼容信息。
    static RuntimeBuildInfo current();
    /// @brief 返回给定可执行文件对应的旁路构建信息路径。
    static std::filesystem::path sidecarPath(const std::filesystem::path& executable);
    /// @brief 读取构建信息文件。
    static bool load(const std::filesystem::path& path, RuntimeBuildInfo& info);
    /// @brief 写入构建信息文件，主要供测试夹具和构建工具使用。
    static bool Save(const std::filesystem::path& path, const RuntimeBuildInfo& info);
    /// @brief 比较当前引擎与候选 Game 的 ABI、场景格式、构建 ID、配置和编译器。
    static bool isCompatible(const RuntimeBuildInfo& candidate, std::string* reason = nullptr);
};
} // namespace tinyengine
