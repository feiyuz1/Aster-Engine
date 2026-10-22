#include "tinyengine/project/solution_generator.h"
#include <fstream>
#include <sstream>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "tinyengine/platform/platform.h"
#include <Windows.h>
#endif

namespace tinyengine
{
namespace
{
namespace fs = std::filesystem;
#ifdef _WIN32
std::wstring quoteArgument(const std::wstring& value)
{
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t c : value)
    {
        if (c == L'\\')
        {
            ++slashes;
            continue;
        }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
SolutionResult runProcess(const fs::path& root, const fs::path& executable, const std::vector<std::wstring>& arguments);
fs::path findCMake(const fs::path& requested)
{
    if (requested != "cmake")
        return requested;
    wchar_t found[32768]{};
    const DWORD length = SearchPathW(nullptr, L"cmake.exe", nullptr, 32768, found, nullptr);
    if (length > 0 && length < 32768)
        return found;
#ifdef TINYENGINE_CMAKE_EXECUTABLE
    const auto configured = fromUtf8Path(TINYENGINE_CMAKE_EXECUTABLE);
    if (fs::is_regular_file(configured))
        return configured;
#endif
    // VS 安装的 CMake 通常不在图形会话的 PATH 中。vswhere 使用 UTF-8 输出，
    // 这样安装在中文目录中的工具也可以直接找到。
    wchar_t programFiles[32768]{};
    const DWORD programFilesLength = GetEnvironmentVariableW(L"ProgramFiles(x86)", programFiles, 32768);
    if (programFilesLength > 0 && programFilesLength < 32768)
    {
        const auto vswhere = fs::path(programFiles) / "Microsoft Visual Studio/Installer/vswhere.exe";
        if (fs::is_regular_file(vswhere))
        {
            const auto query = runProcess(
                vswhere.parent_path(), vswhere,
                {L"-latest", L"-products", L"*", L"-requires", L"Microsoft.VisualStudio.Component.VC.CMake.Project",
                 L"Microsoft.VisualStudio.Component.VC.Tools.x86.x64", L"-find",
                 L"Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe", L"-utf8"});
            std::istringstream lines(query.output);
            std::string line;
            while (query.success && std::getline(lines, line))
            {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.compare(0, 3, "\xef\xbb\xbf") == 0)
                    line.erase(0, 3);
                if (!line.empty() && fs::is_regular_file(fromUtf8Path(line)))
                    return fromUtf8Path(line);
            }
        }
    }
    return requested;
}
SolutionResult runProcess(const fs::path& root, const fs::path& executable, const std::vector<std::wstring>& arguments)
{
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0))
        return {false, {}, "无法创建编译输出管道。"};
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    PROCESS_INFORMATION process{};
    auto command = quoteArgument(executable.wstring());
    for (const auto& argument : arguments)
        command += L" " + quoteArgument(argument);
    const bool launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                         root.wstring().c_str(), &startup, &process) != 0;
    const DWORD launchError = launched ? ERROR_SUCCESS : GetLastError();
    CloseHandle(writePipe);
    if (!launched)
    {
        CloseHandle(readPipe);
        return {false,
                {},
                "无法启动 CMake。请安装 Visual Studio 2022 的 C++ 桌面开发和 CMake 工具。Windows 错误码：" +
                    std::to_string(launchError)};
    }
    std::string output;
    char buffer[4096];
    DWORD bytes = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytes, nullptr) && bytes > 0)
        output.append(buffer, bytes);
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return {exitCode == 0, {}, output.empty() && exitCode ? "CMake 执行失败。" : output};
}
#endif
} // namespace

SolutionResult SolutionGenerator::generate(const fs::path& projectDirectory, const fs::path& cmakeExecutable,
                                           const fs::path& sdkRoot)
{
    std::error_code error;
    const auto root = fs::absolute(projectDirectory, error).lexically_normal();
    if (error || !fs::is_regular_file(root / "CMakeLists.txt"))
        return {false, {}, "项目目录中没有 CMakeLists.txt。"};
#ifndef _WIN32
    (void)cmakeExecutable;
    (void)sdkRoot;
    return {false, {}, "当前只支持在 Windows 上生成 Visual Studio 2022 工程。"};
#else
    std::vector<std::wstring> arguments{L"-S", root.wstring(),           L"-B", (root / "build").wstring(),
                                        L"-G", L"Visual Studio 17 2022", L"-A", L"x64"};
    if (!sdkRoot.empty())
    {
        const auto sdk = fs::absolute(sdkRoot);
        arguments.push_back(L"-DCMAKE_PREFIX_PATH=" + sdk.wstring());
        arguments.push_back(L"-DTinyEngine_DIR=" + (sdk / "lib/cmake/TinyEngine").wstring());
    }
    auto result = runProcess(root, findCMake(cmakeExecutable), arguments);
    if (!result.success)
        return result;
    for (const auto& entry : fs::directory_iterator(root / "build", error))
        if (!error && entry.is_regular_file() && entry.path().extension() == ".sln")
        {
            result.solutionFile = entry.path();
            return result;
        }
    return {false, {}, result.output + "\nCMake 没有生成 .sln 文件。"};
#endif
}

SolutionResult SolutionGenerator::build(const fs::path& projectDirectory, const fs::path& sdkRoot,
                                        const std::string& configuration, const fs::path& cmakeExecutable)
{
    if (configuration != "Debug" && configuration != "Release")
        return {false, {}, "不支持的编译配置。"};
    try
    {
        auto result = generate(projectDirectory, cmakeExecutable, sdkRoot);
#ifdef _WIN32
        if (result.success)
        {
            const auto root = fs::absolute(projectDirectory).lexically_normal();
            auto built = runProcess(root, findCMake(cmakeExecutable),
                                    {L"--build", (root / "build").wstring(), L"--config",
                                     fromUtf8Path(configuration).wstring(), L"--parallel", L"4"});
            result.success = built.success;
            result.output += "\n" + built.output;
        }
#endif
        std::error_code error;
        fs::create_directories(projectDirectory / "Saved", error);
        std::ofstream log(projectDirectory / "saved/ProjectBuild.log", std::ios::binary);
        log << result.output;
        return result;
    }
    catch (const std::exception& error)
    {
        return {false, {}, error.what()};
    }
}
} // namespace tinyengine
