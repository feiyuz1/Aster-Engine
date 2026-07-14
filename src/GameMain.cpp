#include "MiniEngine/Runtime/Engine.hpp"
#include "MiniEngine/Core/CrashReporter.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    std::filesystem::path FindProject(const std::filesystem::path& executableDirectory)
    {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(executableDirectory, error))
            if (entry.is_regular_file() && entry.path().extension() == ".tinyproject") return entry.path();
        return {};
    }
}

int main(int argc, char** argv)
{
    MiniEngine::CrashReporter::Install("TinyEngineGame");
    const auto launchDirectory = std::filesystem::current_path();
    const auto executableDirectory = std::filesystem::absolute(argv[0]).parent_path().lexically_normal();
    std::filesystem::path projectFile;
    std::uint32_t maxFrames = 0;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument.rfind("--frames=", 0) == 0)
        {
            const auto value = std::strtoul(argument.c_str() + 9, nullptr, 10);
            if (value <= static_cast<unsigned long>(std::numeric_limits<std::uint32_t>::max())) maxFrames = static_cast<std::uint32_t>(value);
        }
        else if (projectFile.empty()) projectFile = std::filesystem::path(argument).is_absolute() ? argument : launchDirectory / argument;
    }
    if (projectFile.empty()) projectFile = FindProject(executableDirectory);
    if (projectFile.empty() || !std::filesystem::is_regular_file(projectFile))
    {
        std::cerr << "Usage: tinyengine_game <project.tinyproject> [--frames=N]\n";
        return 2;
    }

    std::error_code error; std::filesystem::current_path(executableDirectory, error);
    MiniEngine::Engine engine;
    if (!engine.Initialize(std::filesystem::absolute(projectFile).lexically_normal(), true)) return 1;
    engine.Run(maxFrames); engine.Shutdown(); return 0;
}
