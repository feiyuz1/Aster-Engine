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
