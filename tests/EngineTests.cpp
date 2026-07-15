#include "MiniEngine/AI/BehaviorTree.hpp"
#include "MiniEngine/Animation/SkeletalAnimation.hpp"
#include "MiniEngine/Audio/AudioEngine.hpp"
#include "MiniEngine/Automation/Automation.hpp"
#include "MiniEngine/Build/AssetCompiler.hpp"
#include "MiniEngine/Build/TargetPlatform.hpp"
#include "MiniEngine/Cinematic/Sequencer.hpp"
#include "MiniEngine/Core/Config.hpp"
#include "MiniEngine/Core/CrashReporter.hpp"
#include "MiniEngine/Core/Module.hpp"
#include "MiniEngine/Diagnostics/Diagnostics.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/Reflection.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/ECS/Systems.hpp"
#include "MiniEngine/Gameplay/Gameplay.hpp"
#include "MiniEngine/Input/InputSystem.hpp"
#include "MiniEngine/Localization/Localization.hpp"
#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Media/ImageSequence.hpp"
#include "MiniEngine/Navigation/Navigation.hpp"
#include "MiniEngine/Network/Replication.hpp"
#include "MiniEngine/Physics/PhysicsWorld.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/Project/ProjectPackager.hpp"
#include "MiniEngine/Render/Renderer.hpp"
#include "MiniEngine/Render/ShaderPipeline.hpp"
#include "MiniEngine/Resource/AssetCooker.hpp"
#include "MiniEngine/Resource/AssetRegistry.hpp"
#include "MiniEngine/Resource/AssetStreaming.hpp"
#include "MiniEngine/Resource/PakArchive.hpp"
#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"
#include "MiniEngine/Serialization/Archive.hpp"
#include "MiniEngine/Task/JobSystem.hpp"
#include "MiniEngine/Terrain/HeightField.hpp"
#include "MiniEngine/UI/UISystem.hpp"
#include "MiniEngine/VFX/ParticleSystem.hpp"
#include "MiniEngine/World/WorldPartition.hpp"
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

class TestModule final : public MiniEngine::IModule
{
  public:
    TestModule(std::string name, std::vector<std::string> dependencies, std::vector<std::string> &events)
        : m_name(std::move(name)), m_dependencies(std::move(dependencies)), m_events(events)
    {
    }
    const char *GetName() const override
    {
        return m_name.c_str();
    }
    std::vector<std::string> GetDependencies() const override
    {
        return m_dependencies;
    }
    bool Startup() override
    {
        m_events.push_back("start:" + m_name);
        return true;
    }
    void Shutdown() override
    {
        m_events.push_back("stop:" + m_name);
    }

  private:
    std::string m_name;
    std::vector<std::string> m_dependencies;
    std::vector<std::string> &m_events;
};

void Check(const bool condition, const char *expression, const int line)
{
    if (!condition)
    {
        std::cerr << "FAILED line " << line << ": " << expression << '\n';
        ++failures;
    }
}
} // namespace
#define CHECK(value) Check((value), #value, __LINE__)

int main()
{
    using namespace MiniEngine;

    CHECK(std::abs(Length(Vec3{3.0f, 4.0f, 0.0f}) - 5.0f) < 0.0001f);
    CHECK(Dot(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == 0.0f);
    const Aabb firstBounds{{-1, -1, -1}, {1, 1, 1}};
    const Aabb secondBounds{{0, 0, 0}, {2, 2, 2}};
    CHECK(firstBounds.Intersects(secondBounds));
    const Mat4 orthographic = Orthographic(-2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    CHECK(std::abs(orthographic.At(0, 0) - 0.5f) < 0.0001f && std::abs(orthographic.At(1, 1) - 1.0f) < 0.0001f);

    BinaryWriter binaryWriter;
    binaryWriter.WriteU32(0x12345678u);
    binaryWriter.WriteFloat(3.5f);
    binaryWriter.WriteString("TinyEngine");
    BinaryReader binaryReader(binaryWriter.GetData());
    std::uint32_t archiveInteger = 0;
    float archiveFloat = 0.0f;
    std::string archiveString;
    CHECK(binaryReader.ReadU32(archiveInteger) && archiveInteger == 0x12345678u);
    CHECK(binaryReader.ReadFloat(archiveFloat) && std::abs(archiveFloat - 3.5f) < 0.001f);
    CHECK(binaryReader.ReadString(archiveString) && archiveString == "TinyEngine" && binaryReader.GetRemainingBytes() == 0);
    VersionedDocument versioned{"TestData", 1, {{"oldName", "value"}}};
    MigrationRegistry migrations;
    CHECK(migrations.Register("TestData", 1, [](VersionedDocument &document, std::string &) {
        document.fields["newName"] = document.fields["oldName"];
        document.fields.erase("oldName");
        return true;
    }));
    CHECK(migrations.Migrate(versioned, 2) && versioned.version == 2 && versioned.fields["newName"] == "value");
    CHECK(VersionedTextArchive::SaveAtomic("Saved/Test.archive", versioned, false));
    VersionedDocument loadedVersioned;
    CHECK(VersionedTextArchive::Deserialize(PlatformFile::ReadAllText("Saved/Test.archive"), loadedVersioned));
    CHECK(loadedVersioned.version == 2 && loadedVersioned.fields["newName"] == "value");

    World world;
    const Entity first = world.CreateEntity("First");
    world.AddComponent(first, MeshComponent{"mesh", "material"});
    world.DestroyEntity(first);
    CHECK(!world.IsAlive(first));
    CHECK(world.GetComponent<MeshComponent>(first) == nullptr);
    const Entity replacement = world.CreateEntity("Replacement");
    CHECK(EntityIndex(first) == EntityIndex(replacement));
    CHECK(first != replacement);
    const auto &reflection = ReflectionRegistry::Get();
    const auto *reflectedTransform = reflection.FindComponent("Transform");
    const auto *reflectedRigidBody = reflection.FindComponent("Rigid Body");
    CHECK(reflectedTransform != nullptr && !reflectedTransform->removable && reflectedTransform->properties.size() == 3);
    CHECK(reflectedRigidBody != nullptr && reflectedRigidBody->removable && !reflectedRigidBody->has(world, replacement));
    reflectedRigidBody->add(world, replacement);
    CHECK(reflectedRigidBody->has(world, replacement) && reflectedRigidBody->properties.front().enumNames.size() == 3);
    const auto massProperty = std::find_if(reflectedRigidBody->properties.begin(), reflectedRigidBody->properties.end(),
                                           [](const ReflectedProperty &property) { return property.name == "Mass"; });
    CHECK(massProperty != reflectedRigidBody->properties.end());
    if (massProperty != reflectedRigidBody->properties.end())
        *static_cast<float *>(massProperty->access(world, replacement)) = 4.5f;
    CHECK(std::abs(world.GetComponent<RigidBodyComponent>(replacement)->mass - 4.5f) < 0.001f);
    reflectedRigidBody->remove(world, replacement);
    CHECK(!reflectedRigidBody->has(world, replacement));

    const Entity child = world.CreateEntity("Child");
    CHECK(world.SetParent(child, replacement));
    CHECK(!world.SetParent(replacement, child));
    world.AddComponent(replacement, JointComponent{JointType::Distance, child, 2.0f, 0.75f});
    world.AddComponent(replacement, LodComponent{"assets/player.mesh", "assets/player.mesh", 10.0f, 30.0f});
    world.AddComponent(replacement, PawnComponent{});
    world.AddComponent(replacement, CharacterControllerComponent{});
    CHECK(SceneSerializer::Save(world, "Saved/Test.scene"));
    World loaded;
    CHECK(SceneSerializer::Load(loaded, "Saved/Test.scene"));
    CHECK(loaded.GetEntityManager().GetAliveEntities().size() == 2);
    bool loadedJointValid = false;
    for (const Entity entity : loaded.GetEntityManager().GetAliveEntities())
        if (const auto *joint = loaded.GetComponent<JointComponent>(entity))
            loadedJointValid = loaded.IsAlive(joint->connectedEntity) && std::abs(joint->restLength - 2.0f) < 0.001f;
    CHECK(loadedJointValid);
    bool loadedLod = false;
    for (const Entity entity : loaded.GetEntityManager().GetAliveEntities())
        if (const auto *lod = loaded.GetComponent<LodComponent>(entity))
            loadedLod = std::abs(lod->level2Distance - 30.0f) < 0.001f;
    CHECK(loadedLod);
    bool loadedCharacter = false;
    for (const Entity entity : loaded.GetEntityManager().GetAliveEntities())
        loadedCharacter =
            loadedCharacter || (loaded.HasComponent<PawnComponent>(entity) && loaded.HasComponent<CharacterControllerComponent>(entity));
    CHECK(loadedCharacter);
    const std::string snapshot = SceneSerializer::Serialize(loaded);
    loaded.CreateEntity("Temporary");
    CHECK(SceneSerializer::Deserialize(loaded, snapshot));
    CHECK(loaded.GetEntityManager().GetAliveEntities().size() == 2);
    CHECK(Prefab::Save(loaded, *loaded.GetEntityManager().GetAliveEntities().begin(), "Saved/Test.prefab"));
    World prefabTarget;
    CHECK(!Prefab::Instantiate(prefabTarget, "Saved/Test.prefab").empty());
    World lightingWorld;
    for (int index = 0; index < 6; ++index)
        lightingWorld.AddComponent(lightingWorld.CreateEntity("Light"),
                                   LightComponent{{1.0f, 0.8f, 0.6f}, static_cast<float>(index + 1), {-0.4f, -1.0f, -0.3f}});
    Renderer lightingRenderer;
    CHECK(lightingRenderer.Initialize(std::make_unique<NullRenderDevice>()));
    lightingRenderer.BuildRenderQueue(lightingWorld);
    CHECK(lightingRenderer.GetFrameView().lightCount == static_cast<int>(FrameView::MaxLights));
    VfxSystem renderVfx;
    ParticleEmitterSettings renderParticleSettings;
    renderParticleSettings.emissionRate = 0.0f;
    const auto renderEmitter = renderVfx.CreateEmitter(renderParticleSettings);
    renderVfx.FindEmitter(renderEmitter)->Burst(3);
    lightingRenderer.SetVfxSystem(&renderVfx);
    lightingRenderer.RenderFrame(lightingWorld);
    CHECK(lightingRenderer.GetLastDrawCallCount() == 3);
    lightingRenderer.Shutdown();

    ProjectSettings project;
    project.name = "TestGame";
    project.startupScene = "Saved/Test.scene";
    project.inputBindings = "assets/default.input";
    project.environmentTexture = "assets/studio.hdr";
    CHECK(project.Save("Saved/Test.tinyproject"));
    ProjectSettings loadedProject;
    CHECK(loadedProject.Load("Saved/Test.tinyproject"));
    CHECK(loadedProject.name == "TestGame");
    CHECK(loadedProject.inputBindings == "assets/default.input" && loadedProject.environmentTexture == "assets/studio.hdr");
    SaveGame saveGame;
    saveGame.Set("player.level", "7");
    saveGame.Set("player.name", "Tiny");
    CHECK(saveGame.Save("Saved/Test.save"));
    SaveGame loadedSave;
    CHECK(loadedSave.Load("Saved/Test.save") && loadedSave.GetInt("player.level") == 7 && loadedSave.GetString("player.name") == "Tiny");

    JobSystem jobs;
    jobs.Initialize(2);
    std::atomic<int> order{0};
    auto dependency = jobs.CreateJob([&order]() { order.store(1); });
    auto dependent = jobs.CreateJob([&order]() {
        if (order.load() == 1)
            order.store(2);
    });
    jobs.AddDependency(dependent, dependency);
    jobs.Submit(dependent);
    jobs.Submit(dependency);
    jobs.Wait(dependent);
    CHECK(order.load() == 2);
    auto failing = jobs.CreateJob([]() { throw std::runtime_error("expected"); });
    jobs.Submit(failing);
    bool exceptionObserved = false;
    try
    {
        jobs.Wait(failing);
    }
    catch (const std::runtime_error &)
    {
        exceptionObserved = true;
    }
    CHECK(exceptionObserved);
    jobs.Shutdown();

    TimerManager timers;
    int timerExecutions = 0;
    const auto timer = timers.SetTimer(0.1f, [&timerExecutions]() { ++timerExecutions; }, true);
    timers.Update(0.25f);
    CHECK(timerExecutions == 1 && timers.IsActive(timer));
    CHECK(timers.ClearTimer(timer));
    World gameplayWorld;
    const Entity pawn = gameplayWorld.CreateEntity("Pawn");
    gameplayWorld.AddComponent(pawn, PawnComponent{});
    CharacterControllerComponent testController;
    testController.grounded = true;
    gameplayWorld.AddComponent(pawn, testController);
    InputSystem gameplayInput;
    InputState gameplayState;
    gameplayState.moveX = 1.0f;
    gameplayInput.Update(gameplayState);
    PhysicsWorld gameplayPhysics;
    GameplayFramework gameplay;
    gameplay.Initialize(&gameplayWorld, &gameplayInput, &gameplayPhysics);
    gameplay.StartPlay();
    gameplay.Update(0.5f);
    CHECK(gameplayWorld.GetComponent<TransformComponent>(pawn)->position.x > 1.9f);
    gameplay.StopPlay();
    gameplay.Shutdown();

    DiagnosticsService diagnostics;
    diagnostics.Initialize();
    int changedValue = 0;
    CHECK(diagnostics.GetConsole().RegisterVariable("test.value", 1, "Test variable", false,
                                                    [&changedValue](const ConsoleValue &value) { changedValue = std::get<int>(value); }));
    CHECK(diagnostics.GetConsole().Execute("test.value 7") == "test.value = 7" && changedValue == 7);
    diagnostics.GetStats().Set("test.stat", 2.0);
    diagnostics.GetStats().Add("test.stat", 3.0);
    CHECK(diagnostics.GetStats().Get("test.stat").value == 5.0);
    diagnostics.Shutdown();

    MeshResource mesh;
    CHECK(mesh.Load("assets/player.mesh"));
    CHECK(mesh.GetVertices().size() == 3);
    CHECK(mesh.GetIndices().size() == 3);
    CHECK(Length(mesh.GetVertices().front().tangent) > 0.9f &&
          std::abs(Dot(mesh.GetVertices().front().normal, mesh.GetVertices().front().tangent)) < 0.001f);
    const auto compiledMeshResult = PlatformAssetCompiler::Compile("assets/player.mesh", "Saved/CompiledPlayer.mesh");
    CHECK(compiledMeshResult.success && compiledMeshResult.type == CompiledAssetType::Mesh);
    MeshResource compiledMesh;
    CHECK(compiledMesh.Load("Saved/CompiledPlayer.mesh") && compiledMesh.GetVertices().size() == mesh.GetVertices().size());
#ifdef TINYENGINE_HAS_ASSIMP
    CHECK(PlatformFile::WriteAllText("Saved/ImportTriangle.obj",
                                     "o Triangle\nv 0 0 0\nv 2 0 0\nv 0 2 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n"));
    ModelImportOptions importOptions;
    importOptions.resourcePrefix = "assets/ImportedTest";
    const auto imported = AssimpImporter::ImportModel("Saved/ImportTriangle.obj", "Saved/ImportedTest", importOptions);
    CHECK(imported.success && !imported.prefabPath.empty());
    const auto importedMesh =
        std::find_if(imported.assets.begin(), imported.assets.end(), [](const ImportedAsset &asset) { return asset.type == "Mesh"; });
    CHECK(importedMesh != imported.assets.end());
    MeshResource convertedMesh;
    CHECK(importedMesh != imported.assets.end() && convertedMesh.Load(importedMesh->path.string()));
    CHECK(convertedMesh.GetVertices().size() == 3 && std::abs(convertedMesh.GetVertices()[1].uv.x - 1.0f) < 0.001f);
    AssetRegistry importedRegistry;
    importedRegistry.Scan("Saved/ImportedTest", false);
    const auto importedPrefab = importedRegistry.Find(imported.prefabPath.generic_string());
    CHECK(importedPrefab.has_value() && importedPrefab->dependencies.size() >= 2 && importedPrefab->importSettings.count("source") == 1);
    AssetImportQueue importQueue;
    importQueue.Initialize(nullptr);
    const auto queuedImport = importQueue.Submit("Saved/ImportTriangle.obj", "Saved/QueuedImport", importOptions);
    const auto queuedSnapshot = importQueue.Get(queuedImport);
    CHECK(queuedSnapshot.status == AssetImportStatus::Completed && queuedSnapshot.result.success);
    importQueue.ClearFinished();
#endif
    TextureResource texture;
    CHECK(texture.Load("assets/default.ppm"));
    CHECK(texture.GetWidth() == 2 && texture.GetHeight() == 2 && texture.GetData().size() == 16);
    const auto compiledTextureResult = PlatformAssetCompiler::Compile("assets/default.ppm", "Saved/CompiledDefault.ppm");
    CHECK(compiledTextureResult.success && compiledTextureResult.type == CompiledAssetType::Texture);
    TextureResource compiledTexture;
    CHECK(compiledTexture.Load("Saved/CompiledDefault.ppm") && compiledTexture.GetData() == texture.GetData());
    const std::vector<std::uint8_t> pngBytes{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48,
                                             0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00,
                                             0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x08,
                                             0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99,
                                             0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    CHECK(PlatformFile::WriteAllBytes("Saved/Test.png", pngBytes));
    TextureResource pngTexture;
    CHECK(pngTexture.Load("Saved/Test.png"));
    CHECK(pngTexture.GetWidth() == 1 && pngTexture.GetHeight() == 1 && pngTexture.GetData().size() == 4);
    const std::string hdrHeader = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
    std::vector<std::uint8_t> hdrBytes(hdrHeader.begin(), hdrHeader.end());
    hdrBytes.insert(hdrBytes.end(), {128, 64, 32, 129});
    CHECK(PlatformFile::WriteAllBytes("Saved/Test.hdr", hdrBytes));
    TextureResource hdrTexture;
    CHECK(hdrTexture.Load("Saved/Test.hdr"));
    CHECK(hdrTexture.GetWidth() == 1 && hdrTexture.GetHeight() == 1 && hdrTexture.IsHdr() && hdrTexture.GetFloatData().size() == 4);
    CHECK(hdrTexture.GetFloatData()[0] > 0.0f && hdrTexture.GetByteSize() == 4 * sizeof(float));
    CHECK(PlatformFile::WriteAllText("Saved/TestPbr.mat",
                                     "color 0.2 0.3 0.4 1\nmetallic 0.8\nroughness 0.25\nemissive 0.1 0.2 0.3\ntexture "
                                     "Saved/Test.png\nnormalTexture Saved/Test.png\nmetallicTexture Saved/Test.png\nroughnessTexture "
                                     "Saved/Test.png\naoTexture Saved/Test.png\nemissiveTexture Saved/Test.png\n"));
    MaterialResource pbrMaterial;
    CHECK(pbrMaterial.Load("Saved/TestPbr.mat"));
    CHECK(std::abs(pbrMaterial.GetMetallic() - 0.8f) < 0.001f && std::abs(pbrMaterial.GetRoughness() - 0.25f) < 0.001f);
    CHECK(std::abs(pbrMaterial.GetEmissive().y - 0.2f) < 0.001f && pbrMaterial.GetTexturePath() == "Saved/Test.png");
    CHECK(pbrMaterial.GetNormalTexturePath() == "Saved/Test.png" && pbrMaterial.GetMetallicTexturePath() == "Saved/Test.png");
    CHECK(pbrMaterial.GetRoughnessTexturePath() == "Saved/Test.png" && pbrMaterial.GetAoTexturePath() == "Saved/Test.png" &&
          pbrMaterial.GetEmissiveTexturePath() == "Saved/Test.png");
    CHECK(PlatformFile::WriteAllText("Saved/TestInstance.mat", "parent Saved/TestPbr.mat\ncolor 0.9 0.1 0.2 1\nroughness 0.5\n"));
    MaterialResource materialInstance;
    CHECK(materialInstance.Load("Saved/TestInstance.mat"));
    CHECK(materialInstance.GetParentPath() == "Saved/TestPbr.mat" && std::abs(materialInstance.GetBaseColor().x - 0.9f) < 0.001f &&
          std::abs(materialInstance.GetMetallic() - 0.8f) < 0.001f && std::abs(materialInstance.GetRoughness() - 0.5f) < 0.001f &&
          materialInstance.GetNormalTexturePath() == "Saved/Test.png");
    ShaderResource shader;
    CHECK(shader.Load("assets/shaders/default.vert"));
    CHECK(shader.GetSource().find("uLightViewProjection") != std::string::npos);
    CHECK(PlatformFile::WriteAllText("Saved/Common.glsl", "vec3 IncludedColor() { return vec3(1.0); }\n"));
    CHECK(PlatformFile::WriteAllText("Saved/Variant.frag",
                                     "#version 330 core\n#pragma variant USE_FOG 0 1\n#include \"Common.glsl\"\nvoid main(){}\n"));
    const auto shaderVariants = ShaderPipeline::BuildVariants("Saved/Variant.frag");
    CHECK(shaderVariants.size() == 2 && shaderVariants[0].success && shaderVariants[0].source.find("IncludedColor") != std::string::npos);
    CHECK(shaderVariants[0].dependencies.size() == 2 && ShaderPipeline::WriteCache(shaderVariants[0], "Saved/ShaderCacheTest"));
    CHECK(PakArchive::PackDirectory("assets", "Saved/Test.tpak"));
    CHECK(PakArchive::List("Saved/Test.tpak").size() >= 4);
    const auto packedMesh = PakArchive::ReadFile("Saved/Test.tpak", "player.mesh");
    CHECK(packedMesh.has_value() && !packedMesh->empty());
    CHECK(PlatformFile::WriteAllBytes("Saved/FakeGame.exe", {0x4d, 0x5a, 0x00, 0x00}));
    const auto package = ProjectPackager::Package({"TinyEngine.tinyproject", "Saved/TestPackage", "Saved/FakeGame.exe"});
    CHECK(package.success && std::filesystem::is_regular_file(package.archive) && std::filesystem::is_regular_file(package.projectFile));
    CHECK(!PakArchive::List(package.archive).empty() &&
          PlatformFile::ReadAllText("Saved/TestPackage/Package.manifest").find("package.version=1") != std::string::npos);
    AssetRegistry registry;
    registry.Scan("assets");
    const auto meshInfo = registry.Find("assets/player.mesh");
    CHECK(meshInfo.has_value() && meshInfo->id.size() == 32);
    CHECK(registry.FindById(meshInfo->id).has_value());
    const auto shaderInfo = registry.Find("assets/shaders/default.frag");
    CHECK(shaderInfo.has_value() && shaderInfo->type == "Shader");
    CHECK(registry.MarkImported("assets/player.mesh"));
    CHECK(!registry.NeedsReimport("assets/player.mesh"));
    AssetDependencyGraph dependencyGraph;
    dependencyGraph.Build(registry);
    const auto dependencyClosure = dependencyGraph.ResolveClosure("assets/default.mat");
    CHECK(!dependencyClosure.empty() && !dependencyGraph.HasCycles());
    ResourceManager streamingResources;
    streamingResources.Initialize(nullptr, "assets");
    AssetStreamingManager streaming;
    streaming.Initialize(&streamingResources, 1);
    {
        auto handle = streaming.Request<MeshResource>("assets/player.mesh");
        CHECK(handle.Wait() && handle.IsReady());
    }
    streaming.Update();
    CHECK(streamingResources.GetCachedResourceCount() == 0);
    streaming.Shutdown();
    streamingResources.Shutdown();
    const auto cook = AssetCooker::Cook("assets", "Saved/CookedTest", "Saved/CookedTest.tpak");
    CHECK(cook.success && cook.cookedFiles + cook.skippedFiles >= 4);
    const auto incrementalCook = AssetCooker::Cook("assets", "Saved/CookedTest", "Saved/CookedTest.tpak");
    CHECK(incrementalCook.success && incrementalCook.skippedFiles >= 4 && incrementalCook.cookedFiles == 0);
    PlatformFile::ClearMounts();

    CHECK(TargetPlatformRegistry::Get().Find("windows") != nullptr);
    CHECK(TargetPlatformCooker::ParseConfiguration("shipping") == BuildConfiguration::Shipping);
    const auto platformCook =
        TargetPlatformCooker::Cook({"TinyEngine.tinyproject", "Saved/PlatformCookTest", "Windows", BuildConfiguration::Shipping, false});
    CHECK(platformCook.success && std::filesystem::is_regular_file(platformCook.archive) && platformCook.shaderVariants >= 2);
    VersionedDocument cookManifest;
    CHECK(VersionedTextArchive::Deserialize(PlatformFile::ReadAllText(platformCook.manifest), cookManifest));
    CHECK(cookManifest.fields["platform"] == "Windows" && cookManifest.fields["configuration"] == "Shipping");
    PlatformFile::ClearMounts();
    CHECK(PlatformFile::MountPak(platformCook.archive));
    MeshResource platformMesh;
    CHECK(platformMesh.Load("assets/player.mesh") && platformMesh.GetVertices().size() == 3);
    PlatformFile::ClearMounts();
    CommandletRegistry commandlets;
    RegisterBuiltInCommandlets(commandlets);
    CHECK(commandlets.Run("ValidateAssets", {"assets"}).exitCode == 0);
    CHECK(commandlets.Run("ValidateScenes", {"Saved"}).exitCode == 0);
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
    for (int index = 0; index < 180; ++index)
        physics.Step(physicsScene, 1.0f / 60.0f);
    CHECK(physicsScene.GetComponent<TransformComponent>(body)->position.y >= 0.49f);
    RaycastHit queryHit;
    CHECK(physics.SphereCast(physicsScene, {{0.0f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, 0.25f, 10.0f, queryHit));
    CHECK(!physics.Overlap(physicsScene, {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}).empty());
    CHECK(physics.GetDebugBoxes().size() == 2 && Length(queryHit.normal) > 0.9f);
    const Entity character = physicsScene.CreateEntity("Character");
    physicsScene.GetComponent<TransformComponent>(character)->position = {2.0f, 1.3f, 0.0f};
    physicsScene.AddComponent(character, ColliderComponent{ColliderShape::Capsule, {0.4f, 0.9f, 0.4f}, 0.4f, 1.8f});
    const auto characterMove = physics.MoveCharacterController(physicsScene, character, {0.5f, 0.0f, 0.0f});
    CHECK(characterMove.appliedDisplacement.x > 0.49f && characterMove.grounded);
    World jointWorld;
    const Entity jointFirst = jointWorld.CreateEntity("JointFirst"), jointSecond = jointWorld.CreateEntity("JointSecond");
    jointWorld.GetComponent<TransformComponent>(jointFirst)->position = {0.0f, 0.0f, 0.0f};
    jointWorld.GetComponent<TransformComponent>(jointSecond)->position = {4.0f, 0.0f, 0.0f};
    jointWorld.AddComponent(jointFirst, RigidBodyComponent{BodyType::Dynamic, 1.0f, {}, false});
    jointWorld.AddComponent(jointSecond, RigidBodyComponent{BodyType::Dynamic, 1.0f, {}, false});
    jointWorld.AddComponent(jointFirst, JointComponent{JointType::Distance, jointSecond, 2.0f, 1.0f});
    physics.Step(jointWorld, 1.0f / 60.0f);
    CHECK(std::abs(Length(jointWorld.GetComponent<TransformComponent>(jointFirst)->position -
                          jointWorld.GetComponent<TransformComponent>(jointSecond)->position) -
                   2.0f) < 0.01f);

    CHECK(PlatformFile::WriteAllText("Saved/Idle.anim", "duration 1\nkey 0 0 0 0 0 0 0 1 1 1\nkey 1 0 0 0 0 0 0 1 1 1\n"));
    CHECK(PlatformFile::WriteAllText("Saved/Run.anim", "duration 1\nkey 0 10 0 0 0 0 0 1 1 1\nkey 1 10 0 0 0 0 0 1 1 1\n"));
    World animationWorld;
    animationWorld.GetSystemManager().RegisterSystem<AnimationSystem>();
    const Entity animatedEntity = animationWorld.CreateEntity("Blend");
    animationWorld.AddComponent(animatedEntity, AnimationComponent{"Saved/Idle.anim", 0.0f, 1.0f, true, true, "Saved/Run.anim", 1.0f});
    animationWorld.Initialize();
    animationWorld.Update(0.5f);
    CHECK(std::abs(animationWorld.GetComponent<TransformComponent>(animatedEntity)->position.x - 5.0f) < 0.01f);
    animationWorld.Shutdown();

    Skeleton skeleton{{{"Root", -1, Mat4::Identity()}, {"Hand", 0, Mat4::Identity()}}};
    CHECK(skeleton.IsValid() && skeleton.FindBone("Hand") == 1);
    auto skeletalClip = std::make_shared<SkeletalClip>();
    skeletalClip->name = "Wave";
    skeletalClip->duration = 1.0f;
    skeletalClip->keyframes = {{0.0f, {BonePose{}, BonePose{}}}, {1.0f, {BonePose{{1.0f, 0.0f, 0.0f}}, BonePose{{0.0f, 1.0f, 0.0f}}}}};
    const auto halfPose = skeletalClip->Sample(0.5f, skeleton.bones.size());
    CHECK(halfPose.size() == 2 && std::abs(halfPose[0].position.x - 0.5f) < 0.001f);
    CHECK(BuildSkinMatrices(skeleton, halfPose).size() == 2);
    AnimationStateMachine stateMachine;
    CHECK(stateMachine.AddState("Wave", skeletalClip));
    CHECK(stateMachine.Update(0.25f, 2).size() == 2);

    Config config;
    config.Set("render.vsync", "true");
    config.Set("physics.fixedRate", "60");
    CHECK(config.Save("Saved/Test.ini"));
    Config loadedConfig;
    CHECK(loadedConfig.Load("Saved/Test.ini"));
    CHECK(loadedConfig.GetBool("render.vsync"));
    CHECK(loadedConfig.GetInt("physics.fixedRate") == 60);
    CrashReporter::Install("TinyEngineTest", "Saved/Crashes");
    CHECK(!CrashReporter::WriteReport("automated test", "Saved/Crashes").empty());
    AudioEngine audioMixer;
    audioMixer.SetBusVolume("Music", 0.35f);
    CHECK(std::abs(audioMixer.GetBusVolume("Music") - 0.35f) < 0.001f);

    InputSystem mappedInput;
    mappedInput.ClearBindings();
    mappedInput.BindAxis("Horizontal", "MoveX", 2.0f);
    mappedInput.BindAction("Fire", "Primary");
    InputState mappedState;
    mappedState.moveX = 0.25f;
    mappedState.primaryAction = true;
    mappedInput.Update(mappedState);
    CHECK(std::abs(mappedInput.GetAxis("Horizontal") - 0.5f) < 0.001f && mappedInput.WasPressed("Fire"));
    CHECK(mappedInput.SaveBindings("Saved/Test.input"));
    InputSystem loadedInput;
    CHECK(loadedInput.LoadBindings("Saved/Test.input"));
    loadedInput.Update(mappedState);
    CHECK(std::abs(loadedInput.GetAxis("Horizontal") - 0.5f) < 0.001f && loadedInput.IsDown("Fire"));

    UISystem ui;
    bool clicked = false;
    ui.AddPanel("hud", {0, 0, 300, 100});
    ui.AddButton("start", "Start", {10, 10, 120, 40}, [&clicked]() { clicked = true; });
    ui.Update(20, 20, true);
    ui.Update(20, 20, false);
    CHECK(clicked && ui.GetFocusedId() == "start" && ui.BuildDrawList().size() == 2);
    UISystem layoutUi;
    int activated = 0;
    layoutUi.SetViewportSize(800.0f, 600.0f);
    layoutUi.AddPanel("menu", {0, 0, 300, 160});
    layoutUi.SetAnchor("menu", UIAnchor::Center);
    layoutUi.SetLayout("menu", UILayout::Vertical, 8.0f, 10.0f);
    layoutUi.AddLabel("title", "TinyEngine", {0, 0, 100, 28});
    layoutUi.AddButton("play", "Play", {0, 0, 120, 36}, [&activated]() { ++activated; });
    CHECK(layoutUi.SetParent("title", "menu") && layoutUi.SetParent("play", "menu"));
    const auto &layoutCommands = layoutUi.BuildDrawList();
    CHECK(layoutCommands.size() == 3 && std::abs(layoutCommands[0].rect.x - 250.0f) < 0.001f);
    CHECK(layoutCommands[1].rect.x == 260.0f &&
          layoutCommands[2].rect.y == layoutCommands[1].rect.y + layoutCommands[1].rect.height + 8.0f);
    layoutUi.FocusNext();
    layoutUi.ActivateFocused();
    CHECK(activated == 1 && layoutUi.GetFocusedId() == "play");
    CHECK(layoutUi.SetScrollable("menu", true) && layoutUi.SetScrollOffset("menu", {0.0f, 12.0f}));
    layoutUi.SetDpiScale(2.0f);
    const auto &scaledCommands = layoutUi.BuildDrawList();
    CHECK(scaledCommands[0].rect.width == 600.0f && scaledCommands[1].fontSize == 36.0f && scaledCommands[1].clipRect.width > 0.0f);

    std::vector<std::uint32_t> codepoints;
    const std::string unicodeText = "Tiny引擎";
    CHECK(Utf8::Decode(unicodeText, codepoints) && codepoints.size() == 6 && Utf8::Encode(codepoints) == unicodeText);
    StringTable strings;
    strings.Set("en", "welcome", "Hello {0}");
    strings.Set("zh-CN", "welcome", "你好，{0}");
    CHECK(strings.Save("Saved/Test.loc"));
    StringTable loadedStrings;
    CHECK(loadedStrings.Load("Saved/Test.loc") && loadedStrings.Format("zh-CN", "welcome", {"Tiny"}) == "你好，Tiny");
    ParticleEmitterSettings particleSettings;
    particleSettings.maximumParticles = 8;
    particleSettings.emissionRate = 0.0f;
    particleSettings.lifetime = 1.0f;
    ParticleEmitter particles(particleSettings);
    particles.Burst(4);
    particles.Update(0.5f);
    CHECK(particles.GetParticles().size() == 4 && particles.GetParticles().front().age > 0.49f);
    particles.Update(0.6f);
    CHECK(particles.GetParticles().empty());

    NavigationGrid navigation;
    CHECK(navigation.Configure({}, 5, 5, 1.0f));
    for (int z = 0; z < 4; ++z)
        CHECK(navigation.SetWalkable(2, z, false));
    const auto path = navigation.FindPath({0.5f, 0.0f, 0.5f}, {4.5f, 0.0f, 0.5f}, false);
    CHECK(!path.empty() && std::any_of(path.begin(), path.end(), [](const Vec3 &point) { return point.z > 4.0f; }));

    BehaviorTree behavior;
    behavior.GetBlackboard().Set("ready", true);
    int actions = 0;
    auto sequence = std::make_unique<SequenceNode>();
    sequence->Add(std::make_unique<ConditionNode>([](const Blackboard &blackboard) {
        const bool *ready = blackboard.Get<bool>("ready");
        return ready && *ready;
    }));
    sequence->Add(std::make_unique<WaitNode>(0.1f));
    sequence->Add(std::make_unique<ActionNode>([&actions](Blackboard &, float) {
        ++actions;
        return BehaviorStatus::Success;
    }));
    behavior.SetRoot(std::move(sequence));
    CHECK(behavior.Tick(0.05f) == BehaviorStatus::Running);
    CHECK(behavior.Tick(0.05f) == BehaviorStatus::Success && actions == 1);

    WorldPartition partition;
    CHECK(partition.RegisterCell({{0, 0}, {{0, -1, 0}, {10, 1, 10}}, "near.scene"}));
    CHECK(partition.RegisterCell({{1, 0}, {{100, -1, 0}, {110, 1, 10}}, "far.scene"}));
    partition.SetStreamingDistances(5.0f, 10.0f);
    int loadedCells = 0, unloadedCells = 0;
    const auto loadCell = [&loadedCells](const WorldPartitionCell &) {
        ++loadedCells;
        return true;
    };
    const auto unloadCell = [&unloadedCells](const WorldPartitionCell &) { ++unloadedCells; };
    partition.Update({2, 0, 2}, loadCell, unloadCell);
    CHECK(partition.GetLoadedCells().size() == 1 && loadedCells == 1);
    partition.Update({105, 0, 2}, loadCell, unloadCell);
    CHECK(partition.GetLoadedCells().size() == 1 && loadedCells == 2 && unloadedCells == 1);

    std::vector<std::string> moduleEvents;
    ModuleManager modules;
    CHECK(modules.Register(std::make_unique<TestModule>("Gameplay", std::vector<std::string>{"Core"}, moduleEvents)));
    CHECK(modules.Register(std::make_unique<TestModule>("Core", std::vector<std::string>{}, moduleEvents)));
    CHECK(modules.StartupAll());
    modules.ShutdownAll();
    CHECK(moduleEvents == std::vector<std::string>({"start:Core", "start:Gameplay", "stop:Gameplay", "stop:Core"}));

    NetworkSnapshot firstSnapshot{1, 1.0, {{MakeEntity(7, 2), {0, 0, 0}, {}, {1, 0, 0}}}};
    NetworkSnapshot secondSnapshot{2, 2.0, {{MakeEntity(7, 2), {10, 0, 0}, {}, {1, 0, 0}}}};
    NetworkSnapshot decodedSnapshot;
    CHECK(SnapshotCodec::Decode(SnapshotCodec::Encode(firstSnapshot), decodedSnapshot));
    CHECK(decodedSnapshot.entities.size() == 1 && decodedSnapshot.entities[0].entity == MakeEntity(7, 2));
    ReplayStream replay;
    replay.Record(firstSnapshot);
    replay.Record(secondSnapshot);
    std::vector<ReplicatedEntityState> sampledStates;
    CHECK(replay.Sample(1.5, sampledStates) && sampledStates.size() == 1 && std::abs(sampledStates[0].position.x - 5.0f) < 0.001f);
    CHECK(replay.Save("Saved/Test.replay"));
    ReplayStream loadedReplay;
    CHECK(loadedReplay.Load("Saved/Test.replay") && std::abs(loadedReplay.GetDuration() - 1.0) < 0.001);

    TransformTrack transformTrack;
    transformTrack.AddKey({0.0f, {{0, 0, 0}, {}, {1, 1, 1}}});
    transformTrack.AddKey({1.0f, {{4, 0, 0}, {}, {1, 1, 1}}});
    CHECK(std::abs(transformTrack.Sample(0.25f).position.x - 1.0f) < 0.001f);
    SequencePlayer sequencePlayer;
    std::vector<std::string> sequenceEvents;
    sequencePlayer.SetDuration(1.0f);
    sequencePlayer.SetEvents({{0.5f, "Half"}});
    sequencePlayer.SetEventCallback([&sequenceEvents](const std::string &name) { sequenceEvents.push_back(name); });
    sequencePlayer.Play();
    sequencePlayer.Update(0.75f);
    CHECK(sequenceEvents == std::vector<std::string>{"Half"} && sequencePlayer.IsPlaying());

    HeightField terrain;
    CHECK(terrain.Configure(2, 2, 1.0f, {0.0f, 2.0f, 2.0f, 4.0f}));
    CHECK(std::abs(terrain.SampleHeight(0.5f, 0.5f) - 2.0f) < 0.001f);
    const auto terrainMesh = terrain.BuildMesh();
    CHECK(terrainMesh.vertices.size() == 4 && terrainMesh.indices.size() == 6);
    ImageSequencePlayer imageSequence;
    CHECK(imageSequence.SetFrames({"frame0.png", "frame1.png", "frame2.png"}, 2.0f));
    imageSequence.Play();
    imageSequence.Update(0.6f);
    CHECK(imageSequence.GetFrameIndex() == 1 && imageSequence.GetCurrentFrame() == "frame1.png");

    if (failures == 0)
        std::cout << "All TinyEngine tests passed\n";
    return failures == 0 ? 0 : 1;
}
