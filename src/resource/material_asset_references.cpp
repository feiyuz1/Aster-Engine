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
bool legacyReferenceField(const std::string& key)
{
    return key == "parent" || key == "shader" || key == "texture" || key == "normalTexture" ||
           key == "metallicTexture" || key == "roughnessTexture" || key == "aoTexture" || key == "emissiveTexture" ||
           key == "packedTexture";
}
template <class Visitor> bool visitReferences(std::string& source, bool graph, Visitor visitor, bool write)
{
    if (graph)
    {
        MaterialGraph parsed;
        if (!parsed.parse(source))
            return false;
        bool changed = false;
        for (auto& p : parsed.GetParameters())
            if (auto* path = std::get_if<std::string>(&p.defaultValue))
                changed = visitor(*path) || changed;
        for (auto& n : parsed.GetNodes())
            if (n.type == MaterialNodeType::Texture)
                changed = visitor(n.texturePath) || changed;
        if (changed && write)
            source = parsed.serialize();
        return changed;
    }
    std::istringstream header(source);
    std::string first;
    header >> first;
    if (first == "TINY_MAT")
    {
        MaterialAssetData parsed;
        std::string error;
        if (!parseMaterialAsset(source, parsed, error))
            return false;
        bool changed = visitor(parsed.parentPath);
        changed = visitor(parsed.definitionPath) || changed;
        for (auto& p : parsed.overrides)
            if (auto* path = std::get_if<std::string>(&p.second))
                changed = visitor(*path) || changed;
        if (changed && write)
            source = serializeMaterialAsset(parsed);
        return changed;
    }
    std::istringstream input(source);
    std::ostringstream output;
    std::string line;
    bool changed = false;
    while (std::getline(input, line))
    {
        std::istringstream fields(line);
        std::string key, reference;
        fields >> key;
        if (legacyReferenceField(key) && (fields >> std::quoted(reference)) && visitor(reference))
        {
            std::ostringstream replacement;
            replacement << key << ' ' << std::quoted(reference);
            line = replacement.str();
            changed = true;
        }
        output << line << '\n';
    }
    if (changed && write)
        source = output.str();
    return changed;
}
} // namespace
std::vector<std::string> CollectMaterialReferences(const std::string& source, bool graph)
{
    std::string copy = source;
    std::vector<std::string> references;
    visitReferences(
        copy, graph,
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
bool rewriteMaterialReferences(std::string& source, bool graph, const std::filesystem::path& owner,
                               const std::filesystem::path& newOwner, const std::filesystem::path& assetRoot,
                               const std::string& oldPath, const std::string& newPath)
{
    std::istringstream header(source);
    std::string first;
    header >> first;
    const bool legacy = !graph && first != "TINY_MAT";
    return visitReferences(
        source, graph,
        [&](std::string& path)
        {
            if (path.empty() || path == "builtin:PBR")
                return false;
            auto resolved = resolveMaterialReference(path, owner, assetRoot);
            const bool directLegacy = legacy && std::filesystem::absolute(fromUtf8Path(path)).lexically_normal() ==
                                                    std::filesystem::absolute(fromUtf8Path(oldPath)).lexically_normal();
            if (directLegacy)
            {
                path = newPath;
                return true;
            }
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
