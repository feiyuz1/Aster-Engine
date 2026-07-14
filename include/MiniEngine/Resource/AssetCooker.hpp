#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace MiniEngine
{
struct CookResult
{
    bool success = false;
    std::size_t cookedFiles = 0;
    std::size_t skippedFiles = 0;
    std::size_t removedFiles = 0;
    bool cancelled = false;
    std::vector<std::string> errors;
};

struct CookOptions
{
    bool incremental = true;
    std::atomic<bool> *cancel = nullptr;
    std::function<void(float, const std::string &)> progress;
    std::function<bool(const std::filesystem::path &)> include;
};

class AssetCooker
{
  public:
    static CookResult Cook(const std::filesystem::path &assetDirectory, const std::filesystem::path &cookedDirectory,
                           const std::filesystem::path &outputPak, const CookOptions &options = {});
};
} // namespace MiniEngine
