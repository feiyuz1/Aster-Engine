#include "asset_trash.h"

#include "tinyengine/ecs/ecs.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/render/material_graph.h"
#include "tinyengine/resource/asset_maintenance.h"
#include "tinyengine/resource/material_asset_references.h"
#include "tinyengine/resource/resource_manager.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "tinyengine/platform/platform.h"
#include <windows.h>
#endif

namespace tinyengine
{
namespace
{
namespace fs = std::filesystem;

std::string Key(const fs::path& path)
{
    auto value = toUtf8GenericPath(fs::absolute(path).lexically_normal());
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return value;
}
bool under(const fs::path& path, const fs::path& root)
{
    const auto child = fromUtf8Path(Key(path)), parent = fromUtf8Path(Key(root));
    auto left = child.begin(), right = parent.begin();
    for (; right != parent.end(); ++left, ++right)
        if (left == child.end() || *left != *right)
            return false;
    return true;
}
void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void noReparse(const fs::path& path)
{
    fs::path prefix;
    for (const auto& component : fs::absolute(path).lexically_normal())
    {
        prefix /= component;
        std::error_code error;
        const auto status = fs::symlink_status(prefix, error);
        if (error == std::errc::no_such_file_or_directory)
            continue;
        require(!error, "Cannot inspect path: " + toUtf8GenericPath(prefix));
        require(!fs::is_symlink(status), "Symbolic links are not supported: " + toUtf8GenericPath(prefix));
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(prefix.c_str());
        require(attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "Reparse points are not supported: " + toUtf8GenericPath(prefix));
#endif
    }
}
fs::path safeAbsolute(const fs::path& path)
{
    require(!path.empty(), "An asset or project path is empty");
    const auto absolute = fs::absolute(path).lexically_normal();
    noReparse(absolute);
    return fs::weakly_canonical(absolute);
}
fs::path Meta(const fs::path& path)
{
    return fromUtf8Path(toUtf8GenericPath(path) + ".meta");
}
bool occupied(const fs::path& path)
{
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return false;
    require(!error, "Cannot inspect destination: " + toUtf8GenericPath(path));
    return fs::exists(status);
}
bool readText(const fs::path& path, std::string& text)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad())
        return false;
    text = buffer.str();
    return true;
}
std::string resolvedSelection(ResourceManager& resources, const std::string& path)
{
    if (const auto info = resources.getAssetRegistry().FindById(path))
        return info->path;
    return resources.resolveAssetPath(path);
}

// Metadata has historically stored both asset-root-relative and owner-relative
// paths. Check both styles, with GUID identity taking precedence.
std::set<std::string> ReferenceKeys(ResourceManager& resources, const std::string& reference, const std::string& owner)
{
    if (reference.empty() || reference.rfind("builtin:", 0) == 0)
        return {};
    if (const auto info = resources.getAssetRegistry().FindById(reference))
        return {Key(fromUtf8Path(info->path))};
    return {Key(fromUtf8Path(resources.resolveAssetPath(reference, owner))),
            Key(fromUtf8Path(resources.resolveAssetPath(reference)))};
}
bool serializedType(const std::string& type)
{
    return type == "Scene" || type == "Prefab" || type == "Shader" || type == "Material" || type == "MaterialGraph" ||
           type == "VFX" || type == "Environment" || type == "BlendTree" || type == "AnimationGraph";
}
bool validSerializedText(const std::string& type, const std::string& text)
{
    if (type == "Scene" || type == "Prefab")
    {
        World validation;
        return SceneSerializer::deserialize(validation, text);
    }
    if (type == "MaterialGraph")
    {
        MaterialGraph validation;
        return validation.parse(text);
    }
    if (type == "Material")
    {
        std::istringstream input(text);
        std::string format;
        input >> format;
        if (format == "TINY_MAT")
        {
            MaterialAssetData validation;
            std::string error;
            return parseMaterialAsset(text, validation, error);
        }
    }
    return true;
}
std::set<std::string> Tokens(const std::string& text)
{
    std::set<std::string> tokens;
    std::istringstream input(text);
    std::string token;
    while (input >> std::quoted(token))
        if (!token.empty())
            tokens.insert(token);
    return tokens;
}

struct TrashEntry
{
    fs::path relative;
};
struct Manifest
{
    fs::path root;
    fs::path project;
    std::vector<TrashEntry> entries;
};
void writeManifest(const fs::path& path, const Manifest& manifest)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "Cannot create trash manifest");
    output << "TINYENGINE_ASSET_TRASH 1\n"
           << std::quoted(toUtf8GenericPath(manifest.root)) << '\n'
           << std::quoted(toUtf8GenericPath(manifest.project)) << '\n'
           << manifest.entries.size() << '\n';
    for (const auto& entry : manifest.entries)
        output << std::quoted(toUtf8GenericPath(entry.relative)) << '\n';
    output.flush();
    require(static_cast<bool>(output), "Cannot write trash manifest");
    output.close();
    require(!output.fail(), "Cannot close trash manifest");
}
Manifest readManifest(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::string format, root, project;
    unsigned int version = 0;
    std::size_t count = 0;
    require(static_cast<bool>(input >> format >> version >> std::quoted(root) >> std::quoted(project) >> count) &&
                format == "TINYENGINE_ASSET_TRASH" && version == 1 && count > 0,
            "Invalid asset trash manifest");
    Manifest manifest{fromUtf8Path(root), fromUtf8Path(project), {}};
    std::set<std::string> entries;
    for (std::size_t index = 0; index < count; ++index)
    {
        std::string relative;
        require(static_cast<bool>(input >> std::quoted(relative)), "Incomplete asset trash manifest");
        const auto path = fromUtf8Path(relative);
        require(
            !path.empty() && !path.is_absolute() && !path.has_root_name() && path == path.lexically_normal() &&
                std::none_of(path.begin(), path.end(), [](const auto& part) { return part == ".." || part == "."; }) &&
                entries.insert(Key(manifest.root / path)).second,
            "Invalid or duplicate path in trash manifest");
        manifest.entries.push_back({path});
    }
    input >> std::ws;
    require(input.eof(), "Unexpected trailing data in trash manifest");
    return manifest;
}
struct FileMove
{
    fs::path source;
    fs::path destination;
};
bool rollback(std::vector<FileMove>& moved, std::string& error)
{
    bool success = true;
    for (auto it = moved.rbegin(); it != moved.rend(); ++it)
    {
        std::error_code code;
        fs::rename(it->destination, it->source, code);
        if (code)
        {
            success = false;
            error += "; rollback failed for " + toUtf8GenericPath(it->source) + ": " + code.message();
        }
    }
    return success;
}
void rescan(ResourceManager& resources, const fs::path& root, std::string& error)
{
    try
    {
        resources.getAssetRegistry().scan(root);
    }
    catch (const std::exception& exception)
    {
        error += "; registry rescan failed: " + std::string(exception.what());
    }
}
void moveFiles(const std::vector<FileMove>& planned, std::vector<FileMove>& moved)
{
    for (const auto& file : planned)
    {
        noReparse(file.source);
        noReparse(file.destination);
        require(!occupied(file.destination), "Destination already exists: " + toUtf8GenericPath(file.destination));
        fs::create_directories(file.destination.parent_path());
        noReparse(file.destination.parent_path());
        fs::rename(file.source, file.destination);
        moved.push_back(file);
    }
}
} // namespace

std::vector<AssetTrashReference> AssetTrash::FindReferences(ResourceManager& resources,
                                                            const std::vector<std::string>& selectedPaths)
{
    std::map<std::string, std::string> selected;
    for (const auto& path : selectedPaths)
    {
        if (path.empty())
            continue;
        const auto resolved = resolvedSelection(resources, path);
        selected.emplace(Key(fromUtf8Path(resolved)), resolved);
    }
    std::set<std::pair<std::string, std::string>> incoming;
    const auto root = resources.getAssetRegistry().GetRootPath();
    for (const auto& owner : resources.getAssetRegistry().GetAssets())
    {
        try
        {
            if (selected.count(Key(fromUtf8Path(owner.path))))
                continue;
            const auto add = [&](const std::string& reference)
            {
                for (const auto& key : ReferenceKeys(resources, reference, owner.path))
                    if (const auto target = selected.find(key); target != selected.end())
                        incoming.emplace(target->second, owner.path);
            };
            for (const auto& reference : owner.dependencies)
                add(reference);
            if (!serializedType(owner.type))
                continue;
            std::string text;
            if (!readText(fromUtf8Path(owner.path), text) || !validSerializedText(owner.type, text))
            {
                // A source that cannot be read or parsed cannot establish safe deletion.
                for (const auto& target : selected)
                    incoming.emplace(target.second, owner.path);
                continue;
            }
            if (owner.type == "Material" || owner.type == "MaterialGraph")
                for (const auto& reference : CollectMaterialReferences(text, owner.type == "MaterialGraph"))
                    add(reference);
            if (owner.type == "Shader")
            {
                for (const auto& target : selected)
                {
                    auto copy = text;
                    const auto source = fromUtf8Path(target.second);
                    if (AssetMaintenance::rewriteShaderIncludes(
                            copy, fromUtf8Path(owner.path), fromUtf8Path(owner.path), source,
                            fromUtf8Path(toUtf8GenericPath(source) + ".trash-probe")))
                        incoming.emplace(target.second, owner.path);
                }
                continue;
            }
            for (const auto& token : Tokens(text))
            {
                bool selectedReference = false;
                for (const auto& key : ReferenceKeys(resources, token, owner.path))
                    selectedReference = selected.count(key) != 0 || selectedReference;
                if (!selectedReference)
                    continue;
                auto copy = text;
                // Testing the literal token as the source makes the existing typed
                // field parser handle GUID and ../ styles without rewriting files.
                const auto probe = fromUtf8Path(
                    toUtf8GenericPath(fs::absolute(fromUtf8Path(token)).lexically_normal()) + ".trash-probe");
                if (AssetMaintenance::rewriteReferences(copy, fromUtf8Path(token), probe, root))
                    add(token);
            }
        }
        catch (const std::exception&)
        {
            for (const auto& target : selected)
                incoming.emplace(target.second, owner.path);
        }
    }
    std::vector<AssetTrashReference> references;
    for (const auto& entry : incoming)
        references.push_back({entry.first, entry.second});
    return references;
}

bool AssetTrash::moveToTrash(ResourceManager& resources, const std::string& projectRoot,
                             const std::vector<std::string>& paths, std::string& manifestPath, std::string& error)
{
    manifestPath.clear();
    error.clear();
    fs::path group, root;
    bool groupCreated = false;
    std::vector<FileMove> moved;
    try
    {
        require(!paths.empty(), "No assets selected");
        root = safeAbsolute(resources.getAssetRegistry().GetRootPath());
        const auto project = safeAbsolute(fromUtf8Path(projectRoot));
        require(fs::is_directory(root) && fs::is_directory(project) && under(root, project),
                "Asset root must be inside the project directory");
        const auto trash = safeAbsolute(project / "saved/DeletedAssets");
        require(!under(trash, root), "Project trash must be outside the asset directory");
        Manifest manifest{root, project, {}};
        std::set<std::string> selectedKeys;
        std::vector<std::string> selection;
        const auto assets = resources.getAssetRegistry().GetAssets();
        for (const auto& path : paths)
        {
            require(!path.empty(), "A selected asset path is empty");
            const auto source = safeAbsolute(fromUtf8Path(resolvedSelection(resources, path)));
            require(under(source, root) && Key(source) != Key(root) && fs::is_regular_file(source),
                    "Selected asset must be a regular file inside the asset directory: " + path);
            require(std::any_of(assets.begin(), assets.end(),
                                [&](const auto& asset) { return Key(fromUtf8Path(asset.path)) == Key(source); }),
                    "Selected asset is not registered: " + path);
            noReparse(Meta(source));
            require(fs::is_regular_file(Meta(source)), "Selected asset metadata is missing: " + path);
            if (!selectedKeys.insert(Key(source)).second)
                continue;
            manifest.entries.push_back({source.lexically_relative(root)});
            selection.push_back(toUtf8GenericPath(source));
        }
        const auto references = FindReferences(resources, selection);
        require(references.empty(), references.empty() ? std::string{}
                                                       : "Asset is referenced by " + references.front().owner + ": " +
                                                             references.front().target);
        fs::create_directories(trash);
        noReparse(trash);
        static std::atomic<unsigned long long> sequence{0};
        do
        {
            group = trash / (std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
                             std::to_string(sequence.fetch_add(1)));
        } while (!fs::create_directory(group));
        groupCreated = true;
        const auto manifestFile = group / "manifest.txt";
        writeManifest(manifestFile, manifest);
        std::vector<FileMove> planned;
        for (const auto& entry : manifest.entries)
        {
            const auto source = root / entry.relative, destination = group / "files" / entry.relative;
            planned.push_back({source, destination});
            planned.push_back({Meta(source), Meta(destination)});
        }
        moved.reserve(planned.size());
        moveFiles(planned, moved);
        resources.getAssetRegistry().scan(root);
        for (const auto& path : selection)
            resources.unloadUnused(path);
        manifestPath = toUtf8GenericPath(manifestFile);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        const bool rolledBack = rollback(moved, error);
        if (!moved.empty())
            rescan(resources, root, error);
        if (groupCreated && rolledBack)
        {
            std::error_code ignored;
            fs::remove_all(group, ignored);
        }
        else if (groupCreated)
            error += "; recovery files remain in " + toUtf8GenericPath(group);
        return false;
    }
}

bool AssetTrash::Restore(ResourceManager& resources, const std::string& manifestPath, std::string& error)
{
    error.clear();
    fs::path root;
    std::vector<FileMove> moved;
    try
    {
        const auto file = safeAbsolute(fromUtf8Path(manifestPath));
        require(fs::is_regular_file(file) && file.filename() == "manifest.txt", "Asset trash manifest is missing");
        const auto manifest = readManifest(file);
        root = safeAbsolute(resources.getAssetRegistry().GetRootPath());
        const auto project = safeAbsolute(manifest.project);
        const auto expectedRoot = safeAbsolute(manifest.root), group = file.parent_path();
        require(Key(expectedRoot) == Key(root) && under(root, project) &&
                    Key(group.parent_path()) == Key(project / "saved/DeletedAssets") && !under(group, root),
                "Trash manifest does not belong to this project's asset directory");
        std::vector<FileMove> planned;
        for (const auto& entry : manifest.entries)
        {
            const auto destination = safeAbsolute(root / entry.relative),
                       source = safeAbsolute(group / "files" / entry.relative);
            require(under(destination, root) && Key(destination) != Key(root) && under(source, group / "files"),
                    "Invalid trash entry path");
            noReparse(Meta(destination));
            noReparse(Meta(source));
            require(!occupied(destination) && !occupied(Meta(destination)),
                    "Restore destination already exists: " + toUtf8GenericPath(destination));
            require(fs::is_regular_file(source) && fs::is_regular_file(Meta(source)),
                    "Trash asset or metadata is missing: " + toUtf8GenericPath(source));
            planned.push_back({source, destination});
            planned.push_back({Meta(source), Meta(destination)});
        }
        moved.reserve(planned.size());
        moveFiles(planned, moved);
        resources.getAssetRegistry().scan(root);
        std::error_code ignored;
        fs::remove_all(group, ignored);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        rollback(moved, error);
        if (!moved.empty())
            rescan(resources, root, error);
        return false;
    }
}
} // namespace tinyengine
