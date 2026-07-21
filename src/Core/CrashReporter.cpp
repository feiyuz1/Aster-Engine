// 文件：src/Core/CrashReporter.cpp
// 作用：安装崩溃处理器并写出诊断信息和小型转储文件。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：实现文件。

#include "MiniEngine/Core/CrashReporter.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
// clang-format off: DbgHelp requires Windows types to be declared first.
#include <Windows.h>

#include <DbgHelp.h>
// clang-format on
#endif

namespace MiniEngine
{
namespace
{
std::string application = "TinyEngine";
std::filesystem::path directory = "Crashes";
std::string Timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y%m%d-%H%M%S");
    return output.str();
}
#ifdef _WIN32
LONG WINAPI Unhandled(EXCEPTION_POINTERS *exception)
{
    const auto base = directory / (application + "-" + Timestamp());
    std::filesystem::create_directories(directory);
    HANDLE file = CreateFileW((base.wstring() + L".dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION information{GetCurrentThreadId(), exception, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, exception ? &information : nullptr, nullptr,
                          nullptr);
        CloseHandle(file);
    }
    CrashReporter::WriteReport("Unhandled structured exception", directory);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
} // namespace

void CrashReporter::Install(std::string applicationName, std::filesystem::path outputDirectory)
{
    application = std::move(applicationName);
    directory = std::move(outputDirectory);
#ifdef _WIN32
    SetUnhandledExceptionFilter(Unhandled);
#endif
    std::set_terminate([]() {
        CrashReporter::WriteReport("std::terminate", directory);
        std::abort();
    });
}

std::filesystem::path CrashReporter::WriteReport(const std::string &reason, const std::filesystem::path &outputDirectory)
{
    const auto targetDirectory = outputDirectory.empty() ? directory : outputDirectory;
    const auto path = targetDirectory / (application + "-" + Timestamp() + ".crash.txt");
    std::ostringstream report;
    report << "application=" << application << "\nreason=" << reason << "\ntimestamp=" << Timestamp() << "\n";
#ifdef _WIN32
    report << "process=" << GetCurrentProcessId() << "\nthread=" << GetCurrentThreadId() << "\n";
#endif
    return PlatformFile::WriteAllText(path, report.str()) ? path : std::filesystem::path{};
}
} // namespace MiniEngine
