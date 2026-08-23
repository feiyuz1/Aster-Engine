// 文件：editor/EditorMain.cpp
// 作用：解析编辑器启动参数并创建 EditorApp。
// 约定：编辑器状态与运行时 World 分离；所有写操作都应进入撤销或明确标记为临时预览。
// 类型：实现文件。

#include "EditorApp.hpp"
#include "MiniEngine/Core/CrashReporter.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
std::filesystem::path FindProjectUpwards(std::filesystem::path directory)
{
    std::error_code error;
    directory = std::filesystem::absolute(directory, error);
    if (error)
        return {};

    while (!directory.empty())
    {
        const auto candidate = directory / "TinyEngine.tinyproject";
        if (std::filesystem::is_regular_file(candidate, error) && !error)
            return candidate;
        error.clear();

        const auto parent = directory.parent_path();
        if (parent == directory)
            break;
        directory = parent;
    }
    return {};
}

std::filesystem::path FindDefaultProject(const char *executablePath)
{
    std::error_code error;
    if (const auto fromWorkingDirectory = FindProjectUpwards(std::filesystem::current_path(error)); !fromWorkingDirectory.empty())
        return fromWorkingDirectory;

    error.clear();
    const auto executable = std::filesystem::absolute(executablePath, error);
    return error ? std::filesystem::path{} : FindProjectUpwards(executable.parent_path());
}
} // namespace

int main(int argc, char **argv)
{
    MiniEngine::CrashReporter::Install("TinyEngineEditor");
    MiniEngine::EditorApp editor;
    std::filesystem::path project;
    std::uint32_t maximumFrames = 0;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument.rfind("--frames=", 0) == 0)
            maximumFrames = static_cast<std::uint32_t>(std::strtoul(argument.c_str() + 9, nullptr, 10));
        else if (project.empty())
            project = argument;
    }
    if (project.empty())
        project = FindDefaultProject(argc > 0 ? argv[0] : "");
    if (!editor.Initialize(project))
        return 1;
    editor.Run(maximumFrames);
    editor.Shutdown();
    return 0;
}
