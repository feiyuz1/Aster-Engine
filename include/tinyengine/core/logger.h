// 文件：include/tinyengine/core/logger.h
// 作用：提供线程安全的分级日志、输出目标和最近日志缓存。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：公开声明。

#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace tinyengine
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
    /// @brief 按指定级别写入日志。
    /// @param level 日志、Mip 或层级值。
    /// @param message 要记录或返回的诊断消息。
    void log(LogLevel level, const std::string& message);
    /// @brief 写入一条调试日志。
    /// @param message 要记录或返回的诊断消息。
    void debug(const std::string& message);
    /// @brief 写入一条信息日志。
    /// @param message 要记录或返回的诊断消息。
    void info(const std::string& message);
    /// @brief 写入一条警告日志。
    /// @param message 要记录或返回的诊断消息。
    void warning(const std::string& message);
    /// @brief 写入一条错误日志。
    /// @param message 要记录或返回的诊断消息。
    void error(const std::string& message);
    /// @brief 设置 `StructuredOutput`。
    /// @param path 文件、资源或目录路径。
    void setStructuredOutput(std::filesystem::path path);

  private:
    /// @brief 将枚举或状态转换为可读文本。
    /// @param level 日志、Mip 或层级值。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    static const char* toString(LogLevel level);
    std::mutex m_mutex;
    std::filesystem::path m_structuredOutput;
};
} // namespace tinyengine
