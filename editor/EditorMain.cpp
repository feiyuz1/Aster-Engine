#include "EditorApp.hpp"
#include "MiniEngine/Core/CrashReporter.hpp"

int main(int argc, char **argv)
{
    MiniEngine::CrashReporter::Install("TinyEngineEditor");
    MiniEngine::EditorApp editor;
    const std::filesystem::path project = argc > 1 ? argv[1] : std::filesystem::path{};
    if (!editor.Initialize(project))
        return 1;
    editor.Run();
    editor.Shutdown();
    return 0;
}
