#include "AssetReferenceGraph.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"
#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <set>

namespace MiniEngine
{
namespace
{
std::string ReferenceKey(const std::string &path)
{
    if (path.rfind("builtin:", 0) == 0)
        return path;
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(std::filesystem::u8path(path), error);
    auto key = (error ? std::filesystem::u8path(path).lexically_normal() : canonical).generic_u8string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}
} // namespace

AssetReferenceGraph BuildAssetReferenceGraph(const ResourceManager &resources, const std::string &root, int depth, std::size_t maxNodes)
{
    AssetReferenceGraph graph;
    if (root.empty())
        return graph;
    const auto &registry = resources.GetAssetRegistry();
    const auto resolve = [&](const std::string &reference, const std::string &owner) {
        if (reference.rfind("builtin:", 0) == 0)
            return reference;
        if (const auto asset = registry.FindById(reference))
            return asset->path;
        return resources.ResolveAssetPath(reference, owner);
    };
    std::map<std::string, AssetReferenceNode> information;
    std::map<std::string, std::set<std::string>> dependencies, referencers;
    const auto describe = [&](const std::string &path, const std::string &type = std::string{}) {
        const auto key = ReferenceKey(path);
        auto &node = information[key];
        node.path = path;
        node.builtin = path.rfind("builtin:", 0) == 0;
        if (!type.empty())
            node.type = type;
        else if (node.type.empty())
            node.type = node.builtin ? "Built-in" : "Asset";
        node.missing = !node.builtin && !PlatformFile::Exists(std::filesystem::u8path(path));
        return key;
    };
    for (const auto &asset : registry.GetAssets())
    {
        const auto owner = describe(asset.path, asset.type);
        for (const auto &reference : asset.dependencies)
        {
            if (reference.empty())
                continue;
            const auto target = describe(resolve(reference, asset.path));
            dependencies[owner].insert(target);
            referencers[target].insert(owner);
        }
    }
    const auto rootKey = describe(resolve(root, {}));
    std::map<std::string, std::size_t> included{{rootKey, 0}};
    graph.nodes.push_back(information.at(rootKey));
    maxNodes = std::max<std::size_t>(1, maxNodes);
    depth = std::clamp(depth, 0, 8);
    // Traverse both directions breadth first; cycles and node count are bounded.
    struct Visit
    {
        std::string key;
        int direction;
        int distance;
    };
    std::deque<Visit> queue{{rootKey, -1, 0}, {rootKey, 1, 0}};
    std::set<std::pair<std::string, int>> visited{{rootKey, -1}, {rootKey, 1}};
    while (!queue.empty())
    {
        const auto visit = queue.front();
        queue.pop_front();
        if (visit.distance >= depth)
            continue;
        const auto &links = visit.direction < 0 ? referencers[visit.key] : dependencies[visit.key];
        for (const auto &key : links)
        {
            auto node = included.find(key);
            if (node == included.end())
            {
                if (graph.nodes.size() >= maxNodes)
                {
                    graph.truncated = true;
                    continue;
                }
                node = included.emplace(key, graph.nodes.size()).first;
                graph.nodes.push_back(information.at(key));
                graph.nodes.back().column = visit.direction * (visit.distance + 1);
            }
            if (visited.emplace(key, visit.direction).second)
                queue.push_back({key, visit.direction, visit.distance + 1});
        }
    }
    for (const auto &source : included)
        for (const auto &target : dependencies[source.first])
            if (const auto found = included.find(target); found != included.end())
                graph.edges.push_back({source.second, found->second});
    std::map<int, std::vector<std::size_t>> columns;
    for (const auto &entry : included)
        columns[graph.nodes[entry.second].column].push_back(entry.second);
    for (const auto &column : columns)
        for (std::size_t row = 0; row < column.second.size(); ++row)
            graph.nodes[column.second[row]].row = static_cast<float>(row) - (static_cast<float>(column.second.size()) - 1) * .5f;
    return graph;
}
} // namespace MiniEngine
