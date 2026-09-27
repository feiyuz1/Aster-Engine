#include "MiniEngine/Project/SolutionGenerator.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace MiniEngine
{
namespace
{
#ifdef _WIN32
// Windows 参数中的反斜杠和引号按 CreateProcess 的 argv 规则转义。
std::wstring QuoteArgument(const std::wstring &value)
{
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : value)
    {
        if (character == L'\\')
        {
            ++slashes;
            continue;
        }
        if (character == L'"')
        {
            result.append(slashes * 2 + 1, L'\\');
            result += character;
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result += character;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}
#endif
} // 结束匿名命名空间

SolutionResult SolutionGenerator::Generate(const std::filesystem::path &projectDirectory,
                                           const std::filesystem::path &cmakeExecutable)
{
    namespace fs = std::filesystem;
    std::error_code error;
    const auto root = fs::absolute(projectDirectory, error).lexically_normal();
    if (error || !fs::is_regular_file(root / "CMakeLists.txt"))
        return {false, {}, "项目目录中没有 CMakeLists.txt。"};
#ifndef _WIN32
    (void)cmakeExecutable;
    return {false, {}, "当前只支持在 Windows 上生成 Visual Studio 2022 工程。"};
#else
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0))
        return {false, {}, "无法创建 CMake 输出管道。"};
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    PROCESS_INFORMATION process{};
    const std::wstring command = QuoteArgument(cmakeExecutable.wstring()) + L" -S " + QuoteArgument(root.wstring()) +
                                 L" -B " + QuoteArgument((root / "build").wstring()) +
                                 L" -G \"Visual Studio 17 2022\"";
    std::wstring mutableCommand = command;
    const bool launched = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                         nullptr, root.wstring().c_str(), &startup, &process) != 0;
    const DWORD launchError = launched ? ERROR_SUCCESS : GetLastError();
    CloseHandle(writePipe);
    if (!launched)
    {
        CloseHandle(readPipe);
        return {false, {}, "无法启动 CMake，请检查是否已安装并加入 PATH。Windows 错误码：" +
                               std::to_string(launchError)};
    }

    std::string output;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
        output.append(buffer, bytesRead);
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exitCode != 0)
        return {false, {}, output.empty() ? "CMake 配置失败。" : output};

    for (const auto &entry : fs::directory_iterator(root / "build", error))
        if (!error && entry.is_regular_file() && entry.path().extension() == ".sln")
            return {true, entry.path(), output};
    return {false, {}, output + "\nCMake 没有生成 .sln 文件。"};
#endif
}
} // 结束 MiniEngine 命名空间
