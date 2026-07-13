#include "MiniEngine/Core/Logger.hpp"

#include <chrono>
#include <ctime>
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
    }

    const char* Logger::ToString(const LogLevel level)
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

    void Logger::Log(const LogLevel level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::cout << "[" << CurrentTimeString() << "]"
                  << "[" << ToString(level) << "] "
                  << message << std::endl;
    }

    void Logger::Debug(const std::string& message)
    {
        Log(LogLevel::Debug, message);
    }

    void Logger::Info(const std::string& message)
    {
        Log(LogLevel::Info, message);
    }

    void Logger::Warning(const std::string& message)
    {
        Log(LogLevel::Warning, message);
    }

    void Logger::Error(const std::string& message)
    {
        Log(LogLevel::Error, message);
    }
}
