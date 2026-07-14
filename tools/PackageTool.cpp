#include "MiniEngine/Project/ProjectPackager.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4)
    {
        std::cout << "Usage: tinyengine_package_tool <project.tinyproject> <output-directory> [game-executable]\n";
        return 1;
    }
    std::filesystem::path gameExecutable = argc == 4 ? std::filesystem::path(argv[3]) : std::filesystem::absolute(argv[0]).parent_path() /
#ifdef _WIN32
        "tinyengine_game.exe";
#else
        "tinyengine_game";
#endif
    const auto result = MiniEngine::ProjectPackager::Package({argv[1], argv[2], gameExecutable});
    for (const auto& warning : result.warnings) std::cerr << "Warning: " << warning << '\n';
    for (const auto& error : result.errors) std::cerr << "Error: " << error << '\n';
    if (!result.success) return 2;
    std::cout << "Packaged " << result.stagedFiles << " files\n"
              << "Executable: " << result.executable.generic_string() << '\n'
              << "Archive: " << result.archive.generic_string() << " (" << result.archiveBytes << " bytes)\n";
    return 0;
}
