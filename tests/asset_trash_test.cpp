#include "asset_trash.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/resource/resource_manager.h"

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

namespace tinyengine
{
namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
struct TemporaryProject
{
    std::filesystem::path root =
        std::filesystem::weakly_canonical(std::filesystem::absolute("saved")) /
        ("AssetTrash_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path assets = root / "Content/Game";
    ResourceManager resources;
    TemporaryProject()
    {
        std::filesystem::create_directories(assets);
    }
    ~TemporaryProject()
    {
        resources.shutdown();
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void scan()
    {
        resources.initialize(nullptr, assets, "Content/Game");
    }
    void write(const std::filesystem::path& path, const std::string& text = "asset payload\n")
    {
        check(PlatformFile::writeAllText(path, text), "write real asset fixture");
    }
};
bool contains(const std::vector<AssetTrashReference>& references, const std::filesystem::path& target,
              const std::filesystem::path& owner)
{
    return std::any_of(references.begin(), references.end(),
                       [&](const auto& reference) {
                           return reference.target == toUtf8GenericPath(target) &&
                                  reference.owner == toUtf8GenericPath(owner);
                       });
}
void RoundTripUnicode()
{
    // Losing a sidecar, GUID, content, UTF-8 path or full-root registry scan breaks recovery.
    TemporaryProject project;
    const auto asset = project.assets / fromUtf8Path("模型/茶杯.mesh");
    const auto unrelated = project.assets / "Elsewhere/keep.mesh";
    project.write(asset, "original unicode asset\n");
    project.write(unrelated);
    project.scan();
    const auto original = project.resources.getAssetRegistry().Find(toUtf8GenericPath(asset));
    check(original.has_value(), "source is registered before deletion");
    const auto metadata = PlatformFile::readAllText(fromUtf8Path(toUtf8GenericPath(asset) + ".meta"));
    std::string manifest, error;
    check(AssetTrash::moveToTrash(project.resources, toUtf8GenericPath(project.root), {toUtf8GenericPath(asset)},
                                  manifest, error),
          "unreferenced Unicode asset moves to project trash");
    check(!manifest.empty() && std::filesystem::is_regular_file(fromUtf8Path(manifest)),
          "trash writes a durable manifest");
    check(toUtf8GenericPath(fromUtf8Path(manifest).lexically_relative(project.root / "saved/DeletedAssets"))
                  .rfind("..", 0) != 0,
          "manifest is inside project saved/DeletedAssets");
    check(!std::filesystem::exists(asset) && !std::filesystem::exists(fromUtf8Path(toUtf8GenericPath(asset) + ".meta")),
          "deletion moves asset and metadata out of asset root");
    check(!project.resources.getAssetRegistry().FindById(original->id) &&
              project.resources.getAssetRegistry().Find(toUtf8GenericPath(unrelated)),
          "trash rescan removes deleted GUID and retains unrelated folders");
    check(AssetTrash::Restore(project.resources, manifest, error), "manifest restores deleted asset");
    check(PlatformFile::readAllText(asset) == "original unicode asset\n" &&
              PlatformFile::readAllText(fromUtf8Path(toUtf8GenericPath(asset) + ".meta")) == metadata,
          "restore preserves asset and metadata bytes");
    const auto restored = project.resources.getAssetRegistry().FindById(original->id);
    check(restored && restored->path == toUtf8GenericPath(asset), "restore rescans original GUID at original path");
    check(!AssetTrash::Restore(project.resources, manifest, error) && !error.empty(),
          "already restored manifest cannot replay deletion");
}
void PortableReferences()
{
    // Resolving only raw registry paths would permit deleting assets referenced through portable paths or GUIDs.
    TemporaryProject project;
    const auto target = project.assets / "Models/item.mesh";
    project.write(target);
    std::vector<std::filesystem::path> owners;
    for (int i = 0; i < 7; ++i)
    {
        owners.push_back(project.assets / ("Owners/owner" + std::to_string(i) + ".bin"));
        project.write(owners.back());
    }
    project.scan();
    auto& registry = project.resources.getAssetRegistry();
    const auto guid = registry.Find(toUtf8GenericPath(target))->id;
    const std::vector<std::string> references = {guid,
                                                 "../Models/item.mesh",
                                                 "Models/item.mesh",
                                                 "assets/Models/item.mesh",
                                                 "Game/Models/item.mesh",
                                                 "Content/Game/Models/item.mesh",
                                                 toUtf8GenericPath(target)};
    for (std::size_t i = 0; i < owners.size(); ++i)
        check(registry.setDependencies(toUtf8GenericPath(owners[i]), {references[i]}),
              "persist real dependency metadata");
    const auto found = AssetTrash::FindReferences(project.resources, {toUtf8GenericPath(target)});
    for (const auto& owner : owners)
        check(contains(found, target, owner), "find incoming GUID and portable path references");
    check(found.size() == owners.size(), "deduplicate incoming references by owner and target");
    std::string manifest, error;
    check(!AssetTrash::moveToTrash(project.resources, toUtf8GenericPath(project.root), {toUtf8GenericPath(target)},
                                   manifest, error) &&
              !error.empty() && std::filesystem::exists(target),
          "incoming references block deletion before mutation");
}
void SerializedReferences()
{
    // Scene/prefab and shader references are intentionally absent from registry dependency metadata.
    TemporaryProject project;
    const auto target = project.assets / "Models/item.mesh", include = project.assets / "Shaders/shared.glsl";
    project.write(target);
    project.write(include, "vec3 tint;\n");
    project.scan();
    const auto guid = project.resources.getAssetRegistry().Find(toUtf8GenericPath(target))->id;
    const std::vector<std::pair<std::string, std::string>> scenes = {{"Scenes/owner.scene", "assets/Models/item.mesh"},
                                                                     {"Prefabs/owner.prefab", "../Models/item.mesh"},
                                                                     {"Scenes/guid.scene", guid}};
    for (const auto& fixture : scenes)
    {
        World world;
        const auto entity = world.createEntity("mesh owner");
        world.AddComponent(entity, MeshComponent{fixture.second, {}});
        check(SceneSerializer::Save(world, project.assets / fixture.first), "save real scene or prefab serialization");
    }
    const auto shader = project.assets / "Shaders/effect.frag", comments = project.assets / "Shaders/comments.frag";
    project.write(shader, "#version 330 core\n#include \"shared.glsl\"\nvoid main() {}\n");
    project.write(comments, "// #include \"shared.glsl\"\n/* #include \"shared.glsl\" */\n");
    World labels;
    const auto label = labels.createEntity("assets/Models/item.mesh");
    (void)label;
    check(SceneSerializer::Save(labels, project.assets / "Scenes/labels.scene"),
          "save unrelated matching display label");
    project.resources.getAssetRegistry().scan(project.assets);
    const auto found =
        AssetTrash::FindReferences(project.resources, {toUtf8GenericPath(target), toUtf8GenericPath(include)});
    for (const auto& fixture : scenes)
        check(contains(found, target, project.assets / fixture.first),
              "detect typed serialized scene and prefab paths including GUID");
    check(contains(found, include, shader) && found.size() == 4,
          "detect real shader include while ignoring comments and entity labels");
    check(PlatformFile::readAllText(shader).find("shared.glsl") != std::string::npos,
          "reference discovery leaves source files unchanged");
}
void SelectedInternalReferences()
{
    // Internal references must not prevent deleting and recovering a coherent selection as a group.
    TemporaryProject project;
    const auto first = project.assets / "a.bin", second = project.assets / "b.bin";
    project.write(first, "a\n");
    project.write(second, "b\n");
    project.scan();
    auto& registry = project.resources.getAssetRegistry();
    check(registry.setDependencies(toUtf8GenericPath(first), {registry.Find(toUtf8GenericPath(second))->id}) &&
              registry.setDependencies(toUtf8GenericPath(second), {toUtf8GenericPath(first)}),
          "persist a cyclic selected group");
    const std::vector<std::string> selection = {toUtf8GenericPath(first), toUtf8GenericPath(second),
                                                toUtf8GenericPath(first)};
    check(AssetTrash::FindReferences(project.resources, selection).empty(),
          "selected internal references are not external blockers");
    std::string manifest, error;
    check(AssetTrash::moveToTrash(project.resources, toUtf8GenericPath(project.root), selection, manifest, error),
          "deduplicated cyclic group moves together");
    check(!std::filesystem::exists(first) && !std::filesystem::exists(second),
          "group deletion moves every selected asset");
    check(AssetTrash::Restore(project.resources, manifest, error) && PlatformFile::readAllText(first) == "a\n" &&
              PlatformFile::readAllText(second) == "b\n",
          "group restore recovers every selected asset");
}
void FullRegistryReferences()
{
    // A graph display budget must never truncate deletion safety checks.
    TemporaryProject project;
    const auto target = project.assets / "target.mesh";
    project.write(target);
    for (int i = 0; i < 150; ++i)
        project.write(project.assets / ("Owners/" + std::to_string(i) + ".bin"));
    project.scan();
    auto& registry = project.resources.getAssetRegistry();
    const auto guid = registry.Find(toUtf8GenericPath(target))->id;
    for (int i = 0; i < 150; ++i)
        check(registry.setDependencies(toUtf8GenericPath((project.assets / ("Owners/" + std::to_string(i) + ".bin"))),
                                       {guid}),
              "persist full registry fixture");
    check(AssetTrash::FindReferences(project.resources, {toUtf8GenericPath(target)}).size() == 150,
          "reference safety scans all owners beyond graph visualization node budget");
}
void MalformedOwnerBlocksDeletion()
{
    // Truncated typed serialization must not silently hide later incoming references.
    TemporaryProject project;
    const auto target = project.assets / "Models/item.mesh", owner = project.assets / "broken.scene";
    project.write(target);
    project.write(owner, "TINYSCENE 11\nmesh \"assets/Models/item.mesh\n");
    project.scan();
    check(contains(AssetTrash::FindReferences(project.resources, {toUtf8GenericPath(target)}), target, owner),
          "malformed registered scene is a conservative deletion blocker");
    std::string manifest, error;
    check(!AssetTrash::moveToTrash(project.resources, toUtf8GenericPath(project.root), {toUtf8GenericPath(target)},
                                   manifest, error) &&
              !error.empty() && std::filesystem::exists(target),
          "malformed owner blocks mutation before trash creation");
}
void RestoreCollision()
{
    // Restoring one member before discovering a later collision would split the trash group.
    TemporaryProject project;
    const auto first = project.assets / "a.mesh", second = project.assets / "b.mesh";
    project.write(first, "original a\n");
    project.write(second, "original b\n");
    project.scan();
    std::string manifest, error;
    check(AssetTrash::moveToTrash(project.resources, toUtf8GenericPath(project.root),
                                  {toUtf8GenericPath(first), toUtf8GenericPath(second)}, manifest, error),
          "prepare group for collision restore");
    project.write(second, "new occupant\n");
    const auto manifestBytes = PlatformFile::readAllText(fromUtf8Path(manifest));
    check(!AssetTrash::Restore(project.resources, manifest, error) && !error.empty(),
          "restore rejects an occupied original destination");
    check(!std::filesystem::exists(first) && PlatformFile::readAllText(second) == "new occupant\n" &&
              PlatformFile::readAllText(fromUtf8Path(manifest)) == manifestBytes,
          "collision leaves originals and entire trash group unchanged");
    std::filesystem::remove(second);
    check(AssetTrash::Restore(project.resources, manifest, error) &&
              PlatformFile::readAllText(first) == "original a\n" && PlatformFile::readAllText(second) == "original b\n",
          "collision retry still recovers original group bytes");
}
void BoundaryGuards()
{
    // Invalid or stale selections must fail before a valid earlier group member is moved.
    TemporaryProject project;
    const auto valid = project.assets / "valid.mesh", outside = project.root / "outside.mesh",
               unregistered = project.assets / "new.mesh";
    project.write(valid);
    project.write(outside);
    project.scan();
    project.write(unregistered);
    std::string manifest, error;
    for (const auto& path : {outside, project.assets, unregistered, project.assets / "missing.mesh"})
    {
        check(!AssetTrash::moveToTrash(project.resources, toUtf8GenericPath(project.root),
                                       {toUtf8GenericPath(valid), toUtf8GenericPath(path)}, manifest, error) &&
                  !error.empty() && std::filesystem::exists(valid) && std::filesystem::exists(outside),
              "out-of-root, root, missing and unregistered selections leave group intact");
    }
    check(!AssetTrash::moveToTrash(project.resources, toUtf8GenericPath(project.root), {}, manifest, error) &&
              !error.empty(),
          "empty selection is rejected");
}
bool createDirectoryLink(const std::filesystem::path& target, const std::filesystem::path& link, std::string& reason)
{
    // Windows may report success without materialising a reparse point when the process lacks the
    // privilege, so verify the result instead of trusting the error code alone.
    std::error_code errorCode;
    std::filesystem::create_directory_symlink(target, link, errorCode);
    if (!errorCode && std::filesystem::is_symlink(std::filesystem::symlink_status(link, errorCode)) && !errorCode)
        return true;
    reason = errorCode ? errorCode.message() : std::string("symbolic link was not created");
    return false;
}
void ReparseGuards()
{
    // A symlinked file/ancestor must never permit trash to mutate the link target outside asset root.
    TemporaryProject project;
    const auto external = project.root / "External", linked = project.assets / "Linked";
    project.write(external / "keep.mesh", "outside bytes\n");
    std::string skip;
    if (!createDirectoryLink(external, linked, skip))
    {
        std::cout << "SKIP: symlink creation unavailable: " << skip << '\n';
        return;
    }
    project.scan();
    std::string manifest, error;
    check(!AssetTrash::moveToTrash(project.resources, toUtf8GenericPath(project.root),
                                   {toUtf8GenericPath((linked / "keep.mesh"))}, manifest, error) &&
              !error.empty() && PlatformFile::readAllText(external / "keep.mesh") == "outside bytes\n",
          "reject symlink ancestor without changing outside target");
    const auto trashLink = project.root / "saved";
    if (!createDirectoryLink(external, trashLink, skip))
    {
        std::cout << "SKIP: trash destination symlink unavailable: " << skip << '\n';
        return;
    }
    project.write(project.assets / "safe.mesh");
    project.resources.getAssetRegistry().scan(project.assets);
    check(!AssetTrash::moveToTrash(project.resources, toUtf8GenericPath(project.root),
                                   {toUtf8GenericPath((project.assets / "safe.mesh"))}, manifest, error) &&
              std::filesystem::exists(project.assets / "safe.mesh"),
          "reject symlinked project trash destination");
}
#ifdef _WIN32
void MutationFailureRollback()
{
    // A real sharing violation on a later sidecar must roll the earlier asset moves back.
    TemporaryProject project;
    const auto first = project.assets / "a.mesh", second = project.assets / "b.mesh";
    project.write(first, "first\n");
    project.write(second, "second\n");
    project.scan();
    const auto metadata = fromUtf8Path(toUtf8GenericPath(second) + ".meta");
    const auto id = project.resources.getAssetRegistry().Find(toUtf8GenericPath(first))->id;
    const HANDLE locked = CreateFileW(metadata.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "lock selected metadata against filesystem rename");
    std::string manifest, error;
    const bool moved = AssetTrash::moveToTrash(project.resources, toUtf8GenericPath(project.root),
                                               {toUtf8GenericPath(first), toUtf8GenericPath(second)}, manifest, error);
    CloseHandle(locked);
    check(!moved && !error.empty() && manifest.empty(), "mutation failure returns no successful trash manifest");
    check(PlatformFile::readAllText(first) == "first\n" && PlatformFile::readAllText(second) == "second\n" &&
              std::filesystem::exists(fromUtf8Path(toUtf8GenericPath(first) + ".meta")) &&
              project.resources.getAssetRegistry().FindById(id),
          "failed group deletion restores asset bytes, sidecars and registry GUID");
}
#endif
} // namespace
} // namespace tinyengine

int main()
{
    using namespace tinyengine;
    const std::vector<std::pair<const char*, void (*)()>> tests = {
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
    for (const auto& test : tests)
    {
        try
        {
            test.second();
            std::cout << "PASS: " << test.first << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? 0 : 1;
}
