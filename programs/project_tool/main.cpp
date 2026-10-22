#include "shell_menu.h"
#include "tinyengine/project/sdk_paths.h"
#include "tinyengine/project/solution_generator.h"
#include <fstream>
#include <shellapi.h>
#include <vector>

namespace
{
namespace fs = std::filesystem;
std::wstring wide(const std::string& text)
{
    const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
int report(bool success, const std::wstring& message, bool quiet)
{
    const auto handle = GetStdHandle(success ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    const auto size = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string output(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, output.data(), size, nullptr, nullptr);
    if (handle && handle != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(handle, output.data(), size - 1, &written, nullptr);
    }
    if (!quiet)
        MessageBoxW(nullptr, message.c_str(), L"Aster-Engine 项目工具",
                    MB_OK | (success ? MB_ICONINFORMATION : MB_ICONERROR));
    return success ? 0 : 1;
}
int Run(const std::vector<std::wstring>& args, const fs::path& executable)
{
    bool quiet = false;
    for (const auto& arg : args)
        if (arg == L"--quiet")
            quiet = true;
    std::wstring action;
    fs::path project, sdk, cmake = L"cmake";
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const auto& arg = args[i];
        if (arg == L"--quiet")
            continue;
        if (arg == L"--sdk-root" || arg == L"--cmake")
        {
            if (i + 1 == args.size())
                return report(false, L"参数缺少路径：" + arg, quiet);
            (arg == L"--sdk-root" ? sdk : cmake) = args[++i];
        }
        else if (arg == L"--generate-project" || arg == L"--register" || arg == L"--unregister" || arg == L"--help")
        {
            if (!action.empty())
                return report(false, L"一次只能执行一个操作。", quiet);
            action = arg;
            if (arg == L"--generate-project")
            {
                if (i + 1 == args.size())
                    return report(false, L"请提供 .tinyproject 文件。", quiet);
                project = args[++i];
            }
        }
        else
            return report(false, L"未知参数：" + arg, quiet);
    }
    if (args.empty())
    {
        const auto answer = MessageBoxW(nullptr,
                                        L"注册 .tinyproject 的“生成 Visual Studio "
                                        L"工程”右键菜单？\n\n是：注册或更新\n否：移除本安装的菜单\n取消：退出",
                                        L"Aster-Engine 项目工具", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL)
            return 0;
        action = answer == IDYES ? L"--register" : L"--unregister";
    }
    if (action == L"--help")
        return report(
            true,
            L"TinyEngineProjectTool\n--generate-project <project.tinyproject> [--sdk-root <SDK>] [--cmake "
            L"<cmake.exe>]\n--register\n--unregister\n--quiet  不显示对话框\n\n双击本程序可注册或移除右键菜单。",
            quiet);
    if (action == L"--register" || action == L"--unregister")
    {
        const auto result = action == L"--register"
                                ? TinyEngineTools::registerProjectMenu(HKEY_CURRENT_USER, executable)
                                : TinyEngineTools::unregisterProjectMenu(HKEY_CURRENT_USER, executable);
        return report(result.success, result.message, quiet);
    }
    if (action != L"--generate-project")
        return report(false, L"请指定操作，使用 --help 查看用法。", quiet);
    project = fs::absolute(project).lexically_normal();
    if (_wcsicmp(project.extension().c_str(), L".tinyproject") != 0 || !fs::is_regular_file(project))
        return report(false, L"请选择存在的 .tinyproject 文件：\n" + project.wstring(), quiet);
    if (sdk.empty())
        sdk = tinyengine::findProjectSdk(executable.parent_path());
    tinyengine::SolutionResult result;
    if (sdk.empty() || !fs::is_regular_file(sdk / "lib/cmake/TinyEngine/TinyEngineConfig.cmake"))
        result.output = "未找到完整 SDK。请从安装目录 programs/project_tool 运行工具，或使用 --sdk-root。";
    else
        result = tinyengine::SolutionGenerator::generate(project.parent_path(), cmake, sdk);
    const auto logDirectory = project.parent_path() / "saved/ProjectTool";
    fs::create_directories(logDirectory);
    const auto logPath = logDirectory / "GenerateSolution.log";
    std::ofstream log(logPath, std::ios::binary);
    log << result.output;
    if (!log)
        return report(false, L"无法写入诊断日志：\n" + logPath.wstring(), quiet);
    const auto message =
        result.success ? L"已生成 Visual Studio 工程：\n" + result.solutionFile.wstring()
                       : L"生成失败：\n" +
                             wide(result.output.substr(result.output.size() > 2500 ? result.output.size() - 2500 : 0));
    return report(result.success, message + L"\n\n日志：" + logPath.wstring(), quiet);
}
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int count = 0;
    auto raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!raw)
        return 2;
    std::vector<std::wstring> args(raw + 1, raw + count);
    LocalFree(raw);
    wchar_t executable[32768]{};
    const auto size = GetModuleFileNameW(nullptr, executable, 32768);
    if (size == 0 || size >= 32768)
        return 2;
    try
    {
        return Run(args, executable);
    }
    catch (const std::exception& error)
    {
        bool quiet = false;
        for (const auto& arg : args)
            if (arg == L"--quiet")
                quiet = true;
        return report(false, wide(error.what()), quiet);
    }
}
