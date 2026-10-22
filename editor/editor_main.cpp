// 文件：editor/editor_main.cpp
// 作用：解析编辑器启动参数并创建 EditorApp。
// 约定：编辑器状态与运行时 World 分离；所有写操作都应进入撤销或明确标记为临时预览。
// 类型：实现文件。

#include "editor_app.h"
#include "tinyengine/core/crash_reporter.h"
#include "tinyengine/project/project_scaffolder.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    tinyengine::CrashReporter::install("Aster-Engine-Editor");
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
    do
    {
        if (!project.empty())
        {
            const auto validation = tinyengine::ProjectScaffolder::validateExisting(project);
            if (!validation.success)
            {
                std::cerr << validation.error << '\n';
                return 1;
            }
            project = validation.projectFile;
        }
        tinyengine::EditorApp editor;
        if (!editor.initialize(project))
            return 1;
        editor.Run(maximumFrames);
        project = editor.takeRequestedProject();
        editor.shutdown();
    } while (!project.empty());
    return 0;
}
