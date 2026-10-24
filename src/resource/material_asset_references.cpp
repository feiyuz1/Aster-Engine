#include "tinyengine/resource/material_asset_references.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/render/material_graph.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace tinyengine
{
std::string resolveMaterialReference(const std::string& reference, const std::filesystem::path& owner,
                                     const std::filesystem::path& assetRoot)
{
    if (reference.empty() || reference == "builtin:PBR")
        return reference;
    const std::filesystem::path path = fromUtf8Path(reference);
    if (path.is_absolute())
        return toUtf8GenericPath(path.lexically_normal());
    const auto first = path.begin();
    if (first != path.end() && (*first == "assets" || *first == assetRoot.filename()))
    {
        std::filesystem::path suffix;
        for (auto it = std::next(first); it != path.end(); ++it)
            suffix /= *it;
        return toUtf8GenericPath((assetRoot / suffix).lexically_normal());
    }
    return toUtf8GenericPath((owner.parent_path() / path).lexically_normal());
}
namespace
{
template <class Visitor> bool visitReferences(std::string& source, Visitor visitor, bool write)
{
    MaterialAssetData parsed;
    std::string error;
    if (!parseMaterialAsset(source, parsed, error))
        return false;
    bool changed = false;
    if (parsed.graph)
    {
        for (auto& parameter : parsed.graph->GetParameters())
            if (auto* path = std::get_if<std::string>(&parameter.defaultValue))
                changed = visitor(*path) || changed;
    }
    else
    {
        changed = visitor(parsed.parentPath);
        for (auto& entry : parsed.overrides)
            if (auto* path = std::get_if<std::string>(&entry.second))
                changed = visitor(*path) || changed;
    }
    if (changed && write)
        source = serializeMaterialAsset(parsed);
    return changed;
}
} // namespace
std::vector<std::string> CollectMaterialReferences(const std::string& source)
{
    std::string copy = source;
    std::vector<std::string> references;
    visitReferences(
        copy,
        [&](std::string& path)
        {
            if (!path.empty() && path != "builtin:PBR" &&
                std::find(references.begin(), references.end(), path) == references.end())
                references.push_back(path);
            return false;
        },
        false);
    return references;
}
bool rewriteMaterialReferences(std::string& source, const std::filesystem::path& owner,
                               const std::filesystem::path& newOwner, const std::filesystem::path& assetRoot,
                               const std::string& oldPath, const std::string& newPath)
{
    return visitReferences(
        source,
        [&](std::string& path)
        {
            if (path.empty() || path == "builtin:PBR")
                return false;
            auto resolved = resolveMaterialReference(path, owner, assetRoot);
            if (std::filesystem::absolute(fromUtf8Path(resolved)).lexically_normal() ==
                std::filesystem::absolute(fromUtf8Path(oldPath)).lexically_normal())
                resolved = newPath;
            else if (owner == newOwner)
                return false;
            auto relative = std::filesystem::absolute(fromUtf8Path(resolved))
                                .lexically_normal()
                                .lexically_relative(std::filesystem::absolute(assetRoot).lexically_normal());
            const std::string rewritten =
                !relative.empty() && *relative.begin() != ".." ? "assets/" + toUtf8GenericPath(relative) : resolved;
            if (path == rewritten)
                return false;
            path = rewritten;
            return true;
        },
        true);
}
} // namespace tinyengine
