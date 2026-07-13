#include "MiniEngine/Resource/PakArchive.hpp"

#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "Usage:\n  tinyengine_asset_tool pack <input-directory> <output.tpak>\n  tinyengine_asset_tool list <archive.tpak>\n";
        return 1;
    }
    const std::string command = argv[1];
    if (command == "pack" && argc == 4)
    {
        if (!MiniEngine::PakArchive::PackDirectory(argv[2], argv[3])) { std::cerr << "Pack failed\n"; return 2; }
        std::cout << "Packed " << argv[2] << " -> " << argv[3] << '\n'; return 0;
    }
    if (command == "list" && argc == 3)
    {
        const auto entries = MiniEngine::PakArchive::List(argv[2]);
        for (const auto& entry : entries) std::cout << entry.byteSize << "\t" << entry.path << '\n';
        return entries.empty() ? 2 : 0;
    }
    std::cerr << "Invalid arguments\n"; return 1;
}
