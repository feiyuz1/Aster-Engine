#include "MiniEngine/Core/Config.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Physics/PhysicsWorld.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Resource/AssetCooker.hpp"
#include "MiniEngine/Resource/AssetRegistry.hpp"
#include "MiniEngine/Resource/PakArchive.hpp"
#include "MiniEngine/Task/JobSystem.hpp"
#include "MiniEngine/UI/UISystem.hpp"
#ifdef TINYENGINE_HAS_ASSIMP
#include "MiniEngine/Import/AssimpImporter.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    int failures = 0;
    void Check(const bool condition, const char* expression, const int line)
    {
        if (!condition) { std::cerr << "FAILED line " << line << ": " << expression << '\n'; ++failures; }
    }
}
#define CHECK(value) Check((value), #value, __LINE__)

int main()
{
    using namespace MiniEngine;

    CHECK(std::abs(Length(Vec3{3.0f, 4.0f, 0.0f}) - 5.0f) < 0.0001f);
    CHECK(Dot(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == 0.0f);
    const Aabb firstBounds{{-1, -1, -1}, {1, 1, 1}};
    const Aabb secondBounds{{0, 0, 0}, {2, 2, 2}};
    CHECK(firstBounds.Intersects(secondBounds));

    World world;
    const Entity first = world.CreateEntity("First");
    world.AddComponent(first, MeshComponent{"mesh", "material"});
    world.DestroyEntity(first);
    CHECK(!world.IsAlive(first));
    CHECK(world.GetComponent<MeshComponent>(first) == nullptr);
    const Entity replacement = world.CreateEntity("Replacement");
    CHECK(EntityIndex(first) == EntityIndex(replacement));
    CHECK(first != replacement);

    const Entity child = world.CreateEntity("Child");
    CHECK(world.SetParent(child, replacement));
    CHECK(!world.SetParent(replacement, child));
    CHECK(SceneSerializer::Save(world, "Saved/Test.scene"));
    World loaded;
    CHECK(SceneSerializer::Load(loaded, "Saved/Test.scene"));
    CHECK(loaded.GetEntityManager().GetAliveEntities().size() == 2);
    const std::string snapshot = SceneSerializer::Serialize(loaded);
    loaded.CreateEntity("Temporary");
    CHECK(SceneSerializer::Deserialize(loaded, snapshot));
    CHECK(loaded.GetEntityManager().GetAliveEntities().size() == 2);
    CHECK(Prefab::Save(loaded, *loaded.GetEntityManager().GetAliveEntities().begin(), "Saved/Test.prefab"));
    World prefabTarget;
    CHECK(!Prefab::Instantiate(prefabTarget, "Saved/Test.prefab").empty());

    ProjectSettings project;
    project.name = "TestGame";
    project.startupScene = "Saved/Test.scene";
    CHECK(project.Save("Saved/Test.tinyproject"));
    ProjectSettings loadedProject;
    CHECK(loadedProject.Load("Saved/Test.tinyproject"));
    CHECK(loadedProject.name == "TestGame");
    SaveGame saveGame;
    saveGame.Set("player.level", "7"); saveGame.Set("player.name", "Tiny");
    CHECK(saveGame.Save("Saved/Test.save"));
    SaveGame loadedSave;
    CHECK(loadedSave.Load("Saved/Test.save") && loadedSave.GetInt("player.level") == 7 && loadedSave.GetString("player.name") == "Tiny");

    JobSystem jobs;
    jobs.Initialize(2);
    std::atomic<int> order{0};
    auto dependency = jobs.CreateJob([&order]() { order.store(1); });
    auto dependent = jobs.CreateJob([&order]() { if (order.load() == 1) order.store(2); });
    jobs.AddDependency(dependent, dependency);
    jobs.Submit(dependent);
    jobs.Submit(dependency);
    jobs.Wait(dependent);
    CHECK(order.load() == 2);
    auto failing = jobs.CreateJob([]() { throw std::runtime_error("expected"); });
    jobs.Submit(failing);
    bool exceptionObserved = false;
    try { jobs.Wait(failing); } catch (const std::runtime_error&) { exceptionObserved = true; }
    CHECK(exceptionObserved);
    jobs.Shutdown();

    MeshResource mesh;
    CHECK(mesh.Load("assets/player.mesh"));
    CHECK(mesh.GetVertices().size() == 3);
    CHECK(mesh.GetIndices().size() == 3);
#ifdef TINYENGINE_HAS_ASSIMP
    CHECK(PlatformFile::WriteAllText("Saved/ImportTriangle.obj", "o Triangle\nv 0 0 0\nv 2 0 0\nv 0 2 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n"));
    ModelImportOptions importOptions; importOptions.resourcePrefix = "assets/ImportedTest";
    const auto imported = AssimpImporter::ImportModel("Saved/ImportTriangle.obj", "Saved/ImportedTest", importOptions);
    CHECK(imported.success && !imported.prefabPath.empty());
    const auto importedMesh = std::find_if(imported.assets.begin(), imported.assets.end(), [](const ImportedAsset& asset) { return asset.type == "Mesh"; });
    CHECK(importedMesh != imported.assets.end());
    MeshResource convertedMesh;
    CHECK(importedMesh != imported.assets.end() && convertedMesh.Load(importedMesh->path.string()));
    CHECK(convertedMesh.GetVertices().size() == 3 && std::abs(convertedMesh.GetVertices()[1].uv.x - 1.0f) < 0.001f);
    AssetRegistry importedRegistry; importedRegistry.Scan("Saved/ImportedTest", false);
    const auto importedPrefab = importedRegistry.Find(imported.prefabPath.generic_string());
    CHECK(importedPrefab.has_value() && importedPrefab->dependencies.size() >= 2 && importedPrefab->importSettings.count("source") == 1);
#endif
    TextureResource texture;
    CHECK(texture.Load("assets/default.ppm"));
    CHECK(texture.GetWidth() == 2 && texture.GetHeight() == 2 && texture.GetData().size() == 16);
    CHECK(PakArchive::PackDirectory("assets", "Saved/Test.tpak"));
    CHECK(PakArchive::List("Saved/Test.tpak").size() >= 4);
    const auto packedMesh = PakArchive::ReadFile("Saved/Test.tpak", "player.mesh");
    CHECK(packedMesh.has_value() && !packedMesh->empty());
    AssetRegistry registry;
    registry.Scan("assets");
    const auto meshInfo = registry.Find("assets/player.mesh");
    CHECK(meshInfo.has_value() && meshInfo->id.size() == 32);
    CHECK(registry.FindById(meshInfo->id).has_value());
    CHECK(registry.MarkImported("assets/player.mesh"));
    CHECK(!registry.NeedsReimport("assets/player.mesh"));
    const auto cook = AssetCooker::Cook("assets", "Saved/CookedTest", "Saved/CookedTest.tpak");
    CHECK(cook.success && cook.cookedFiles >= 4);
    PlatformFile::ClearMounts();
    CHECK(PlatformFile::MountPak("Saved/CookedTest.tpak"));
    CHECK(!PlatformFile::ReadAllBytes("player.mesh").empty());
    PlatformFile::ClearMounts();

    World physicsScene;
    const Entity body = physicsScene.CreateEntity("Body");
    physicsScene.GetComponent<TransformComponent>(body)->position = {0.0f, 2.0f, 0.0f};
    physicsScene.AddComponent(body, RigidBodyComponent{});
    physicsScene.AddComponent(body, ColliderComponent{});
    const Entity ground = physicsScene.CreateEntity("Ground");
    physicsScene.GetComponent<TransformComponent>(ground)->position = {0.0f, -0.25f, 0.0f};
    physicsScene.GetComponent<TransformComponent>(ground)->scale = {20.0f, 0.5f, 20.0f};
    physicsScene.AddComponent(ground, RigidBodyComponent{BodyType::Static, 0.0f, {}, false});
    physicsScene.AddComponent(ground, ColliderComponent{});
    PhysicsWorld physics;
    for (int index = 0; index < 180; ++index) physics.Step(physicsScene, 1.0f / 60.0f);
    CHECK(physicsScene.GetComponent<TransformComponent>(body)->position.y >= 0.49f);
    RaycastHit queryHit;
    CHECK(physics.SphereCast(physicsScene, {{0.0f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, 0.25f, 10.0f, queryHit));
    CHECK(!physics.Overlap(physicsScene, {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}).empty());

    Config config;
    config.Set("render.vsync", "true");
    config.Set("physics.fixedRate", "60");
    CHECK(config.Save("Saved/Test.ini"));
    Config loadedConfig;
    CHECK(loadedConfig.Load("Saved/Test.ini"));
    CHECK(loadedConfig.GetBool("render.vsync"));
    CHECK(loadedConfig.GetInt("physics.fixedRate") == 60);

    UISystem ui;
    bool clicked = false;
    ui.AddPanel("hud", {0, 0, 300, 100});
    ui.AddButton("start", "Start", {10, 10, 120, 40}, [&clicked]() { clicked = true; });
    ui.Update(20, 20, true); ui.Update(20, 20, false);
    CHECK(clicked && ui.GetFocusedId() == "start" && ui.BuildDrawList().size() == 2);

    if (failures == 0) std::cout << "All TinyEngine tests passed\n";
    return failures == 0 ? 0 : 1;
}
