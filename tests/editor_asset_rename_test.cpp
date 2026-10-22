#include "editor_app.h"
#include "tinyengine/core/config.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/render/shader_pipeline.h"
#include "tinyengine/resource/asset_maintenance.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
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
    std::cout << "PASS: " << message << '\n';
}
struct TemporaryProject
{
    std::filesystem::path root =
        std::filesystem::weakly_canonical(std::filesystem::absolute("saved")) /
        ("EditorAssetRename_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryProject()
    {
        std::filesystem::create_directories(root / "assets/Models");
    }
    ~TemporaryProject()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
Entity addMesh(World& world, const std::string& name, const std::string& path)
{
    const auto entity = world.createEntity(name);
    world.AddComponent(entity, MeshComponent{path, {}});
    return entity;
}
const MeshComponent* findMesh(const World& world, const std::string& name)
{
    for (const auto entity : world.GetEntityManager().GetAliveEntities())
        if (const auto* component = world.GetComponent<NameComponent>(entity); component && component->name == name)
            return world.GetComponent<MeshComponent>(entity);
    return nullptr;
}
void shaderIncludeRename(bool moveOwner)
{
    // Renaming an included file or relocating its owner must keep the real preprocessor dependency graph valid.
    TemporaryProject project;
    const auto assets = project.root / "assets", shaders = assets / "Shaders";
    const auto included = shaders / "shared.glsl", owner = shaders / "effect.frag";
    const std::string comments = "// #include \"shared.glsl\"\n// Keep unrelated text: shared.glsl\n";
    PlatformFile::writeAllText(included, "vec3 SharedColor() { return vec3(1.0); }\n");
    PlatformFile::writeAllText(owner, "#version 330 core\n  #include \"shared.glsl\" // preserve this comment\n// "
                                      "display label: \"shared.glsl\"\nvoid main() {}\n");
    PlatformFile::writeAllText(shaders / "comments.frag", comments);
    check(ShaderPipeline::preprocess(owner).success, "shader include fixture preprocesses before rename");
    AssetRegistry registry;
    registry.scan(assets);
    const auto source = moveOwner ? owner : included;
    const auto destination = assets / (moveOwner ? "Moved/effect.frag" : "Library/renamed.glsl");
    std::string error;
    check(AssetMaintenance::moveAsset(registry, source, destination, &error), "shader move transaction succeeds");
    const auto preprocessed = ShaderPipeline::preprocess(moveOwner ? destination : owner);
    check(preprocessed.success && preprocessed.source.find("SharedColor") != std::string::npos,
          moveOwner ? "moving shader owner repairs its outgoing relative includes"
                    : "renaming included shader repairs incoming owner-relative includes");
    const auto text = PlatformFile::readAllText(moveOwner ? destination : owner);
    check(text.find("// preserve this comment") != std::string::npos &&
              text.find("// display label: \"shared.glsl\"") != std::string::npos &&
              PlatformFile::readAllText(shaders / "comments.frag") == comments,
          "shader migration preserves comments and unrelated text");
}
void backendPortableReferences()
{
    // Removing portable-reference migration must leave these real scene/dependency paths stale.
    TemporaryProject project;
    const auto assets = project.root / "assets";
    const auto source = assets / "Models/a.mesh", destination = assets / "Moved/renamed.mesh";
    const auto scene = assets / "main.scene";
    PlatformFile::writeAllText(source, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    World world;
    addMesh(world, "assets/Models/a.mesh", "assets/Models/a.mesh");
    addMesh(world, "asset relative", "Models/a.mesh");
    addMesh(world, "unrelated substring", "assets/Models/a.mesh.backup");
    check(SceneSerializer::Save(world, scene), "write real portable-reference scene fixture");
    AssetRegistry registry;
    registry.scan(assets);
    const auto before = registry.Find(toUtf8GenericPath(source));
    check(before.has_value(), "mesh has a stable registered GUID");
    registry.setDependencies(toUtf8GenericPath(scene),
                             {"assets/Models/a.mesh", "Models/a.mesh", toUtf8GenericPath(source), before->id});
    std::string error;
    const bool moved = AssetMaintenance::moveAsset(registry, source, destination, &error);
    if (!moved)
        std::cerr << "Mesh rename: " << error << '\n';
    check(moved, "move mesh and metadata transaction");
    World loaded;
    check(SceneSerializer::load(loaded, scene), "migrated scene remains loadable");
    check(findMesh(loaded, "assets/Models/a.mesh") &&
              findMesh(loaded, "assets/Models/a.mesh")->meshPath == "assets/Moved/renamed.mesh",
          "project-relative scene reference follows rename without changing entity name");
    check(findMesh(loaded, "asset relative") && findMesh(loaded, "asset relative")->meshPath == "Moved/renamed.mesh",
          "asset-root-relative scene reference follows rename");
    check(findMesh(loaded, "unrelated substring")->meshPath == "assets/Models/a.mesh.backup",
          "rename does not rewrite a different path containing the old name");
    const auto owner = registry.Find(toUtf8GenericPath(scene));
    check(owner && owner->dependencies == std::vector<std::string>{"assets/Moved/renamed.mesh", "Moved/renamed.mesh",
                                                                   toUtf8GenericPath(destination), before->id},
          "portable dependency styles migrate while GUID dependencies remain stable");
    check(registry.FindById(before->id) && registry.FindById(before->id)->path == toUtf8GenericPath(destination) &&
              !std::filesystem::exists(source) && !std::filesystem::exists(toUtf8GenericPath(source) + ".meta") &&
              std::filesystem::exists(toUtf8GenericPath(destination) + ".meta"),
          "rename preserves GUID and moves the metadata sidecar");
}
#ifdef _WIN32
void backendWriteFailureRollback()
{
    // A denied dependent-file replacement must roll back the asset, metadata and earlier writes.
    TemporaryProject project;
    const auto assets = project.root / "assets";
    const auto source = assets / "Models/a.mesh", destination = assets / "Moved/renamed.mesh";
    const auto scene = assets / "locked.scene", other = assets / "other.scene";
    PlatformFile::writeAllText(source, "mesh payload\n");
    World world;
    addMesh(world, "Dependent", "assets/Models/a.mesh");
    SceneSerializer::Save(world, scene);
    SceneSerializer::Save(world, other);
    AssetRegistry registry;
    registry.scan(assets);
    const auto guid = registry.Find(toUtf8GenericPath(source))->id;
    const auto oldScene = PlatformFile::readAllText(scene), oldOther = PlatformFile::readAllText(other);
    const auto oldMetadata = PlatformFile::readAllText(toUtf8GenericPath(source) + ".meta");
    const HANDLE locked = CreateFileW(scene.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    check(locked != INVALID_HANDLE_VALUE, "lock a real dependent file against replacement");
    std::string error;
    const bool moved = AssetMaintenance::moveAsset(registry, source, destination, &error);
    CloseHandle(locked);
    check(!moved && !error.empty() && std::filesystem::exists(source) && !std::filesystem::exists(destination) &&
              PlatformFile::readAllText(toUtf8GenericPath(source) + ".meta") == oldMetadata &&
              PlatformFile::readAllText(scene) == oldScene && PlatformFile::readAllText(other) == oldOther &&
              registry.FindById(guid) && registry.FindById(guid)->path == toUtf8GenericPath(source),
          "dependent write failure rolls back files, metadata, registry and earlier reference edits");
}
#endif
} // namespace

struct EditorInteractionTests
{
    void openShaderIncludeRename()
    {
        // A dirty dependent must never be overwritten; a clean editor must receive the rewritten disk baseline.
        TemporaryProject project;
        const auto assets = project.root / "assets", included = assets / "Shaders/shared.glsl",
                   owner = assets / "Shaders/effect.frag";
        const auto destination = assets / "Library/renamed.glsl";
        const std::string original = "#version 330 core\n#include \"shared.glsl\"\nvoid main() {}\n";
        PlatformFile::writeAllText(included, "vec3 SharedColor() { return vec3(1.0); }\n");
        PlatformFile::writeAllText(owner, original);
        ResourceManager resources;
        resources.initialize(nullptr, assets);
        World world;
        EditorApp app;
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.world = &world;
        context.resourceManager = &resources;
        app.m_engine.GetSceneManager().setWorld(&world);
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        settings.projectFile = project.root / "Rename.tinyproject";
        settings.assetDirectory = "assets";
        app.m_openShaderAsset = toUtf8GenericPath(owner);
        app.m_savedShaderSource = original;
        const auto draft = original + "// unsaved edit\n";
        app.m_shaderSource.assign(draft.begin(), draft.end());
        app.m_shaderSource.push_back('\0');
        std::string error;
        check(!app.moveEditorAsset(toUtf8GenericPath(included), destination, error) && !error.empty() &&
                  std::filesystem::exists(included) && !std::filesystem::exists(destination) &&
                  PlatformFile::readAllText(owner) == original && std::string(app.m_shaderSource.data()) == draft,
              "dirty open dependent shader blocks rename before files or drafts change");
        app.m_shaderSource.assign(original.begin(), original.end());
        app.m_shaderSource.push_back('\0');
        const auto revision = app.m_shaderDocumentRevision;
        check(app.moveEditorAsset(toUtf8GenericPath(included), destination, error),
              "rename include with clean dependent editor");
        check(app.m_savedShaderSource == PlatformFile::readAllText(owner) &&
                  std::string(app.m_shaderSource.data()) == app.m_savedShaderSource &&
                  app.m_savedShaderSource.find("../Library/renamed.glsl") != std::string::npos &&
                  app.m_shaderDocumentRevision > revision,
              "clean dependent shader buffer and saved baseline follow incoming include rewrite");
        const auto movedOwner = assets / "Moved/effect.frag";
        check(app.moveEditorAsset(toUtf8GenericPath(owner), movedOwner, error) &&
                  app.m_openShaderAsset == toUtf8GenericPath(movedOwner) &&
                  app.m_savedShaderSource == PlatformFile::readAllText(movedOwner) &&
                  std::string(app.m_shaderSource.data()) == app.m_savedShaderSource &&
                  ShaderPipeline::preprocess(movedOwner).success,
              "clean renamed shader owner reloads its rebased includes without stale save text");
        resources.shutdown();
        context.resourceManager = nullptr;
        context.world = nullptr;
    }
    void configuredProjectReferences(bool deniedWrite)
    {
        // Dropping project-file or staged settings migration breaks restart or violates failed-move atomicity.
        TemporaryProject project;
        const auto assets = project.root / "assets", projectFile = project.root / "Rename.tinyproject";
        World world;
        const auto entity = addMesh(world, "Project world", "assets/Models/a.mesh");
        world.GetComponent<MeshComponent>(entity)->materialPath = "assets/textureOwner.mat";
        PlatformFile::writeAllText(assets / "Models/a.mesh", "mesh payload\n");
        check(SceneSerializer::Save(world, assets / "start.scene"), "write startup scene fixture");
        PlatformFile::writeAllText(assets / "start.input", "bindings fixture\n");
        PlatformFile::writeAllText(assets / "sky.ppm", "P3\n1 1\n255\n128 128 128\n");
        PlatformFile::writeAllText(assets / "textureOwner.mat", "texture \"assets/sky.ppm\"\n");
        ResourceManager resources;
        resources.initialize(nullptr, assets);
        EditorApp app;
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.world = &world;
        context.resourceManager = &resources;
        app.m_engine.GetSceneManager().setWorld(&world);
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        settings.projectFile = projectFile;
        settings.assetDirectory = "assets";
        settings.startupScene = "assets/start.scene";
        settings.inputBindings = "assets/start.input";
        settings.environmentTexture = "assets/sky.ppm";
        check(settings.Save(projectFile), "write real configured project file");
        PlatformFile::writeAllText(projectFile,
                                   PlatformFile::readAllText(projectFile) + "custom.preserve=assets/sky.ppm\n");
        const auto snapshot = SceneSerializer::serialize(world);
        app.m_sceneDocuments = {{assets / "start.scene", snapshot, false, {snapshot}, {snapshot}}};
        app.m_undo = {snapshot};
        app.m_redo = {snapshot};
        std::string error;
        if (deniedWrite)
        {
#ifdef _WIN32
            const auto source = assets / "sky.ppm", destination = assets / "Moved/sky.ppm";
            const auto oldConfig = PlatformFile::readAllText(projectFile),
                       oldOwner = PlatformFile::readAllText(assets / "textureOwner.mat");
            const auto oldMeta = PlatformFile::readAllText(toUtf8GenericPath(source) + ".meta");
            const auto guid = resources.getAssetRegistry().Find(toUtf8GenericPath(source))->id;
            const HANDLE locked = CreateFileW(projectFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            check(locked != INVALID_HANDLE_VALUE, "lock real project file against transaction replacement");
            const bool moved = app.moveEditorAsset(toUtf8GenericPath(source), destination, error);
            CloseHandle(locked);
            check(!moved && !error.empty(), "denied project write rejects the entire asset rename");
            check(std::filesystem::exists(source) && !std::filesystem::exists(destination) &&
                      PlatformFile::readAllText(toUtf8GenericPath(source) + ".meta") == oldMeta &&
                      PlatformFile::readAllText(projectFile) == oldConfig &&
                      PlatformFile::readAllText(assets / "textureOwner.mat") == oldOwner &&
                      settings.environmentTexture == "assets/sky.ppm" &&
                      SceneSerializer::serialize(world) == snapshot &&
                      app.m_undo == std::vector<std::string>{snapshot} &&
                      resources.getAssetRegistry().FindById(guid)->path == toUtf8GenericPath(source),
                  "project-write failure restores files, references, GUID, settings and editor histories");
#endif
        }
        else
        {
            check(app.moveEditorAsset(toUtf8GenericPath((assets / "start.scene")), assets / "Moved/start.scene", error),
                  "rename configured startup scene");
            check(settings.startupScene == "assets/Moved/start.scene", "live startup scene setting follows rename");
            check(app.moveEditorAsset(toUtf8GenericPath((assets / "start.input")), assets / "Moved/start.input", error),
                  "rename configured input bindings");
            check(settings.inputBindings == "assets/Moved/start.input", "live input bindings setting follows rename");
            const bool movedEnvironment =
                app.moveEditorAsset(toUtf8GenericPath((assets / "sky.ppm")), assets / "Moved/sky.ppm", error);
            if (!movedEnvironment)
                std::cerr << "Environment rename: " << error << '\n';
            check(movedEnvironment, "rename configured environment texture");
            check(settings.environmentTexture == "assets/Moved/sky.ppm",
                  "live environment texture setting follows rename");
            ProjectSettings reloaded;
            check(reloaded.load(projectFile) && reloaded.startupScene == "assets/Moved/start.scene" &&
                      reloaded.inputBindings == "assets/Moved/start.input" &&
                      reloaded.environmentTexture == "assets/Moved/sky.ppm",
                  "portable configured references survive a project restart");
            Config config;
            check(config.load(toUtf8GenericPath(projectFile)) &&
                      config.getString("custom.preserve") == "assets/sky.ppm",
                  "project migration preserves unknown configuration keys");
        }
        resources.shutdown();
        context.resourceManager = nullptr;
        context.world = nullptr;
    }
    void portableLiveWorldAndHistories()
    {
        // Omitting any editor snapshot migration must allow Undo or an inactive tab to resurrect the old path.
        TemporaryProject project;
        const auto assets = project.root / "assets";
        const auto source = assets / "Models/a.mesh", destination = assets / "Moved/renamed.mesh";
        const auto scenePath = assets / "active.scene", inactivePath = assets / "inactive.scene";
        PlatformFile::writeAllText(source, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        World world;
        const auto entity = addMesh(world, "Keep handle", "assets/Models/a.mesh");
        addMesh(world, "assets/Models/a.mesh", "Models/a.mesh");
        auto snapshot = SceneSerializer::serialize(world);
        check(SceneSerializer::Save(world, scenePath) && SceneSerializer::Save(world, inactivePath),
              "write active and inactive scene fixtures");
        ResourceManager resources;
        resources.initialize(nullptr, assets);
        EditorApp app;
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.world = &world;
        context.resourceManager = &resources;
        app.m_engine.GetSceneManager().setWorld(&world);
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        settings.projectFile = project.root / "Rename.tinyproject";
        settings.assetDirectory = "assets";
        app.m_sceneDocuments = {{scenePath, snapshot, false, {snapshot}, {snapshot}},
                                {inactivePath, snapshot, true, {snapshot}, {snapshot}}};
        app.m_activeSceneDocument = 0;
        app.m_undo = {snapshot};
        app.m_redo = {snapshot};
        app.m_selected = entity;
        app.m_selection = {entity};
        app.m_selectedAsset.clear();
        std::string error;
        const auto guid = resources.getAssetRegistry().Find(toUtf8GenericPath(source))->id;
        const auto blocked = assets / "blocked.mesh";
        PlatformFile::writeAllText(blocked, "existing destination\n");
        const auto originalMeta = PlatformFile::readAllText(toUtf8GenericPath(source) + ".meta");
        check(!app.moveEditorAsset(toUtf8GenericPath(source), blocked, error) && !error.empty(),
              "invalid rename fails visibly");
        check(SceneSerializer::serialize(world) == snapshot && app.m_undo == std::vector<std::string>{snapshot} &&
                  app.m_redo == std::vector<std::string>{snapshot} && app.m_sceneDocuments[1].snapshot == snapshot &&
                  PlatformFile::readAllText(toUtf8GenericPath(source) + ".meta") == originalMeta,
              "failed rename leaves live World, histories and metadata unchanged");
        error.clear();
        check(app.moveEditorAsset(toUtf8GenericPath(source), destination, error),
              "editor rename succeeds through the real move transaction");
        check(world.isAlive(entity) &&
                  world.GetComponent<MeshComponent>(entity)->meshPath == "assets/Moved/renamed.mesh" &&
                  app.m_selected == entity && app.m_selection == std::vector<Entity>{entity} &&
                  app.m_selectedAsset.empty(),
              "rename updates live portable reference while preserving entity handles and inspector ownership");
        check(findMesh(world, "assets/Models/a.mesh") &&
                  findMesh(world, "assets/Models/a.mesh")->meshPath == "Moved/renamed.mesh",
              "live reference migration leaves an identical entity name untouched");
        check(app.m_undo.size() == 1 && app.m_redo.size() == 1 && app.m_sceneDocuments[0].undo.size() == 1 &&
                  app.m_sceneDocuments[1].undo.size() == 1,
              "rename preserves history entries instead of clearing undo");
        app.undo();
        check(findMesh(world, "Keep handle") && findMesh(world, "Keep handle")->meshPath == "assets/Moved/renamed.mesh",
              "Undo cannot restore the old asset reference");
        app.redo();
        check(findMesh(world, "Keep handle")->meshPath == "assets/Moved/renamed.mesh",
              "Redo retains the renamed asset reference");
        check(app.switchSceneDocument(1) && findMesh(world, "Keep handle")->meshPath == "assets/Moved/renamed.mesh",
              "inactive scene tab snapshot follows rename");
        app.undo();
        app.redo();
        check(findMesh(world, "Keep handle")->meshPath == "assets/Moved/renamed.mesh",
              "inactive scene undo and redo histories follow rename");
        const auto sceneDestination = assets / "Scenes/renamed.scene";
        check(app.moveEditorAsset(toUtf8GenericPath(inactivePath), sceneDestination, error) &&
                  app.m_sceneDocuments[1].path == sceneDestination,
              "renaming an open scene updates its document save path");
        check(resources.getAssetRegistry().FindById(guid)->path == toUtf8GenericPath(destination),
              "editor rename retains GUID lookup");
        resources.shutdown();
        context.resourceManager = nullptr;
        context.world = nullptr;
    }
};
} // namespace tinyengine

int main(int argc, char** argv)
{
    try
    {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (mode.empty() || mode == "--backend")
        {
            tinyengine::backendPortableReferences();
#ifdef _WIN32
            tinyengine::backendWriteFailureRollback();
#endif
        }
        if (mode.empty() || mode == "--editor")
            tinyengine::EditorInteractionTests{}.portableLiveWorldAndHistories();
        if (mode.empty() || mode == "--project")
            tinyengine::EditorInteractionTests{}.configuredProjectReferences(false);
        if (mode.empty() || mode == "--project-rollback")
            tinyengine::EditorInteractionTests{}.configuredProjectReferences(true);
        if (mode.empty() || mode == "--shader-in")
            tinyengine::shaderIncludeRename(false);
        if (mode.empty() || mode == "--shader-out")
            tinyengine::shaderIncludeRename(true);
        if (mode.empty() || mode == "--shader-editor")
            tinyengine::EditorInteractionTests{}.openShaderIncludeRename();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
