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
