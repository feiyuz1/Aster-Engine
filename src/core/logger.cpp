// 文件：src/core/logger.cpp
// 作用：提供线程安全的分级日志、输出目标和最近日志缓存。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：实现文件。

#include "tinyengine/core/logger.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace tinyengine
{
namespace
{
std::string currentTimeString()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};

#if defined(_WIN32)
    localtime_s(&localTime, &nowTime);
#else
    localtime_r(&nowTime, &localTime);
#endif

    std::ostringstream oss;
    oss << std::put_time(&localTime, "%H:%M:%S");
    return oss.str();
}
} // namespace

const char* Logger::toString(const LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug:
        return "Debug";
    case LogLevel::Info:
        return "Info";
    case LogLevel::Warning:
        return "Warning";
    case LogLevel::Error:
        return "Error";
    default:
        return "Unknown";
    }
}

void Logger::log(const LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << "[" << currentTimeString() << "]"
              << "[" << toString(level) << "] " << message << std::endl;
    if (!m_structuredOutput.empty())
    {
        if (m_structuredOutput.has_parent_path())
            std::filesystem::create_directories(m_structuredOutput.parent_path());
        std::ofstream output(m_structuredOutput, std::ios::app | std::ios::binary);
        std::string escaped;
        for (const char character : message)
        {
            if (character == '\\' || character == '"')
                escaped += '\\';
            if (character == '\n')
                escaped += "\\n";
            else if (character != '\r')
                escaped += character;
        }
        output << "{\"time\":\"" << currentTimeString() << "\",\"level\":\"" << toString(level) << "\",\"message\":\""
               << escaped << "\"}\n";
    }
}

void Logger::debug(const std::string& message)
{
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message)
{
    log(LogLevel::Info, message);
}

void Logger::warning(const std::string& message)
{
    log(LogLevel::Warning, message);
}

void Logger::error(const std::string& message)
{
    log(LogLevel::Error, message);
}

void Logger::setStructuredOutput(std::filesystem::path path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_structuredOutput = std::move(path);
}
} // namespace tinyengine
