#include "MiniEngine/Resource/AssetCooker.hpp"

#include "MiniEngine/Resource/PakArchive.hpp"

namespace MiniEngine
{
    CookResult AssetCooker::Cook(const std::filesystem::path& assetDirectory, const std::filesystem::path& cookedDirectory, const std::filesystem::path& outputPak)
    {
        CookResult result;
        if (!std::filesystem::is_directory(assetDirectory)) { result.errors.push_back("Asset directory does not exist"); return result; }
        std::error_code error;
        std::filesystem::create_directories(cookedDirectory, error);
        if (error) { result.errors.push_back(error.message()); return result; }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assetDirectory))
        {
            if (!entry.is_regular_file() || entry.path().extension() == ".meta") continue;
            const auto relative = std::filesystem::relative(entry.path(), assetDirectory);
            const auto destination = cookedDirectory / relative;
            std::filesystem::create_directories(destination.parent_path(), error);
            std::filesystem::copy_file(entry.path(), destination, std::filesystem::copy_options::overwrite_existing, error);
            if (error) { result.errors.push_back(relative.generic_string() + ": " + error.message()); error.clear(); }
            else ++result.cookedFiles;
        }
        result.success = result.errors.empty() && PakArchive::PackDirectory(cookedDirectory, outputPak);
        if (!result.success && result.errors.empty()) result.errors.push_back("Failed to create archive");
        return result;
    }
}
