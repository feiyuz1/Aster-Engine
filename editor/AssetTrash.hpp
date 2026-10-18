#pragma once

#include <string>
#include <vector>

namespace MiniEngine
{
class ResourceManager;

struct AssetTrashReference
{
    std::string target;
    std::string owner;
};

// Editor-only recoverable deletion. All paths and manifest strings use UTF-8.
class AssetTrash
{
  public:
    static std::vector<AssetTrashReference> FindReferences(ResourceManager &resources, const std::vector<std::string> &selectedPaths);
    static bool MoveToTrash(ResourceManager &resources, const std::string &projectRoot, const std::vector<std::string> &paths,
                            std::string &manifestPath, std::string &error);
    static bool Restore(ResourceManager &resources, const std::string &manifestPath, std::string &error);
};
} // namespace MiniEngine
