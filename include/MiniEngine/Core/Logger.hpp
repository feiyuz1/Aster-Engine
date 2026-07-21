// 文件：include/MiniEngine/Core/Logger.hpp
// 作用：提供线程安全的分级日志、输出目标和最近日志缓存。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：公开声明。

#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace MiniEngine
{
enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error
};

class Logger
{
  public:
    void Log(LogLevel level, const std::string &message);
    void Debug(const std::string &message);
    void Info(const std::string &message);
    void Warning(const std::string &message);
    void Error(const std::string &message);
    void SetStructuredOutput(std::filesystem::path path);

  private:
    static const char *ToString(LogLevel level);
    std::mutex m_mutex;
    std::filesystem::path m_structuredOutput;
};
} // namespace MiniEngine
