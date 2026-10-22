#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace tinyengine
{
class ResourceManager;

struct AssetReferenceNode
{
    std::string path;
    std::string type;
    int column = 0;
    float row = 0;
    bool missing = false;
    bool builtin = false;
};

struct AssetReferenceEdge
{
    std::size_t from = 0;
    std::size_t to = 0;
};

struct AssetReferenceGraph
{
    std::vector<AssetReferenceNode> nodes;
    std::vector<AssetReferenceEdge> edges;
    bool truncated = false;
};

// Edges point from the referring asset to its dependency. A GUID and a relative
// path to the same resource share one node; traversal is bounded even for cycles.
AssetReferenceGraph buildAssetReferenceGraph(const ResourceManager& resources, const std::string& root, int depth = 1,
                                             std::size_t maxNodes = 128);
} // namespace tinyengine
