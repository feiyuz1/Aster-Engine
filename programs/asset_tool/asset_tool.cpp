// 文件：programs/asset_tool/asset_tool.cpp
// 作用：提供资产导入、烘焙和维护的命令行入口。
// 约定：命令行工具以非交互方式运行，失败时返回非零退出码并输出可定位的错误。
// 类型：实现文件。

#include "tinyengine/render/environment_baker.h"
#include "tinyengine/resource/asset_cooker.h"
#include "tinyengine/resource/asset_maintenance.h"
#include "tinyengine/resource/asset_registry.h"
#include "tinyengine/resource/pak_archive.h"
#ifdef TINYENGINE_HAS_ASSIMP
#include "tinyengine/import/assimp_importer.h"
#endif

#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "Usage:\n  tinyengine_asset_tool import <source-model> <output-directory> [resource-prefix]\n  "
                     "tinyengine_asset_tool "
                     "bake-env <source-hdr> <output-directory>\n  tinyengine_asset_tool scan <asset-directory>\n  "
                     "tinyengine_asset_tool "
                     "cook <asset-directory> <cooked-directory> <output.tpak>\n  "
                     "tinyengine_asset_tool pack <input-directory> <output.tpak>\n  tinyengine_asset_tool list "
                     "<archive.tpak>\n  "
                     "tinyengine_asset_tool thumbnail <asset> <output.ppm>\n  tinyengine_asset_tool chunk <asset> "
                     "<output.chunkmanifest> "
                     "[chunk-bytes]\n  tinyengine_asset_tool missing <asset-directory>\n";
        return 1;
    }
    const std::string command = argv[1];
    if (command == "bake-env" && argc == 4)
    {
        const auto result = tinyengine::EnvironmentBaker::bake(argv[2], argv[3]);
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
        tinyengine::ModelImportOptions options;
        if (argc == 5)
            options.resourcePrefix = argv[4];
        const auto result = tinyengine::AssimpImporter::importModel(argv[2], argv[3], options);
        for (const auto& warning : result.warnings)
            std::cerr << "Warning: " << warning << '\n';
        if (!result.success)
        {
            std::cerr << "Import failed: " << result.error << '\n';
            return 2;
        }
        for (const auto& asset : result.assets)
            std::cout << asset.type << "\t" << asset.path.generic_string() << '\n';
        return 0;
    }
#endif
    if (command == "scan" && argc == 3)
    {
        tinyengine::AssetRegistry registry;
        registry.scan(argv[2]);
        for (const auto& asset : registry.GetAssets())
            std::cout << asset.id << "\t" << asset.type << "\t" << asset.path << '\n';
        return 0;
    }
    if (command == "missing" && argc == 3)
    {
        tinyengine::AssetRegistry registry;
        registry.scan(argv[2]);
        const auto issues = tinyengine::AssetMaintenance::FindMissingDependencies(registry);
        for (const auto& issue : issues)
        {
            std::cout << issue.owner << " -> " << issue.missingPath;
            for (const auto& candidate : issue.candidates)
                std::cout << "\t" << candidate;
            std::cout << '\n';
        }
        return issues.empty() ? 0 : 3;
    }
    if (command == "thumbnail" && argc == 4)
    {
        tinyengine::AssetRegistry registry;
        registry.scan(std::filesystem::path(argv[2]).parent_path());
        const auto info = registry.Find(argv[2]);
        std::string error;
        if (!info || !tinyengine::AssetMaintenance::generateThumbnail(*info, argv[3], 128, &error))
        {
            std::cerr << (error.empty() ? "Asset not found" : error) << '\n';
            return 2;
        }
        return 0;
    }
    if (command == "chunk" && (argc == 4 || argc == 5))
    {
        const std::size_t chunkBytes = argc == 5 ? static_cast<std::size_t>(std::stoull(argv[4])) : 4 * 1024 * 1024;
        std::string error;
        if (!tinyengine::AssetMaintenance::chunkAsset(argv[2], argv[3], chunkBytes, &error))
        {
            std::cerr << error << '\n';
            return 2;
        }
        return 0;
    }
    if (command == "cook" && argc == 5)
    {
        const auto result = tinyengine::AssetCooker::cook(argv[2], argv[3], argv[4]);
        for (const auto& error : result.errors)
            std::cerr << error << '\n';
        if (!result.success)
            return 2;
        std::cout << "Cooked " << result.cookedFiles << " assets -> " << argv[4] << '\n';
        return 0;
    }
    if (command == "pack" && argc == 4)
    {
        if (!tinyengine::PakArchive::packDirectory(argv[2], argv[3]))
        {
            std::cerr << "Pack failed\n";
            return 2;
        }
        std::cout << "Packed " << argv[2] << " -> " << argv[3] << '\n';
        return 0;
    }
    if (command == "list" && argc == 3)
    {
        const auto entries = tinyengine::PakArchive::List(argv[2]);
        for (const auto& entry : entries)
            std::cout << entry.byteSize << "\t" << entry.path << '\n';
        return entries.empty() ? 2 : 0;
    }
    std::cerr << "Invalid arguments\n";
    return 1;
}
