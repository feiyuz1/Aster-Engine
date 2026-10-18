#include "EditorApp.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/AssetMaintenance.hpp"
#include "MiniEngine/Render/ShaderPipeline.hpp"
#include "MiniEngine/Core/Config.hpp"
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

namespace MiniEngine
{
namespace
{
void Check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS: " << message << '\n';
}
struct TemporaryProject
{
    std::filesystem::path root = std::filesystem::absolute("Saved") /
        ("EditorAssetRename_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryProject() { std::filesystem::create_directories(root / "assets/Models"); }
    ~TemporaryProject() { std::error_code error; std::filesystem::remove_all(root, error); }
};
Entity AddMesh(World &world, const std::string &name, const std::string &path)
{
    const auto entity = world.CreateEntity(name);
    world.AddComponent(entity, MeshComponent{path, {}});
    return entity;
}
const MeshComponent *FindMesh(const World &world, const std::string &name)
{
    for (const auto entity : world.GetEntityManager().GetAliveEntities())
        if (const auto *component = world.GetComponent<NameComponent>(entity); component && component->name == name)
            return world.GetComponent<MeshComponent>(entity);
    return nullptr;
}
void ShaderIncludeRename(bool moveOwner)
{
    // Renaming an included file or relocating its owner must keep the real preprocessor dependency graph valid.
    TemporaryProject project;
    const auto assets = project.root / "assets", shaders = assets / "Shaders";
    const auto included = shaders / "shared.glsl", owner = shaders / "effect.frag";
    const std::string comments = "// #include \"shared.glsl\"\n// Keep unrelated text: shared.glsl\n";
    PlatformFile::WriteAllText(included, "vec3 SharedColor() { return vec3(1.0); }\n");
    PlatformFile::WriteAllText(owner, "#version 330 core\n  #include \"shared.glsl\" // preserve this comment\n// display label: \"shared.glsl\"\nvoid main() {}\n");
    PlatformFile::WriteAllText(shaders / "comments.frag", comments);
    Check(ShaderPipeline::Preprocess(owner).success, "shader include fixture preprocesses before rename");
    AssetRegistry registry; registry.Scan(assets);
    const auto source = moveOwner ? owner : included;
    const auto destination = assets / (moveOwner ? "Moved/effect.frag" : "Library/renamed.glsl");
    std::string error;
    Check(AssetMaintenance::MoveAsset(registry, source, destination, &error), "shader move transaction succeeds");
    const auto preprocessed = ShaderPipeline::Preprocess(moveOwner ? destination : owner);
    Check(preprocessed.success && preprocessed.source.find("SharedColor") != std::string::npos,
          moveOwner ? "moving shader owner repairs its outgoing relative includes" : "renaming included shader repairs incoming owner-relative includes");
    const auto text = PlatformFile::ReadAllText(moveOwner ? destination : owner);
    Check(text.find("// preserve this comment") != std::string::npos && text.find("// display label: \"shared.glsl\"") != std::string::npos &&
          PlatformFile::ReadAllText(shaders / "comments.frag") == comments, "shader migration preserves comments and unrelated text");
}
void BackendPortableReferences()
{
    // Removing portable-reference migration must leave these real scene/dependency paths stale.
    TemporaryProject project;
    const auto assets = project.root / "assets";
    const auto source = assets / "Models/a.mesh", destination = assets / "Moved/renamed.mesh";
    const auto scene = assets / "main.scene";
    PlatformFile::WriteAllText(source, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    World world;
    AddMesh(world, "assets/Models/a.mesh", "assets/Models/a.mesh");
    AddMesh(world, "asset relative", "Models/a.mesh");
    AddMesh(world, "unrelated substring", "assets/Models/a.mesh.backup");
    Check(SceneSerializer::Save(world, scene), "write real portable-reference scene fixture");
    AssetRegistry registry; registry.Scan(assets);
    const auto before = registry.Find(source.generic_u8string());
    Check(before.has_value(), "mesh has a stable registered GUID");
    registry.SetDependencies(scene.generic_u8string(), {"assets/Models/a.mesh", "Models/a.mesh", source.generic_u8string(), before->id});
    std::string error;
    Check(AssetMaintenance::MoveAsset(registry, source, destination, &error), "move mesh and metadata transaction");
    World loaded;
    Check(SceneSerializer::Load(loaded, scene), "migrated scene remains loadable");
    Check(FindMesh(loaded, "assets/Models/a.mesh") && FindMesh(loaded, "assets/Models/a.mesh")->meshPath == "assets/Moved/renamed.mesh",
          "project-relative scene reference follows rename without changing entity name");
    Check(FindMesh(loaded, "asset relative") && FindMesh(loaded, "asset relative")->meshPath == "Moved/renamed.mesh",
          "asset-root-relative scene reference follows rename");
    Check(FindMesh(loaded, "unrelated substring")->meshPath == "assets/Models/a.mesh.backup",
          "rename does not rewrite a different path containing the old name");
    const auto owner = registry.Find(scene.generic_u8string());
    Check(owner && owner->dependencies == std::vector<std::string>{"assets/Moved/renamed.mesh", "Moved/renamed.mesh", destination.generic_u8string(), before->id},
          "portable dependency styles migrate while GUID dependencies remain stable");
    Check(registry.FindById(before->id) && registry.FindById(before->id)->path == destination.generic_u8string() &&
          !std::filesystem::exists(source) && !std::filesystem::exists(source.generic_u8string() + ".meta") &&
          std::filesystem::exists(destination.generic_u8string() + ".meta"), "rename preserves GUID and moves the metadata sidecar");
}
#ifdef _WIN32
void BackendWriteFailureRollback()
{
    // A denied dependent-file replacement must roll back the asset, metadata and earlier writes.
    TemporaryProject project;
    const auto assets = project.root / "assets";
    const auto source = assets / "Models/a.mesh", destination = assets / "Moved/renamed.mesh";
    const auto scene = assets / "locked.scene", other = assets / "other.scene";
    PlatformFile::WriteAllText(source, "mesh payload\n");
    World world; AddMesh(world, "Dependent", "assets/Models/a.mesh");
    SceneSerializer::Save(world, scene); SceneSerializer::Save(world, other);
    AssetRegistry registry; registry.Scan(assets);
    const auto guid = registry.Find(source.generic_u8string())->id;
    const auto oldScene = PlatformFile::ReadAllText(scene), oldOther = PlatformFile::ReadAllText(other);
    const auto oldMetadata = PlatformFile::ReadAllText(source.generic_u8string() + ".meta");
    const HANDLE locked = CreateFileW(scene.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "lock a real dependent file against replacement");
    std::string error;
    const bool moved = AssetMaintenance::MoveAsset(registry, source, destination, &error);
    CloseHandle(locked);
    Check(!moved && !error.empty() && std::filesystem::exists(source) && !std::filesystem::exists(destination) &&
          PlatformFile::ReadAllText(source.generic_u8string() + ".meta") == oldMetadata &&
          PlatformFile::ReadAllText(scene) == oldScene && PlatformFile::ReadAllText(other) == oldOther &&
          registry.FindById(guid) && registry.FindById(guid)->path == source.generic_u8string(),
          "dependent write failure rolls back files, metadata, registry and earlier reference edits");
}
#endif
}

struct EditorInteractionTests
{
    void OpenShaderIncludeRename()
    {
        // A dirty dependent must never be overwritten; a clean editor must receive the rewritten disk baseline.
        TemporaryProject project;
        const auto assets = project.root / "assets", included = assets / "Shaders/shared.glsl", owner = assets / "Shaders/effect.frag";
        const auto destination = assets / "Library/renamed.glsl";
        const std::string original = "#version 330 core\n#include \"shared.glsl\"\nvoid main() {}\n";
        PlatformFile::WriteAllText(included, "vec3 SharedColor() { return vec3(1.0); }\n");
        PlatformFile::WriteAllText(owner, original);
        ResourceManager resources; resources.Initialize(nullptr, assets);
        World world; EditorApp app;
        auto &context = const_cast<EngineContext &>(app.m_engine.GetContext()); context.world = &world; context.resourceManager = &resources;
        app.m_engine.GetSceneManager().SetWorld(&world);
        auto &settings = const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings());
        settings.projectFile = project.root / "Rename.tinyproject"; settings.assetDirectory = "assets";
        app.m_openShaderAsset = owner.generic_u8string(); app.m_savedShaderSource = original;
        const auto draft = original + "// unsaved edit\n";
        app.m_shaderSource.assign(draft.begin(), draft.end()); app.m_shaderSource.push_back('\0');
        std::string error;
        Check(!app.MoveEditorAsset(included.generic_u8string(), destination, error) && !error.empty() &&
              std::filesystem::exists(included) && !std::filesystem::exists(destination) && PlatformFile::ReadAllText(owner) == original &&
              std::string(app.m_shaderSource.data()) == draft, "dirty open dependent shader blocks rename before files or drafts change");
        app.m_shaderSource.assign(original.begin(), original.end()); app.m_shaderSource.push_back('\0');
        const auto revision = app.m_shaderDocumentRevision;
        Check(app.MoveEditorAsset(included.generic_u8string(), destination, error), "rename include with clean dependent editor");
        Check(app.m_savedShaderSource == PlatformFile::ReadAllText(owner) && std::string(app.m_shaderSource.data()) == app.m_savedShaderSource &&
              app.m_savedShaderSource.find("../Library/renamed.glsl") != std::string::npos && app.m_shaderDocumentRevision > revision,
              "clean dependent shader buffer and saved baseline follow incoming include rewrite");
        const auto movedOwner = assets / "Moved/effect.frag";
        Check(app.MoveEditorAsset(owner.generic_u8string(), movedOwner, error) && app.m_openShaderAsset == movedOwner.generic_u8string() &&
              app.m_savedShaderSource == PlatformFile::ReadAllText(movedOwner) && std::string(app.m_shaderSource.data()) == app.m_savedShaderSource &&
              ShaderPipeline::Preprocess(movedOwner).success, "clean renamed shader owner reloads its rebased includes without stale save text");
        resources.Shutdown(); context.resourceManager = nullptr; context.world = nullptr;
    }
    void ConfiguredProjectReferences(bool deniedWrite)
    {
        // Dropping project-file or staged settings migration breaks restart or violates failed-move atomicity.
        TemporaryProject project;
        const auto assets = project.root / "assets", projectFile = project.root / "Rename.tinyproject";
        World world; const auto entity = AddMesh(world, "Project world", "assets/Models/a.mesh");
        world.GetComponent<MeshComponent>(entity)->materialPath = "assets/textureOwner.mat";
        PlatformFile::WriteAllText(assets / "Models/a.mesh", "mesh payload\n");
        Check(SceneSerializer::Save(world, assets / "start.scene"), "write startup scene fixture");
        PlatformFile::WriteAllText(assets / "start.input", "bindings fixture\n");
        PlatformFile::WriteAllText(assets / "sky.ppm", "P3\n1 1\n255\n128 128 128\n");
        PlatformFile::WriteAllText(assets / "textureOwner.mat", "texture \"assets/sky.ppm\"\n");
        ResourceManager resources; resources.Initialize(nullptr, assets);
        EditorApp app;
        auto &context = const_cast<EngineContext &>(app.m_engine.GetContext()); context.world = &world; context.resourceManager = &resources;
        app.m_engine.GetSceneManager().SetWorld(&world);
        auto &settings = const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings());
        settings.projectFile = projectFile; settings.assetDirectory = "assets";
        settings.startupScene = "assets/start.scene"; settings.inputBindings = "assets/start.input"; settings.environmentTexture = "assets/sky.ppm";
        Check(settings.Save(projectFile), "write real configured project file");
        PlatformFile::WriteAllText(projectFile, PlatformFile::ReadAllText(projectFile) + "custom.preserve=assets/sky.ppm\n");
        const auto snapshot = SceneSerializer::Serialize(world);
        app.m_sceneDocuments = {{assets / "start.scene", snapshot, false, {snapshot}, {snapshot}}};
        app.m_undo = {snapshot}; app.m_redo = {snapshot};
        std::string error;
        if (deniedWrite)
        {
#ifdef _WIN32
            const auto source = assets / "sky.ppm", destination = assets / "Moved/sky.ppm";
            const auto oldConfig = PlatformFile::ReadAllText(projectFile), oldOwner = PlatformFile::ReadAllText(assets / "textureOwner.mat");
            const auto oldMeta = PlatformFile::ReadAllText(source.generic_u8string() + ".meta");
            const auto guid = resources.GetAssetRegistry().Find(source.generic_u8string())->id;
            const HANDLE locked = CreateFileW(projectFile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            Check(locked != INVALID_HANDLE_VALUE, "lock real project file against transaction replacement");
            const bool moved = app.MoveEditorAsset(source.generic_u8string(), destination, error);
            CloseHandle(locked);
            Check(!moved && !error.empty(), "denied project write rejects the entire asset rename");
            Check(std::filesystem::exists(source) && !std::filesystem::exists(destination) &&
                  PlatformFile::ReadAllText(source.generic_u8string() + ".meta") == oldMeta && PlatformFile::ReadAllText(projectFile) == oldConfig &&
                  PlatformFile::ReadAllText(assets / "textureOwner.mat") == oldOwner && settings.environmentTexture == "assets/sky.ppm" &&
                  SceneSerializer::Serialize(world) == snapshot && app.m_undo == std::vector<std::string>{snapshot} &&
                  resources.GetAssetRegistry().FindById(guid)->path == source.generic_u8string(),
                  "project-write failure restores files, references, GUID, settings and editor histories");
#endif
        }
        else
        {
            Check(app.MoveEditorAsset((assets / "start.scene").generic_u8string(), assets / "Moved/start.scene", error), "rename configured startup scene");
            Check(settings.startupScene == "assets/Moved/start.scene", "live startup scene setting follows rename");
            Check(app.MoveEditorAsset((assets / "start.input").generic_u8string(), assets / "Moved/start.input", error), "rename configured input bindings");
            Check(settings.inputBindings == "assets/Moved/start.input", "live input bindings setting follows rename");
            const bool movedEnvironment=app.MoveEditorAsset((assets / "sky.ppm").generic_u8string(), assets / "Moved/sky.ppm", error);
            if (!movedEnvironment) std::cerr << "Environment rename: " << error << '\n';
            Check(movedEnvironment, "rename configured environment texture");
            Check(settings.environmentTexture == "assets/Moved/sky.ppm", "live environment texture setting follows rename");
            ProjectSettings reloaded;
            Check(reloaded.Load(projectFile) && reloaded.startupScene == "assets/Moved/start.scene" &&
                  reloaded.inputBindings == "assets/Moved/start.input" && reloaded.environmentTexture == "assets/Moved/sky.ppm",
                  "portable configured references survive a project restart");
            Config config;
            Check(config.Load(projectFile.generic_u8string()) && config.GetString("custom.preserve") == "assets/sky.ppm", "project migration preserves unknown configuration keys");
        }
        resources.Shutdown(); context.resourceManager = nullptr; context.world = nullptr;
    }
    void PortableLiveWorldAndHistories()
    {
        // Omitting any editor snapshot migration must allow Undo or an inactive tab to resurrect the old path.
        TemporaryProject project;
        const auto assets = project.root / "assets";
        const auto source = assets / "Models/a.mesh", destination = assets / "Moved/renamed.mesh";
        const auto scenePath = assets / "active.scene", inactivePath = assets / "inactive.scene";
        PlatformFile::WriteAllText(source, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        World world;
        const auto entity = AddMesh(world, "Keep handle", "assets/Models/a.mesh");
        AddMesh(world, "assets/Models/a.mesh", "Models/a.mesh");
        auto snapshot = SceneSerializer::Serialize(world);
        Check(SceneSerializer::Save(world, scenePath) && SceneSerializer::Save(world, inactivePath), "write active and inactive scene fixtures");
        ResourceManager resources; resources.Initialize(nullptr, assets);
        EditorApp app;
        auto &context = const_cast<EngineContext &>(app.m_engine.GetContext()); context.world = &world; context.resourceManager = &resources;
        app.m_engine.GetSceneManager().SetWorld(&world);
        auto &settings = const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings());
        settings.projectFile = project.root / "Rename.tinyproject"; settings.assetDirectory = "assets";
        app.m_sceneDocuments = {{scenePath, snapshot, false, {snapshot}, {snapshot}}, {inactivePath, snapshot, true, {snapshot}, {snapshot}}};
        app.m_activeSceneDocument = 0; app.m_undo = {snapshot}; app.m_redo = {snapshot};
        app.m_selected = entity; app.m_selection = {entity}; app.m_selectedAsset.clear();
        std::string error;
        const auto guid = resources.GetAssetRegistry().Find(source.generic_u8string())->id;
        const auto blocked = assets / "blocked.mesh";
        PlatformFile::WriteAllText(blocked, "existing destination\n");
        const auto originalMeta = PlatformFile::ReadAllText(source.generic_u8string() + ".meta");
        Check(!app.MoveEditorAsset(source.generic_u8string(), blocked, error) && !error.empty(), "invalid rename fails visibly");
        Check(SceneSerializer::Serialize(world) == snapshot && app.m_undo == std::vector<std::string>{snapshot} &&
              app.m_redo == std::vector<std::string>{snapshot} && app.m_sceneDocuments[1].snapshot == snapshot &&
              PlatformFile::ReadAllText(source.generic_u8string() + ".meta") == originalMeta,
              "failed rename leaves live World, histories and metadata unchanged");
        error.clear();
        Check(app.MoveEditorAsset(source.generic_u8string(), destination, error), "editor rename succeeds through the real move transaction");
        Check(world.IsAlive(entity) && world.GetComponent<MeshComponent>(entity)->meshPath == "assets/Moved/renamed.mesh" &&
              app.m_selected == entity && app.m_selection == std::vector<Entity>{entity} && app.m_selectedAsset.empty(),
              "rename updates live portable reference while preserving entity handles and inspector ownership");
        Check(FindMesh(world, "assets/Models/a.mesh") && FindMesh(world, "assets/Models/a.mesh")->meshPath == "Moved/renamed.mesh",
              "live reference migration leaves an identical entity name untouched");
        Check(app.m_undo.size() == 1 && app.m_redo.size() == 1 && app.m_sceneDocuments[0].undo.size() == 1 &&
              app.m_sceneDocuments[1].undo.size() == 1, "rename preserves history entries instead of clearing undo");
        app.Undo();
        Check(FindMesh(world, "Keep handle") && FindMesh(world, "Keep handle")->meshPath == "assets/Moved/renamed.mesh",
              "Undo cannot restore the old asset reference");
        app.Redo();
        Check(FindMesh(world, "Keep handle")->meshPath == "assets/Moved/renamed.mesh", "Redo retains the renamed asset reference");
        Check(app.SwitchSceneDocument(1) && FindMesh(world, "Keep handle")->meshPath == "assets/Moved/renamed.mesh",
              "inactive scene tab snapshot follows rename");
        app.Undo(); app.Redo();
        Check(FindMesh(world, "Keep handle")->meshPath == "assets/Moved/renamed.mesh", "inactive scene undo and redo histories follow rename");
        const auto sceneDestination = assets / "Scenes/renamed.scene";
        Check(app.MoveEditorAsset(inactivePath.generic_u8string(), sceneDestination, error) && app.m_sceneDocuments[1].path == sceneDestination,
              "renaming an open scene updates its document save path");
        Check(resources.GetAssetRegistry().FindById(guid)->path == destination.generic_u8string(), "editor rename retains GUID lookup");
        resources.Shutdown(); context.resourceManager = nullptr; context.world = nullptr;
    }
};
}

int main(int argc, char **argv)
{
    try
    {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (mode.empty() || mode == "--backend")
        {
            MiniEngine::BackendPortableReferences();
#ifdef _WIN32
            MiniEngine::BackendWriteFailureRollback();
#endif
        }
        if (mode.empty() || mode == "--editor") MiniEngine::EditorInteractionTests{}.PortableLiveWorldAndHistories();
        if (mode.empty() || mode == "--project") MiniEngine::EditorInteractionTests{}.ConfiguredProjectReferences(false);
        if (mode.empty() || mode == "--project-rollback") MiniEngine::EditorInteractionTests{}.ConfiguredProjectReferences(true);
        if (mode.empty() || mode == "--shader-in") MiniEngine::ShaderIncludeRename(false);
        if (mode.empty() || mode == "--shader-out") MiniEngine::ShaderIncludeRename(true);
        if (mode.empty() || mode == "--shader-editor") MiniEngine::EditorInteractionTests{}.OpenShaderIncludeRename();
        return 0;
    }
    catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
