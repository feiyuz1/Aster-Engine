#include "MiniEngine/Render/EnvironmentBaker.hpp"
#include "MiniEngine/Resource/AssetCooker.hpp"
#include "MiniEngine/Resource/AssetRegistry.hpp"
#include "MiniEngine/Resource/PakArchive.hpp"
#ifdef TINYENGINE_HAS_ASSIMP
#include "MiniEngine/Import/AssimpImporter.hpp"
#endif

#include <iostream>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cout << "Usage:\n  tinyengine_asset_tool import <source-model> <output-directory> [resource-prefix]\n  tinyengine_asset_tool "
                     "bake-env <source-hdr> <output-directory>\n  tinyengine_asset_tool scan <asset-directory>\n  tinyengine_asset_tool "
                     "cook <asset-directory> <cooked-directory> <output.tpak>\n  "
                     "tinyengine_asset_tool pack <input-directory> <output.tpak>\n  tinyengine_asset_tool list <archive.tpak>\n";
        return 1;
    }
    const std::string command = argv[1];
    if (command == "bake-env" && argc == 4)
    {
        const auto result = MiniEngine::EnvironmentBaker::Bake(argv[2], argv[3]);
        if (!result.success)
        {
            std::cerr << "Environment bake failed: " << result.error << '\n';
            return 2;
        }
        std::cout << result.environmentPath.generic_string() << '\n';
        return 0;
    }
#ifdef TINYENGINE_HAS_ASSIMP
    if (command == "import" && (argc == 4 || argc == 5))
    {
        MiniEngine::ModelImportOptions options;
        if (argc == 5)
            options.resourcePrefix = argv[4];
        const auto result = MiniEngine::AssimpImporter::ImportModel(argv[2], argv[3], options);
        for (const auto &warning : result.warnings)
            std::cerr << "Warning: " << warning << '\n';
        if (!result.success)
        {
            std::cerr << "Import failed: " << result.error << '\n';
            return 2;
        }
        for (const auto &asset : result.assets)
            std::cout << asset.type << "\t" << asset.path.generic_string() << '\n';
        return 0;
    }
#endif
    if (command == "scan" && argc == 3)
    {
        MiniEngine::AssetRegistry registry;
        registry.Scan(argv[2]);
        for (const auto &asset : registry.GetAssets())
            std::cout << asset.id << "\t" << asset.type << "\t" << asset.path << '\n';
        return 0;
    }
    if (command == "cook" && argc == 5)
    {
        const auto result = MiniEngine::AssetCooker::Cook(argv[2], argv[3], argv[4]);
        for (const auto &error : result.errors)
            std::cerr << error << '\n';
        if (!result.success)
            return 2;
        std::cout << "Cooked " << result.cookedFiles << " assets -> " << argv[4] << '\n';
        return 0;
    }
    if (command == "pack" && argc == 4)
    {
        if (!MiniEngine::PakArchive::PackDirectory(argv[2], argv[3]))
        {
            std::cerr << "Pack failed\n";
            return 2;
        }
        std::cout << "Packed " << argv[2] << " -> " << argv[3] << '\n';
        return 0;
    }
    if (command == "list" && argc == 3)
    {
        const auto entries = MiniEngine::PakArchive::List(argv[2]);
        for (const auto &entry : entries)
            std::cout << entry.byteSize << "\t" << entry.path << '\n';
        return entries.empty() ? 2 : 0;
    }
    std::cerr << "Invalid arguments\n";
    return 1;
}
