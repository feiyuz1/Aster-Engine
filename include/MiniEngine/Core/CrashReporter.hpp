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
    static void Install(std::string applicationName, std::filesystem::path outputDirectory = "Crashes");
    static std::filesystem::path WriteReport(const std::string &reason, const std::filesystem::path &outputDirectory = {});
};
} // namespace MiniEngine
