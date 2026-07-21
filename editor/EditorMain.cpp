// 文件：editor/EditorMain.cpp
// 作用：解析编辑器启动参数并创建 EditorApp。
// 约定：编辑器状态与运行时 World 分离；所有写操作都应进入撤销或明确标记为临时预览。
// 类型：实现文件。

#include "EditorApp.hpp"
#include "MiniEngine/Core/CrashReporter.hpp"

#include <cstdlib>
#include <string>

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
    if (!editor.Initialize(project))
        return 1;
    editor.Run(maximumFrames);
    editor.Shutdown();
    return 0;
}
