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
#include <vector>
#ifdef _WIN32
#define NOMINMAX
// DbgHelp 依赖 Windows 类型，必须后包含。
// clang-format off
#include <Windows.h>

#include <DbgHelp.h>
// clang-format on
#endif

namespace MiniEngine
{
namespace
{
std::string application = "Aster-Engine";
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
std::string DescribeException(EXCEPTION_POINTERS *exception)
{
    if (!exception || !exception->ExceptionRecord || !exception->ContextRecord)
        return "Unhandled structured exception";

    std::ostringstream description;
    description << "Unhandled structured exception code=0x" << std::hex << std::uppercase
                << exception->ExceptionRecord->ExceptionCode << " address=0x"
                << reinterpret_cast<std::uintptr_t>(exception->ExceptionRecord->ExceptionAddress);

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(process, nullptr, TRUE))
        return description.str();

    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame{};
#if defined(_M_X64)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_IX86)
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = context.Eip;
    frame.AddrFrame.Offset = context.Ebp;
    frame.AddrStack.Offset = context.Esp;
#else
    SymCleanup(process);
    return description.str();
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    std::vector<unsigned char> symbolStorage(sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
    auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbolStorage.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    for (unsigned int depth = 0; depth < 32 && frame.AddrPC.Offset != 0; ++depth)
    {
        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        description << "\nstack[" << std::dec << depth << "]=0x" << std::hex << frame.AddrPC.Offset;
        if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
            description << ' ' << symbol->Name << "+0x" << displacement;
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line) && line.FileName)
            description << " (" << line.FileName << ':' << std::dec << line.LineNumber << ')';
        if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &context, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr))
            break;
    }
    SymCleanup(process);
    return description.str();
}

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
    CrashReporter::WriteReport(DescribeException(exception), directory);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
} // 结束匿名命名空间

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
} // 结束 MiniEngine 命名空间
