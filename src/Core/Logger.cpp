// 文件：src/Core/Logger.cpp
// 作用：提供线程安全的分级日志、输出目标和最近日志缓存。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：实现文件。

#include "MiniEngine/Core/Logger.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace MiniEngine
{
namespace
{
std::string CurrentTimeString()
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
} // 结束匿名命名空间

const char *Logger::ToString(const LogLevel level)
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

void Logger::Log(const LogLevel level, const std::string &message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << "[" << CurrentTimeString() << "]"
              << "[" << ToString(level) << "] " << message << std::endl;
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
        output << "{\"time\":\"" << CurrentTimeString() << "\",\"level\":\"" << ToString(level) << "\",\"message\":\"" << escaped
               << "\"}\n";
    }
}

void Logger::Debug(const std::string &message)
{
    Log(LogLevel::Debug, message);
}

void Logger::Info(const std::string &message)
{
    Log(LogLevel::Info, message);
}

void Logger::Warning(const std::string &message)
{
    Log(LogLevel::Warning, message);
}

void Logger::Error(const std::string &message)
{
    Log(LogLevel::Error, message);
}

void Logger::SetStructuredOutput(std::filesystem::path path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_structuredOutput = std::move(path);
}
} // 结束 MiniEngine 命名空间
