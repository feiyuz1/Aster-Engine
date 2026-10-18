#include "AssetTrash.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
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
void Check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
struct TemporaryProject
{
    std::filesystem::path root =
        std::filesystem::absolute("Saved") / ("AssetTrash_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path assets = root / "Content/Game";
    ResourceManager resources;
    TemporaryProject()
    {
        std::filesystem::create_directories(assets);
    }
    ~TemporaryProject()
    {
        resources.Shutdown();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void Scan()
    {
        resources.Initialize(nullptr, assets, "Content/Game");
    }
    void Write(const std::filesystem::path &path, const std::string &text = "asset payload\n")
    {
        Check(PlatformFile::WriteAllText(path, text), "write real asset fixture");
    }
};
bool Contains(const std::vector<AssetTrashReference> &references, const std::filesystem::path &target, const std::filesystem::path &owner)
{
    return std::any_of(references.begin(), references.end(), [&](const auto &reference) {
        return reference.target == target.generic_u8string() && reference.owner == owner.generic_u8string();
    });
}
void RoundTripUnicode()
{
    // Losing a sidecar, GUID, content, UTF-8 path or full-root registry scan breaks recovery.
    TemporaryProject project;
    const auto asset = project.assets / std::filesystem::u8path(u8"模型/茶杯.mesh");
    const auto unrelated = project.assets / "Elsewhere/keep.mesh";
    project.Write(asset, "original unicode asset\n");
    project.Write(unrelated);
    project.Scan();
    const auto original = project.resources.GetAssetRegistry().Find(asset.generic_u8string());
    Check(original.has_value(), "source is registered before deletion");
    const auto metadata = PlatformFile::ReadAllText(std::filesystem::u8path(asset.generic_u8string() + ".meta"));
    std::string manifest, error;
    Check(AssetTrash::MoveToTrash(project.resources, project.root.generic_u8string(), {asset.generic_u8string()}, manifest, error),
          "unreferenced Unicode asset moves to project trash");
    Check(!manifest.empty() && std::filesystem::is_regular_file(std::filesystem::u8path(manifest)), "trash writes a durable manifest");
    Check(std::filesystem::u8path(manifest).lexically_relative(project.root / "Saved/DeletedAssets").generic_u8string().rfind("..", 0) != 0,
          "manifest is inside project Saved/DeletedAssets");
    Check(!std::filesystem::exists(asset) && !std::filesystem::exists(std::filesystem::u8path(asset.generic_u8string() + ".meta")),
          "deletion moves asset and metadata out of asset root");
    Check(!project.resources.GetAssetRegistry().FindById(original->id) &&
              project.resources.GetAssetRegistry().Find(unrelated.generic_u8string()),
          "trash rescan removes deleted GUID and retains unrelated folders");
    Check(AssetTrash::Restore(project.resources, manifest, error), "manifest restores deleted asset");
    Check(PlatformFile::ReadAllText(asset) == "original unicode asset\n" &&
              PlatformFile::ReadAllText(std::filesystem::u8path(asset.generic_u8string() + ".meta")) == metadata,
          "restore preserves asset and metadata bytes");
    const auto restored = project.resources.GetAssetRegistry().FindById(original->id);
    Check(restored && restored->path == asset.generic_u8string(), "restore rescans original GUID at original path");
    Check(!AssetTrash::Restore(project.resources, manifest, error) && !error.empty(), "already restored manifest cannot replay deletion");
}
void PortableReferences()
{
    // Resolving only raw registry paths would permit deleting assets referenced through portable paths or GUIDs.
    TemporaryProject project;
    const auto target = project.assets / "Models/item.mesh";
    project.Write(target);
    std::vector<std::filesystem::path> owners;
    for (int i = 0; i < 7; ++i)
    {
        owners.push_back(project.assets / ("Owners/owner" + std::to_string(i) + ".bin"));
        project.Write(owners.back());
    }
    project.Scan();
    auto &registry = project.resources.GetAssetRegistry();
    const auto guid = registry.Find(target.generic_u8string())->id;
    const std::vector<std::string> references = {guid,
                                                 "../Models/item.mesh",
                                                 "Models/item.mesh",
                                                 "assets/Models/item.mesh",
                                                 "Game/Models/item.mesh",
                                                 "Content/Game/Models/item.mesh",
                                                 target.generic_u8string()};
    for (std::size_t i = 0; i < owners.size(); ++i)
        Check(registry.SetDependencies(owners[i].generic_u8string(), {references[i]}), "persist real dependency metadata");
    const auto found = AssetTrash::FindReferences(project.resources, {target.generic_u8string()});
    for (const auto &owner : owners)
        Check(Contains(found, target, owner), "find incoming GUID and portable path references");
    Check(found.size() == owners.size(), "deduplicate incoming references by owner and target");
    std::string manifest, error;
    Check(!AssetTrash::MoveToTrash(project.resources, project.root.generic_u8string(), {target.generic_u8string()}, manifest, error) &&
              !error.empty() && std::filesystem::exists(target),
          "incoming references block deletion before mutation");
}
void SerializedReferences()
{
    // Scene/prefab and shader references are intentionally absent from registry dependency metadata.
    TemporaryProject project;
    const auto target = project.assets / "Models/item.mesh", include = project.assets / "Shaders/shared.glsl";
    project.Write(target);
    project.Write(include, "vec3 tint;\n");
    project.Scan();
    const auto guid = project.resources.GetAssetRegistry().Find(target.generic_u8string())->id;
    const std::vector<std::pair<std::string, std::string>> scenes = {
        {"Scenes/owner.scene", "assets/Models/item.mesh"}, {"Prefabs/owner.prefab", "../Models/item.mesh"}, {"Scenes/guid.scene", guid}};
    for (const auto &fixture : scenes)
    {
        World world;
        const auto entity = world.CreateEntity("mesh owner");
        world.AddComponent(entity, MeshComponent{fixture.second, {}});
        Check(SceneSerializer::Save(world, project.assets / fixture.first), "save real scene or prefab serialization");
    }
    const auto shader = project.assets / "Shaders/effect.frag", comments = project.assets / "Shaders/comments.frag";
    project.Write(shader, "#version 330 core\n#include \"shared.glsl\"\nvoid main() {}\n");
    project.Write(comments, "// #include \"shared.glsl\"\n/* #include \"shared.glsl\" */\n");
    World labels;
    const auto label = labels.CreateEntity("assets/Models/item.mesh");
    (void)label;
    Check(SceneSerializer::Save(labels, project.assets / "Scenes/labels.scene"), "save unrelated matching display label");
    project.resources.GetAssetRegistry().Scan(project.assets);
    const auto found = AssetTrash::FindReferences(project.resources, {target.generic_u8string(), include.generic_u8string()});
    for (const auto &fixture : scenes)
        Check(Contains(found, target, project.assets / fixture.first), "detect typed serialized scene and prefab paths including GUID");
    Check(Contains(found, include, shader) && found.size() == 4, "detect real shader include while ignoring comments and entity labels");
    Check(PlatformFile::ReadAllText(shader).find("shared.glsl") != std::string::npos, "reference discovery leaves source files unchanged");
}
void SelectedInternalReferences()
{
    // Internal references must not prevent deleting and recovering a coherent selection as a group.
    TemporaryProject project;
    const auto first = project.assets / "a.bin", second = project.assets / "b.bin";
    project.Write(first, "a\n");
    project.Write(second, "b\n");
    project.Scan();
    auto &registry = project.resources.GetAssetRegistry();
    Check(registry.SetDependencies(first.generic_u8string(), {registry.Find(second.generic_u8string())->id}) &&
              registry.SetDependencies(second.generic_u8string(), {first.generic_u8string()}),
          "persist a cyclic selected group");
    const std::vector<std::string> selection = {first.generic_u8string(), second.generic_u8string(), first.generic_u8string()};
    Check(AssetTrash::FindReferences(project.resources, selection).empty(), "selected internal references are not external blockers");
    std::string manifest, error;
    Check(AssetTrash::MoveToTrash(project.resources, project.root.generic_u8string(), selection, manifest, error),
          "deduplicated cyclic group moves together");
    Check(!std::filesystem::exists(first) && !std::filesystem::exists(second), "group deletion moves every selected asset");
    Check(AssetTrash::Restore(project.resources, manifest, error) && PlatformFile::ReadAllText(first) == "a\n" &&
              PlatformFile::ReadAllText(second) == "b\n",
          "group restore recovers every selected asset");
}
void FullRegistryReferences()
{
    // A graph display budget must never truncate deletion safety checks.
    TemporaryProject project;
    const auto target = project.assets / "target.mesh";
    project.Write(target);
    for (int i = 0; i < 150; ++i)
        project.Write(project.assets / ("Owners/" + std::to_string(i) + ".bin"));
    project.Scan();
    auto &registry = project.resources.GetAssetRegistry();
    const auto guid = registry.Find(target.generic_u8string())->id;
    for (int i = 0; i < 150; ++i)
        Check(registry.SetDependencies((project.assets / ("Owners/" + std::to_string(i) + ".bin")).generic_u8string(), {guid}),
              "persist full registry fixture");
    Check(AssetTrash::FindReferences(project.resources, {target.generic_u8string()}).size() == 150,
          "reference safety scans all owners beyond graph visualization node budget");
}
void MalformedOwnerBlocksDeletion()
{
    // Truncated typed serialization must not silently hide later incoming references.
    TemporaryProject project;
    const auto target = project.assets / "Models/item.mesh", owner = project.assets / "broken.scene";
    project.Write(target);
    project.Write(owner, "TINYSCENE 11\nmesh \"assets/Models/item.mesh\n");
    project.Scan();
    Check(Contains(AssetTrash::FindReferences(project.resources, {target.generic_u8string()}), target, owner),
          "malformed registered scene is a conservative deletion blocker");
    std::string manifest, error;
    Check(!AssetTrash::MoveToTrash(project.resources, project.root.generic_u8string(), {target.generic_u8string()}, manifest, error) &&
              !error.empty() && std::filesystem::exists(target),
          "malformed owner blocks mutation before trash creation");
}
void RestoreCollision()
{
    // Restoring one member before discovering a later collision would split the trash group.
    TemporaryProject project;
    const auto first = project.assets / "a.mesh", second = project.assets / "b.mesh";
    project.Write(first, "original a\n");
    project.Write(second, "original b\n");
    project.Scan();
    std::string manifest, error;
    Check(AssetTrash::MoveToTrash(project.resources, project.root.generic_u8string(), {first.generic_u8string(), second.generic_u8string()},
                                  manifest, error),
          "prepare group for collision restore");
    project.Write(second, "new occupant\n");
    const auto manifestBytes = PlatformFile::ReadAllText(std::filesystem::u8path(manifest));
    Check(!AssetTrash::Restore(project.resources, manifest, error) && !error.empty(), "restore rejects an occupied original destination");
    Check(!std::filesystem::exists(first) && PlatformFile::ReadAllText(second) == "new occupant\n" &&
              PlatformFile::ReadAllText(std::filesystem::u8path(manifest)) == manifestBytes,
          "collision leaves originals and entire trash group unchanged");
    std::filesystem::remove(second);
    Check(AssetTrash::Restore(project.resources, manifest, error) && PlatformFile::ReadAllText(first) == "original a\n" &&
              PlatformFile::ReadAllText(second) == "original b\n",
          "collision retry still recovers original group bytes");
}
void BoundaryGuards()
{
    // Invalid or stale selections must fail before a valid earlier group member is moved.
    TemporaryProject project;
    const auto valid = project.assets / "valid.mesh", outside = project.root / "outside.mesh", unregistered = project.assets / "new.mesh";
    project.Write(valid);
    project.Write(outside);
    project.Scan();
    project.Write(unregistered);
    std::string manifest, error;
    for (const auto &path : {outside, project.assets, unregistered, project.assets / "missing.mesh"})
    {
        Check(!AssetTrash::MoveToTrash(project.resources, project.root.generic_u8string(),
                                       {valid.generic_u8string(), path.generic_u8string()}, manifest, error) &&
                  !error.empty() && std::filesystem::exists(valid) && std::filesystem::exists(outside),
              "out-of-root, root, missing and unregistered selections leave group intact");
    }
    Check(!AssetTrash::MoveToTrash(project.resources, project.root.generic_u8string(), {}, manifest, error) && !error.empty(),
          "empty selection is rejected");
}
void ReparseGuards()
{
    // A symlinked file/ancestor must never permit trash to mutate the link target outside asset root.
    TemporaryProject project;
    const auto external = project.root / "External", linked = project.assets / "Linked";
    project.Write(external / "keep.mesh", "outside bytes\n");
    std::error_code errorCode;
    std::filesystem::create_directory_symlink(external, linked, errorCode);
    if (errorCode)
    {
        std::cout << "SKIP: symlink creation unavailable: " << errorCode.message() << '\n';
        return;
    }
    project.Scan();
    std::string manifest, error;
    Check(!AssetTrash::MoveToTrash(project.resources, project.root.generic_u8string(), {(linked / "keep.mesh").generic_u8string()},
                                   manifest, error) &&
              !error.empty() && PlatformFile::ReadAllText(external / "keep.mesh") == "outside bytes\n",
          "reject symlink ancestor without changing outside target");
    const auto trashLink = project.root / "Saved";
    std::filesystem::create_directory_symlink(external, trashLink, errorCode);
    if (!errorCode)
    {
        project.Write(project.assets / "safe.mesh");
        project.resources.GetAssetRegistry().Scan(project.assets);
        Check(!AssetTrash::MoveToTrash(project.resources, project.root.generic_u8string(),
                                       {(project.assets / "safe.mesh").generic_u8string()}, manifest, error) &&
                  std::filesystem::exists(project.assets / "safe.mesh"),
              "reject symlinked project trash destination");
    }
}
#ifdef _WIN32
void MutationFailureRollback()
{
    // A real sharing violation on a later sidecar must roll the earlier asset moves back.
    TemporaryProject project;
    const auto first = project.assets / "a.mesh", second = project.assets / "b.mesh";
    project.Write(first, "first\n");
    project.Write(second, "second\n");
    project.Scan();
    const auto metadata = std::filesystem::u8path(second.generic_u8string() + ".meta");
    const auto id = project.resources.GetAssetRegistry().Find(first.generic_u8string())->id;
    const HANDLE locked =
        CreateFileW(metadata.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "lock selected metadata against filesystem rename");
    std::string manifest, error;
    const bool moved = AssetTrash::MoveToTrash(project.resources, project.root.generic_u8string(),
                                               {first.generic_u8string(), second.generic_u8string()}, manifest, error);
    CloseHandle(locked);
    Check(!moved && !error.empty() && manifest.empty(), "mutation failure returns no successful trash manifest");
    Check(PlatformFile::ReadAllText(first) == "first\n" && PlatformFile::ReadAllText(second) == "second\n" &&
              std::filesystem::exists(std::filesystem::u8path(first.generic_u8string() + ".meta")) &&
              project.resources.GetAssetRegistry().FindById(id),
          "failed group deletion restores asset bytes, sidecars and registry GUID");
}
#endif
} // namespace
} // namespace MiniEngine

int main()
{
    using namespace MiniEngine;
    const std::vector<std::pair<const char *, void (*)()>> tests = {
        {"round trip Unicode", RoundTripUnicode},
        {"portable references", PortableReferences},
        {"serialized references", SerializedReferences},
        {"selected internal references", SelectedInternalReferences},
        {"full registry references", FullRegistryReferences},
        {"malformed owner", MalformedOwnerBlocksDeletion},
        {"restore collision", RestoreCollision},
        {"boundary guards", BoundaryGuards},
        {"reparse guards", ReparseGuards},
#ifdef _WIN32
        {"mutation failure rollback", MutationFailureRollback},
#endif
    };
    int failures = 0;
    for (const auto &test : tests)
    {
        try
        {
            test.second();
            std::cout << "PASS: " << test.first << '\n';
        }
        catch (const std::exception &error)
        {
            ++failures;
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
