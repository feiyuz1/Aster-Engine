#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
    struct CookResult
    {
        bool success = false;
        std::size_t cookedFiles = 0;
        std::vector<std::string> errors;
    };

    class AssetCooker
    {
    public:
        static CookResult Cook(const std::filesystem::path& assetDirectory, const std::filesystem::path& cookedDirectory, const std::filesystem::path& outputPak);
    };
}
