// 文件：include/MiniEngine/Core/CrashReporter.hpp
// 作用：安装崩溃处理器并写出诊断信息和小型转储文件。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：公开声明。

#pragma once

#include <filesystem>
#include <string>

namespace MiniEngine
{
class CrashReporter
{
  public:
    /// @brief 安装运行时处理器。
    /// @param applicationName 用于查找或标识对象的名称。
    /// @param outputDirectory 生成文件的输出目录。
    static void Install(std::string applicationName, std::filesystem::path outputDirectory = "Crashes");
    /// @brief 写入 `Report`。
    /// @param reason 接口所需的输入参数。
    /// @param outputDirectory 生成文件的输出目录。
    /// @return 返回对应的文本或路径。
    static std::filesystem::path WriteReport(const std::string &reason, const std::filesystem::path &outputDirectory = {});
};
} // 结束 MiniEngine 命名空间
