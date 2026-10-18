#include "AssetTrash.hpp"

#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Render/MaterialGraph.hpp"
#include "MiniEngine/Resource/AssetMaintenance.hpp"
#include "MiniEngine/Resource/MaterialAssetReferences.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"

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
#include <windows.h>
#endif

namespace MiniEngine
{
namespace
{
namespace fs = std::filesystem;

std::string Key(const fs::path &path)
{
    auto value = fs::absolute(path).lexically_normal().generic_u8string();
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return value;
}
bool Under(const fs::path &path, const fs::path &root)
{
    const auto child = fs::u8path(Key(path)), parent = fs::u8path(Key(root));
    auto left = child.begin(), right = parent.begin();
    for (; right != parent.end(); ++left, ++right)
        if (left == child.end() || *left != *right)
            return false;
    return true;
}
void Require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}
void NoReparse(const fs::path &path)
{
    fs::path prefix;
    for (const auto &component : fs::absolute(path).lexically_normal())
    {
        prefix /= component;
        std::error_code error;
        const auto status = fs::symlink_status(prefix, error);
        if (error == std::errc::no_such_file_or_directory)
            continue;
        Require(!error, "Cannot inspect path: " + prefix.generic_u8string());
        Require(!fs::is_symlink(status), "Symbolic links are not supported: " + prefix.generic_u8string());
#ifdef _WIN32
        const DWORD attributes = GetFileAttributesW(prefix.c_str());
        Require(attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "Reparse points are not supported: " + prefix.generic_u8string());
#endif
    }
}
fs::path SafeAbsolute(const fs::path &path)
{
    Require(!path.empty(), "An asset or project path is empty");
    const auto absolute = fs::absolute(path).lexically_normal();
    NoReparse(absolute);
    return fs::weakly_canonical(absolute);
}
fs::path Meta(const fs::path &path)
{
    return fs::u8path(path.generic_u8string() + ".meta");
}
bool Occupied(const fs::path &path)
{
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return false;
    Require(!error, "Cannot inspect destination: " + path.generic_u8string());
    return fs::exists(status);
}
bool ReadText(const fs::path &path, std::string &text)
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
std::string ResolvedSelection(ResourceManager &resources, const std::string &path)
{
    if (const auto info = resources.GetAssetRegistry().FindById(path))
        return info->path;
    return resources.ResolveAssetPath(path);
}

// Metadata has historically stored both asset-root-relative and owner-relative
// paths. Check both styles, with GUID identity taking precedence.
std::set<std::string> ReferenceKeys(ResourceManager &resources, const std::string &reference, const std::string &owner)
{
    if (reference.empty() || reference.rfind("builtin:", 0) == 0)
        return {};
    if (const auto info = resources.GetAssetRegistry().FindById(reference))
        return {Key(fs::u8path(info->path))};
    return {Key(fs::u8path(resources.ResolveAssetPath(reference, owner))), Key(fs::u8path(resources.ResolveAssetPath(reference)))};
}
bool SerializedType(const std::string &type)
{
    return type == "Scene" || type == "Prefab" || type == "Shader" || type == "Material" || type == "MaterialGraph" || type == "VFX" ||
           type == "Environment" || type == "BlendTree" || type == "AnimationGraph";
}
bool ValidSerializedText(const std::string &type, const std::string &text)
{
    if (type == "Scene" || type == "Prefab")
    {
        World validation;
        return SceneSerializer::Deserialize(validation, text);
    }
    if (type == "MaterialGraph")
    {
        MaterialGraph validation;
        return validation.Parse(text);
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
            return ParseMaterialAsset(text, validation, error);
        }
    }
    return true;
}
std::set<std::string> Tokens(const std::string &text)
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
void WriteManifest(const fs::path &path, const Manifest &manifest)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(output), "Cannot create trash manifest");
    output << "TINYENGINE_ASSET_TRASH 1\n"
           << std::quoted(manifest.root.generic_u8string()) << '\n'
           << std::quoted(manifest.project.generic_u8string()) << '\n'
           << manifest.entries.size() << '\n';
    for (const auto &entry : manifest.entries)
        output << std::quoted(entry.relative.generic_u8string()) << '\n';
    output.flush();
    Require(static_cast<bool>(output), "Cannot write trash manifest");
    output.close();
    Require(!output.fail(), "Cannot close trash manifest");
}
Manifest ReadManifest(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    std::string format, root, project;
    unsigned int version = 0;
    std::size_t count = 0;
    Require(static_cast<bool>(input >> format >> version >> std::quoted(root) >> std::quoted(project) >> count) &&
                format == "TINYENGINE_ASSET_TRASH" && version == 1 && count > 0,
            "Invalid asset trash manifest");
    Manifest manifest{fs::u8path(root), fs::u8path(project), {}};
    std::set<std::string> entries;
    for (std::size_t index = 0; index < count; ++index)
    {
        std::string relative;
        Require(static_cast<bool>(input >> std::quoted(relative)), "Incomplete asset trash manifest");
        const auto path = fs::u8path(relative);
        Require(!path.empty() && !path.is_absolute() && !path.has_root_name() && path == path.lexically_normal() &&
                    std::none_of(path.begin(), path.end(), [](const auto &part) { return part == ".." || part == "."; }) &&
                    entries.insert(Key(manifest.root / path)).second,
                "Invalid or duplicate path in trash manifest");
        manifest.entries.push_back({path});
    }
    input >> std::ws;
    Require(input.eof(), "Unexpected trailing data in trash manifest");
    return manifest;
}
struct FileMove
{
    fs::path source;
    fs::path destination;
};
bool Rollback(std::vector<FileMove> &moved, std::string &error)
{
    bool success = true;
    for (auto it = moved.rbegin(); it != moved.rend(); ++it)
    {
        std::error_code code;
        fs::rename(it->destination, it->source, code);
        if (code)
        {
            success = false;
            error += "; rollback failed for " + it->source.generic_u8string() + ": " + code.message();
        }
    }
    return success;
}
void Rescan(ResourceManager &resources, const fs::path &root, std::string &error)
{
    try
    {
        resources.GetAssetRegistry().Scan(root);
    }
    catch (const std::exception &exception)
    {
        error += "; registry rescan failed: " + std::string(exception.what());
    }
}
void MoveFiles(const std::vector<FileMove> &planned, std::vector<FileMove> &moved)
{
    for (const auto &file : planned)
    {
        NoReparse(file.source);
        NoReparse(file.destination);
        Require(!Occupied(file.destination), "Destination already exists: " + file.destination.generic_u8string());
        fs::create_directories(file.destination.parent_path());
        NoReparse(file.destination.parent_path());
        fs::rename(file.source, file.destination);
        moved.push_back(file);
    }
}
} // namespace

std::vector<AssetTrashReference> AssetTrash::FindReferences(ResourceManager &resources, const std::vector<std::string> &selectedPaths)
{
    std::map<std::string, std::string> selected;
    for (const auto &path : selectedPaths)
    {
        if (path.empty())
            continue;
        const auto resolved = ResolvedSelection(resources, path);
        selected.emplace(Key(fs::u8path(resolved)), resolved);
    }
    std::set<std::pair<std::string, std::string>> incoming;
    const auto root = resources.GetAssetRegistry().GetRootPath();
    for (const auto &owner : resources.GetAssetRegistry().GetAssets())
    {
        try
        {
            if (selected.count(Key(fs::u8path(owner.path))))
                continue;
            const auto add = [&](const std::string &reference) {
                for (const auto &key : ReferenceKeys(resources, reference, owner.path))
                    if (const auto target = selected.find(key); target != selected.end())
                        incoming.emplace(target->second, owner.path);
            };
            for (const auto &reference : owner.dependencies)
                add(reference);
            if (!SerializedType(owner.type))
                continue;
            std::string text;
            if (!ReadText(fs::u8path(owner.path), text) || !ValidSerializedText(owner.type, text))
            {
                // A source that cannot be read or parsed cannot establish safe deletion.
                for (const auto &target : selected)
                    incoming.emplace(target.second, owner.path);
                continue;
            }
            if (owner.type == "Material" || owner.type == "MaterialGraph")
                for (const auto &reference : CollectMaterialReferences(text, owner.type == "MaterialGraph"))
                    add(reference);
            if (owner.type == "Shader")
            {
                for (const auto &target : selected)
                {
                    auto copy = text;
                    const auto source = fs::u8path(target.second);
                    if (AssetMaintenance::RewriteShaderIncludes(copy, fs::u8path(owner.path), fs::u8path(owner.path), source,
                                                                fs::u8path(source.generic_u8string() + ".trash-probe")))
                        incoming.emplace(target.second, owner.path);
                }
                continue;
            }
            for (const auto &token : Tokens(text))
            {
                bool selectedReference = false;
                for (const auto &key : ReferenceKeys(resources, token, owner.path))
                    selectedReference = selected.count(key) != 0 || selectedReference;
                if (!selectedReference)
                    continue;
                auto copy = text;
                // Testing the literal token as the source makes the existing typed
                // field parser handle GUID and ../ styles without rewriting files.
                const auto probe = fs::u8path(fs::absolute(fs::u8path(token)).lexically_normal().generic_u8string() + ".trash-probe");
                if (AssetMaintenance::RewriteReferences(copy, fs::u8path(token), probe, root))
                    add(token);
            }
        }
        catch (const std::exception &)
        {
            for (const auto &target : selected)
                incoming.emplace(target.second, owner.path);
        }
    }
    std::vector<AssetTrashReference> references;
    for (const auto &entry : incoming)
        references.push_back({entry.first, entry.second});
    return references;
}

bool AssetTrash::MoveToTrash(ResourceManager &resources, const std::string &projectRoot, const std::vector<std::string> &paths,
                             std::string &manifestPath, std::string &error)
{
    manifestPath.clear();
    error.clear();
    fs::path group, root;
    bool groupCreated = false;
    std::vector<FileMove> moved;
    try
    {
        Require(!paths.empty(), "No assets selected");
        root = SafeAbsolute(resources.GetAssetRegistry().GetRootPath());
        const auto project = SafeAbsolute(fs::u8path(projectRoot));
        Require(fs::is_directory(root) && fs::is_directory(project) && Under(root, project),
                "Asset root must be inside the project directory");
        const auto trash = SafeAbsolute(project / "Saved/DeletedAssets");
        Require(!Under(trash, root), "Project trash must be outside the asset directory");
        Manifest manifest{root, project, {}};
        std::set<std::string> selectedKeys;
        std::vector<std::string> selection;
        const auto assets = resources.GetAssetRegistry().GetAssets();
        for (const auto &path : paths)
        {
            Require(!path.empty(), "A selected asset path is empty");
            const auto source = SafeAbsolute(fs::u8path(ResolvedSelection(resources, path)));
            Require(Under(source, root) && Key(source) != Key(root) && fs::is_regular_file(source),
                    "Selected asset must be a regular file inside the asset directory: " + path);
            Require(
                std::any_of(assets.begin(), assets.end(), [&](const auto &asset) { return Key(fs::u8path(asset.path)) == Key(source); }),
                "Selected asset is not registered: " + path);
            NoReparse(Meta(source));
            Require(fs::is_regular_file(Meta(source)), "Selected asset metadata is missing: " + path);
            if (!selectedKeys.insert(Key(source)).second)
                continue;
            manifest.entries.push_back({source.lexically_relative(root)});
            selection.push_back(source.generic_u8string());
        }
        const auto references = FindReferences(resources, selection);
        Require(references.empty(), references.empty()
                                        ? std::string{}
                                        : "Asset is referenced by " + references.front().owner + ": " + references.front().target);
        fs::create_directories(trash);
        NoReparse(trash);
        static std::atomic<unsigned long long> sequence{0};
        do
        {
            group = trash / (std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
                             std::to_string(sequence.fetch_add(1)));
        } while (!fs::create_directory(group));
        groupCreated = true;
        const auto manifestFile = group / "manifest.txt";
        WriteManifest(manifestFile, manifest);
        std::vector<FileMove> planned;
        for (const auto &entry : manifest.entries)
        {
            const auto source = root / entry.relative, destination = group / "files" / entry.relative;
            planned.push_back({source, destination});
            planned.push_back({Meta(source), Meta(destination)});
        }
        moved.reserve(planned.size());
        MoveFiles(planned, moved);
        resources.GetAssetRegistry().Scan(root);
        for (const auto &path : selection)
            resources.UnloadUnused(path);
        manifestPath = manifestFile.generic_u8string();
        return true;
    }
    catch (const std::exception &exception)
    {
        error = exception.what();
        const bool rolledBack = Rollback(moved, error);
        if (!moved.empty())
            Rescan(resources, root, error);
        if (groupCreated && rolledBack)
        {
            std::error_code ignored;
            fs::remove_all(group, ignored);
        }
        else if (groupCreated)
            error += "; recovery files remain in " + group.generic_u8string();
        return false;
    }
}

bool AssetTrash::Restore(ResourceManager &resources, const std::string &manifestPath, std::string &error)
{
    error.clear();
    fs::path root;
    std::vector<FileMove> moved;
    try
    {
        const auto file = SafeAbsolute(fs::u8path(manifestPath));
        Require(fs::is_regular_file(file) && file.filename() == "manifest.txt", "Asset trash manifest is missing");
        const auto manifest = ReadManifest(file);
        root = SafeAbsolute(resources.GetAssetRegistry().GetRootPath());
        const auto project = SafeAbsolute(manifest.project);
        const auto expectedRoot = SafeAbsolute(manifest.root), group = file.parent_path();
        Require(Key(expectedRoot) == Key(root) && Under(root, project) &&
                    Key(group.parent_path()) == Key(project / "Saved/DeletedAssets") && !Under(group, root),
                "Trash manifest does not belong to this project's asset directory");
        std::vector<FileMove> planned;
        for (const auto &entry : manifest.entries)
        {
            const auto destination = SafeAbsolute(root / entry.relative), source = SafeAbsolute(group / "files" / entry.relative);
            Require(Under(destination, root) && Key(destination) != Key(root) && Under(source, group / "files"),
                    "Invalid trash entry path");
            NoReparse(Meta(destination));
            NoReparse(Meta(source));
            Require(!Occupied(destination) && !Occupied(Meta(destination)),
                    "Restore destination already exists: " + destination.generic_u8string());
            Require(fs::is_regular_file(source) && fs::is_regular_file(Meta(source)),
                    "Trash asset or metadata is missing: " + source.generic_u8string());
            planned.push_back({source, destination});
            planned.push_back({Meta(source), Meta(destination)});
        }
        moved.reserve(planned.size());
        MoveFiles(planned, moved);
        resources.GetAssetRegistry().Scan(root);
        std::error_code ignored;
        fs::remove_all(group, ignored);
        return true;
    }
    catch (const std::exception &exception)
    {
        error = exception.what();
        Rollback(moved, error);
        if (!moved.empty())
            Rescan(resources, root, error);
        return false;
    }
}
} // namespace MiniEngine
