#pragma once

#include <filesystem>
#include <string>

namespace MiniEngine
{
enum class CompiledAssetType
{
    Passthrough,
    Texture,
    Mesh
};

struct AssetCompileResult
{
    bool success = false;
    CompiledAssetType type = CompiledAssetType::Passthrough;
    std::uintmax_t sourceBytes = 0;
    std::uintmax_t outputBytes = 0;
    std::string error;
};

class PlatformAssetCompiler
{
  public:
    static AssetCompileResult Compile(const std::filesystem::path &source, const std::filesystem::path &destination);
    static bool IsTexture(const std::filesystem::path &path);
    static bool IsMesh(const std::filesystem::path &path);
};
} // namespace MiniEngine
