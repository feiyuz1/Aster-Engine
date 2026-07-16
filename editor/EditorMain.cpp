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
