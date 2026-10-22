// 文件：tests/engine_test.cpp
// 作用：覆盖引擎核心模块和跨系统集成行为的自动化测试。
// 约定：测试必须可重复执行，不依赖执行顺序或开发机上的隐式状态。
// 类型：实现文件。

#include "tinyengine/ai/behavior_tree.h"
#include "tinyengine/animation/skeletal_animation.h"
#include "tinyengine/audio/audio_engine.h"
#include "tinyengine/automation/automation.h"
#include "tinyengine/build/asset_compiler.h"
#include "tinyengine/build/build_compatibility.h"
#include "tinyengine/build/target_platform.h"
#include "tinyengine/cinematic/sequencer.h"
#include "tinyengine/core/config.h"
#include "tinyengine/core/crash_reporter.h"
#include "tinyengine/core/module.h"
#include "tinyengine/diagnostics/diagnostics.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/ecs/reflection.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/ecs/systems.h"
#include "tinyengine/gameplay/gameplay.h"
#include "tinyengine/input/input_system.h"
#include "tinyengine/localization/localization.h"
#include "tinyengine/math/math.h"
#include "tinyengine/media/image_sequence.h"
#include "tinyengine/navigation/navigation.h"
#include "tinyengine/network/replication.h"
#include "tinyengine/physics/physics_world.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/plugin/plugin_manager.h"
#include "tinyengine/project/project.h"
#include "tinyengine/project/project_packager.h"
#include "tinyengine/render/environment_baker.h"
#include "tinyengine/render/material_graph.h"
#include "tinyengine/render/renderer.h"
#include "tinyengine/render/shader_pipeline.h"
#include "tinyengine/resource/asset_cooker.h"
#include "tinyengine/resource/asset_maintenance.h"
#include "tinyengine/resource/asset_registry.h"
#include "tinyengine/resource/asset_streaming.h"
#include "tinyengine/resource/pak_archive.h"
#include "tinyengine/resource/resource.h"
#include "tinyengine/resource/resource_manager.h"
#include "tinyengine/serialization/archive.h"
#include "tinyengine/task/job_system.h"
#include "tinyengine/terrain/height_field.h"
#include "tinyengine/ui/text_layout.h"
#include "tinyengine/ui/ui_system.h"
#include "tinyengine/vfx/particle_system.h"
#include "tinyengine/world/world_partition.h"
#ifdef TINYENGINE_HAS_ASSIMP
#include "tinyengine/import/assimp_importer.h"
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace
{
int failures = 0;
bool pluginStarted = false;
int pluginTicks = 0;

int TINY_PLUGIN_CALL StartTestPlugin(const TinyPluginHostAPI* host)
{
    pluginStarted = host && host->abi_version == TINY_PLUGIN_ABI_VERSION;
    return pluginStarted ? 1 : 0;
}
void TINY_PLUGIN_CALL StopTestPlugin()
{
    pluginStarted = false;
}
void TINY_PLUGIN_CALL TickTestPlugin(double)
{
    ++pluginTicks;
}

class TestModule final : public tinyengine::IModule
{
  public:
    TestModule(std::string name, std::vector<std::string> dependencies, std::vector<std::string>& events)
        : m_name(std::move(name)), m_dependencies(std::move(dependencies)), m_events(events)
    {
    }
    const char* GetName() const override
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
    void shutdown() override
    {
        m_events.push_back("stop:" + m_name);
    }

  private:
    std::string m_name;
    std::vector<std::string> m_dependencies;
    std::vector<std::string>& m_events;
};

void check(const bool condition, const char* expression, const int line)
{
    if (!condition)
    {
        std::cerr << "FAILED line " << line << ": " << expression << '\n';
        ++failures;
    }
}
} // namespace
#define CHECK(value) check((value), #value, __LINE__)

int main()
{
    using namespace tinyengine;

    CHECK(std::abs(length(Vec3{3.0f, 4.0f, 0.0f}) - 5.0f) < 0.0001f);
    CHECK(dot(Vec3{1, 0, 0}, Vec3{0, 1, 0}) == 0.0f);
    const Aabb firstBounds{{-1, -1, -1}, {1, 1, 1}};
    const Aabb secondBounds{{0, 0, 0}, {2, 2, 2}};
    CHECK(firstBounds.intersects(secondBounds));
    const Mat4 orthographicValue = orthographic(-2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    CHECK(std::abs(orthographicValue.at(0, 0) - 0.5f) < 0.0001f &&
          std::abs(orthographicValue.at(1, 1) - 1.0f) < 0.0001f);

    RenderGraph renderGraph;
    const auto graphA = renderGraph.createResource({RenderGraphResourceType::Texture, "A", 64, 64, 1, 1});
    const auto graphB = renderGraph.createResource({RenderGraphResourceType::Texture, "B", 64, 64, 1, 1});
    const auto graphOutput = renderGraph.importResource({RenderGraphResourceType::Texture, "Output", 64, 64, 1, 1});
    std::vector<int> graphExecution;
    renderGraph.addPass({"Write A", {}, {graphA}, [&] { graphExecution.push_back(1); }});
    renderGraph.addPass({"Read A", {graphA}, {}, [&] { graphExecution.push_back(2); }, true});
    renderGraph.addPass({"Write B", {}, {graphB}, [&] { graphExecution.push_back(3); }});
    renderGraph.addPass({"Composite", {graphB}, {graphOutput}, [&] { graphExecution.push_back(4); }, true});
    std::string graphError;
    CHECK(renderGraph.compile(&graphError) && renderGraph.execute(&graphError));
    CHECK(graphExecution == std::vector<int>({1, 2, 3, 4}));
    CHECK(renderGraph.getLifetime(graphA)->aliasSlot == renderGraph.getLifetime(graphB)->aliasSlot &&
          renderGraph.getPhysicalResourceCount() == 2);
    RenderGraph invalidGraph;
    const auto neverWritten =
        invalidGraph.createResource({RenderGraphResourceType::Texture, "NeverWritten", 1, 1, 1, 1});
    invalidGraph.addPass({"Invalid Read", {neverWritten}, {}, {}, true});
    CHECK(!invalidGraph.compile(&graphError) && graphError.find("before it is written") != std::string::npos);

    std::vector<RenderLight> clusterLights(2);
    clusterLights[0].type = RenderLightType::Directional;
    clusterLights[1].type = RenderLightType::Point;
    clusterLights[1].position = {0.0f, 0.0f, -5.0f};
    clusterLights[1].range = 2.0f;
    const auto clustered = ClusteredLightBuilder::build(
        clusterLights, Mat4::identity(), perspective(60.0f, 16.0f / 9.0f, 0.1f, 100.0f), 1280, 720, 0.1f, 100.0f);
    CHECK(clustered.getClusterCount() == 16 * 9 * 24 && clustered.offsets.size() == clustered.getClusterCount());
    CHECK(clustered.lightIndices.size() >= clustered.getClusterCount());

    BinaryWriter binaryWriter;
    binaryWriter.writeU32(0x12345678u);
    binaryWriter.writeFloat(3.5f);
    binaryWriter.writeString("TinyEngine");
    BinaryReader binaryReader(binaryWriter.GetData());
    std::uint32_t archiveInteger = 0;
    float archiveFloat = 0.0f;
    std::string archiveString;
    CHECK(binaryReader.readU32(archiveInteger) && archiveInteger == 0x12345678u);
    CHECK(binaryReader.readFloat(archiveFloat) && std::abs(archiveFloat - 3.5f) < 0.001f);
    CHECK(binaryReader.readString(archiveString) && archiveString == "TinyEngine" &&
          binaryReader.getRemainingBytes() == 0);
    VersionedDocument versioned{"TestData", 1, {{"oldName", "value"}}};
    MigrationRegistry migrations;
    CHECK(migrations.Register("TestData", 1,
                              [](VersionedDocument& document, std::string&)
                              {
                                  document.fields["newName"] = document.fields["oldName"];
                                  document.fields.erase("oldName");
                                  return true;
                              }));
    CHECK(migrations.migrate(versioned, 2) && versioned.version == 2 && versioned.fields["newName"] == "value");
    CHECK(VersionedTextArchive::saveAtomic("saved/Test.archive", versioned, false));
    VersionedDocument loadedVersioned;
    CHECK(VersionedTextArchive::deserialize(PlatformFile::readAllText("saved/Test.archive"), loadedVersioned));
    CHECK(loadedVersioned.version == 2 && loadedVersioned.fields["newName"] == "value");
    const std::filesystem::path atomicTextPath = "saved/AtomicText.txt";
    std::filesystem::remove(atomicTextPath);
    std::filesystem::remove(atomicTextPath.string() + ".bak");
    std::filesystem::remove(atomicTextPath.string() + ".tmp");
    CHECK(PlatformFile::writeAllTextAtomic(atomicTextPath, "before", true));
    CHECK(PlatformFile::writeAllTextAtomic(atomicTextPath, "after", true));
    CHECK(PlatformFile::readAllText(atomicTextPath) == "after");
    CHECK(PlatformFile::readAllText(atomicTextPath.string() + ".bak") == "before");
    CHECK(!std::filesystem::exists(atomicTextPath.string() + ".tmp"));

    World world;
    const Entity first = world.createEntity("First");
    world.AddComponent(first, MeshComponent{"mesh", "material"});
    world.destroyEntity(first);
    CHECK(!world.isAlive(first));
    CHECK(world.GetComponent<MeshComponent>(first) == nullptr);
    const Entity replacement = world.createEntity("Replacement");
    CHECK(entityIndex(first) == entityIndex(replacement));
    CHECK(first != replacement);
    const auto& reflection = ReflectionRegistry::Get();
    const auto* reflectedTransform = reflection.findComponent("Transform");
    const auto* reflectedRigidBody = reflection.findComponent("Rigid Body");
    CHECK(reflectedTransform != nullptr && !reflectedTransform->removable &&
          reflectedTransform->properties.size() == 3);
    CHECK(reflectedRigidBody != nullptr && reflectedRigidBody->removable &&
          !reflectedRigidBody->has(world, replacement));
    reflectedRigidBody->add(world, replacement);
    CHECK(reflectedRigidBody->has(world, replacement) && reflectedRigidBody->properties.front().enumNames.size() == 3);
    const auto massProperty = std::find_if(reflectedRigidBody->properties.begin(), reflectedRigidBody->properties.end(),
                                           [](const ReflectedProperty& property) { return property.name == "Mass"; });
    CHECK(massProperty != reflectedRigidBody->properties.end());
    if (massProperty != reflectedRigidBody->properties.end())
        *static_cast<float*>(massProperty->access(world, replacement)) = 4.5f;
    CHECK(std::abs(world.GetComponent<RigidBodyComponent>(replacement)->mass - 4.5f) < 0.001f);
    reflectedRigidBody->remove(world, replacement);
    CHECK(!reflectedRigidBody->has(world, replacement));

    const Entity child = world.createEntity("Child");
    CHECK(world.setParent(child, replacement));
    CHECK(!world.setParent(replacement, child));
    world.AddComponent(replacement, JointComponent{JointType::Distance, child, 2.0f, 0.75f});
    world.AddComponent(replacement, LodComponent{"assets/player.mesh", "assets/player.mesh", 10.0f, 30.0f});
    world.AddComponent(replacement, PawnComponent{});
    world.AddComponent(replacement, CharacterControllerComponent{});
    AnimationComponent serializedAnimation;
    serializedAnimation.clipPath = "saved/Compressed.skanim";
    serializedAnimation.skeletonPath = "saved/Test.skeleton";
    serializedAnimation.graphPath = "saved/Locomotion.animgraph";
    serializedAnimation.graphParameter = "Speed";
    serializedAnimation.graphParameterValue = 2.5f;
    world.AddComponent(replacement, serializedAnimation);
    CHECK(SceneSerializer::Save(world, "saved/Test.scene"));
    CHECK(!SceneSerializer::Save(world, {}));
    World loaded;
    CHECK(SceneSerializer::load(loaded, "saved/Test.scene"));
    CHECK(loaded.GetEntityManager().GetAliveEntities().size() == 2);
    bool loadedJointValid = false;
    for (const Entity entity : loaded.GetEntityManager().GetAliveEntities())
        if (const auto* joint = loaded.GetComponent<JointComponent>(entity))
            loadedJointValid = loaded.isAlive(joint->connectedEntity) && std::abs(joint->restLength - 2.0f) < 0.001f;
    CHECK(loadedJointValid);
    bool loadedLod = false;
    for (const Entity entity : loaded.GetEntityManager().GetAliveEntities())
        if (const auto* lod = loaded.GetComponent<LodComponent>(entity))
            loadedLod = std::abs(lod->level2Distance - 30.0f) < 0.001f;
    CHECK(loadedLod);
    bool loadedCharacter = false;
    for (const Entity entity : loaded.GetEntityManager().GetAliveEntities())
        loadedCharacter = loadedCharacter || (loaded.HasComponent<PawnComponent>(entity) &&
                                              loaded.HasComponent<CharacterControllerComponent>(entity));
    CHECK(loadedCharacter);
    bool loadedAnimationGraph = false;
    for (const Entity entity : loaded.GetEntityManager().GetAliveEntities())
        if (const auto* animation = loaded.GetComponent<AnimationComponent>(entity))
            loadedAnimationGraph = animation->graphPath == "saved/Locomotion.animgraph" &&
                                   animation->graphParameter == "Speed" &&
                                   std::abs(animation->graphParameterValue - 2.5f) < 0.001f;
    CHECK(loadedAnimationGraph);
    const std::string snapshot = SceneSerializer::serialize(loaded);
    loaded.createEntity("Temporary");
    CHECK(SceneSerializer::deserialize(loaded, snapshot));
    CHECK(loaded.GetEntityManager().GetAliveEntities().size() == 2);
    CHECK(Prefab::Save(loaded, *loaded.GetEntityManager().GetAliveEntities().begin(), "saved/Test.prefab"));
    World prefabTarget;
    CHECK(!Prefab::Instantiate(prefabTarget, "saved/Test.prefab").empty());

    // 角色预制体保存并重新实例化后，控制方式和移动参数都应保留。
    World characterPrefabSource;
    const Entity characterPrefabRoot = characterPrefabSource.createEntity("Character prefab");
    characterPrefabSource.AddComponent(characterPrefabRoot, PawnComponent{false, 3});
    CharacterControllerComponent prefabController;
    prefabController.movementSpeed = 6.5f;
    prefabController.sprintSpeed = 12.0f;
    prefabController.jumpSpeed = 7.5f;
    prefabController.gravity = 22.0f;
    prefabController.airControl = 0.6f;
    prefabController.grounded = true;
    prefabController.velocity = {1.0f, 2.0f, 3.0f};
    characterPrefabSource.AddComponent(characterPrefabRoot, prefabController);
    CHECK(Prefab::Save(characterPrefabSource, characterPrefabRoot, "saved/Character.prefab"));
    World savedCharacterPrefab;
    CHECK(SceneSerializer::load(savedCharacterPrefab, "saved/Character.prefab"));
    CHECK(savedCharacterPrefab.GetEntityManager().GetAliveEntities().size() == 1);
    const auto& savedCharacters = savedCharacterPrefab.GetEntityManager().GetAliveEntities();
    const Entity savedCharacter = savedCharacters.empty() ? kInvalidEntity : *savedCharacters.begin();
    CHECK(savedCharacterPrefab.HasComponent<PawnComponent>(savedCharacter));
    CHECK(savedCharacterPrefab.HasComponent<CharacterControllerComponent>(savedCharacter));
    World characterPrefabTarget;
    const auto characterRoots = Prefab::Instantiate(characterPrefabTarget, "saved/Character.prefab");
    CHECK(characterRoots.size() == 1);
    const Entity instancedCharacter = characterRoots.empty() ? kInvalidEntity : characterRoots.front();
    const auto* instancedPawn = characterPrefabTarget.GetComponent<PawnComponent>(instancedCharacter);
    CHECK(instancedPawn && !instancedPawn->playerControlled && instancedPawn->controllerIndex == 3);
    const auto* instancedController =
        characterPrefabTarget.GetComponent<CharacterControllerComponent>(instancedCharacter);
    CHECK(instancedController && std::abs(instancedController->movementSpeed - 6.5f) < 0.001f &&
          std::abs(instancedController->sprintSpeed - 12.0f) < 0.001f &&
          std::abs(instancedController->jumpSpeed - 7.5f) < 0.001f &&
          std::abs(instancedController->gravity - 22.0f) < 0.001f &&
          std::abs(instancedController->airControl - 0.6f) < 0.001f && instancedController->grounded &&
          length(instancedController->velocity - Vec3{1.0f, 2.0f, 3.0f}) < 0.001f);

    World lightingWorld;
    for (int index = 0; index < 6; ++index)
    {
        LightComponent light;
        light.color = {1.0f, 0.8f, 0.6f};
        light.intensity = static_cast<float>(index + 1);
        lightingWorld.AddComponent(lightingWorld.createEntity("Light"), light);
    }
    Renderer lightingRenderer;
    CHECK(lightingRenderer.initialize(std::make_unique<NullRenderDevice>()));
    lightingRenderer.buildRenderQueue(lightingWorld);
    CHECK(lightingRenderer.GetFrameView().lightCount == 6);
    VfxSystem renderVfx;
    ParticleEmitterSettings renderParticleSettings;
    renderParticleSettings.emissionRate = 0.0f;
    const auto renderEmitter = renderVfx.createEmitter(renderParticleSettings);
    renderVfx.findEmitter(renderEmitter)->burst(3);
    lightingRenderer.setVfxSystem(&renderVfx);
    lightingRenderer.renderFrame(lightingWorld);
    CHECK(lightingRenderer.getLastDrawCallCount() == 3);
    CHECK(lightingRenderer.GetLastRenderGraph().getPassCount() == 9);
    lightingRenderer.shutdown();

    ProjectSettings project;
    project.name = "TestGame";
    project.startupScene = "saved/Test.scene";
    project.inputBindings = "assets/default.input";
    project.environmentTexture = "assets/studio.hdr";
    CHECK(project.Save("saved/Test.tinyproject"));
    ProjectSettings loadedProject;
    CHECK(loadedProject.load("saved/Test.tinyproject"));
    CHECK(loadedProject.name == "TestGame");
    CHECK(loadedProject.inputBindings == "assets/default.input" &&
          loadedProject.environmentTexture == "assets/studio.hdr");
    SaveGame saveGame;
    saveGame.set("player.level", "7");
    saveGame.set("player.name", "Tiny");
    CHECK(saveGame.Save("saved/Test.save"));
    SaveGame loadedSave;
    CHECK(loadedSave.load("saved/Test.save") && loadedSave.getInt("player.level") == 7 &&
          loadedSave.getString("player.name") == "Tiny");

    JobSystem jobs;
    jobs.initialize(2);
    std::atomic<int> order{0};
    auto dependency = jobs.createJob([&order]() { order.store(1); });
    auto dependent = jobs.createJob(
        [&order]()
        {
            if (order.load() == 1)
                order.store(2);
        });
    jobs.addDependency(dependent, dependency);
    jobs.submit(dependent);
    jobs.submit(dependency);
    jobs.wait(dependent);
    CHECK(order.load() == 2);
    auto failing = jobs.createJob([]() { throw std::runtime_error("expected"); });
    jobs.submit(failing);
    bool exceptionObserved = false;
    try
    {
        jobs.wait(failing);
    }
    catch (const std::runtime_error&)
    {
        exceptionObserved = true;
    }
    CHECK(exceptionObserved);
    jobs.shutdown();

    TimerManager timers;
    int timerExecutions = 0;
    const auto timer = timers.setTimer(
        0.1f, [&timerExecutions]() { ++timerExecutions; }, true);
    timers.Update(0.25f);
    CHECK(timerExecutions == 1 && timers.isActive(timer));
    CHECK(timers.clearTimer(timer));
    World gameplayWorld;
    const Entity pawn = gameplayWorld.createEntity("Pawn");
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
    gameplay.initialize(&gameplayWorld, &gameplayInput, &gameplayPhysics);
    gameplay.startPlay();
    gameplay.Update(0.5f);
    CHECK(gameplayWorld.GetComponent<TransformComponent>(pawn)->position.x > 1.9f);
    gameplay.stopPlay();
    gameplay.shutdown();

    DiagnosticsService diagnostics;
    diagnostics.initialize();
    int changedValue = 0;
    CHECK(diagnostics.GetConsole().registerVariable("test.value", 1, "Test variable", false,
                                                    [&changedValue](const ConsoleValue& value)
                                                    { changedValue = std::get<int>(value); }));
    CHECK(diagnostics.GetConsole().execute("test.value 7") == "test.value = 7" && changedValue == 7);
    diagnostics.GetStats().set("test.stat", 2.0);
    diagnostics.GetStats().Add("test.stat", 3.0);
    CHECK(diagnostics.GetStats().Get("test.stat").value == 5.0);
    diagnostics.shutdown();

    MeshResource mesh;
    CHECK(mesh.load("assets/player.mesh"));
    CHECK(mesh.GetVertices().size() == 3);
    CHECK(mesh.GetIndices().size() == 3);
    CHECK(length(mesh.GetVertices().front().tangent) > 0.9f &&
          std::abs(dot(mesh.GetVertices().front().normal, mesh.GetVertices().front().tangent)) < 0.001f);
    const auto compiledMeshResult = PlatformAssetCompiler::compile("assets/player.mesh", "saved/CompiledPlayer.mesh");
    CHECK(compiledMeshResult.success && compiledMeshResult.type == CompiledAssetType::Mesh);
    MeshResource compiledMesh;
    CHECK(compiledMesh.load("saved/CompiledPlayer.mesh") &&
          compiledMesh.GetVertices().size() == mesh.GetVertices().size());
#ifdef TINYENGINE_HAS_ASSIMP
    const auto supportedModelExtensions = AssimpImporter::GetSupportedExtensions();
    CHECK(std::find(supportedModelExtensions.begin(), supportedModelExtensions.end(), ".fbx") !=
          supportedModelExtensions.end());
    CHECK(std::find(supportedModelExtensions.begin(), supportedModelExtensions.end(), ".obj") !=
          supportedModelExtensions.end());
    // 仓库只保留这一份极小源模型，确保导入回归使用真实文件，而不是依赖开发机上的大型样例。
    const std::filesystem::path importFixture = "tests/fixtures/Import/triangle.obj";
    CHECK(std::filesystem::is_regular_file(importFixture));
    ModelImportOptions importOptions;
    importOptions.resourcePrefix = "assets/ImportedTest";
    const auto imported = AssimpImporter::importModel(importFixture, "saved/ImportedTest", importOptions);
    CHECK(imported.success && !imported.prefabPath.empty());
    const auto importedMesh = std::find_if(imported.assets.begin(), imported.assets.end(),
                                           [](const ImportedAsset& asset) { return asset.type == "Mesh"; });
    CHECK(importedMesh != imported.assets.end());
    MeshResource convertedMesh;
    CHECK(importedMesh != imported.assets.end() && convertedMesh.load(importedMesh->path.string()));
    CHECK(convertedMesh.GetVertices().size() == 3 && std::abs(convertedMesh.GetVertices()[1].uv.x - 1.0f) < 0.001f);
    AssetRegistry importedRegistry;
    importedRegistry.scan("saved/ImportedTest", false);
    const auto importedPrefab = importedRegistry.Find(imported.prefabPath.generic_string());
    CHECK(importedPrefab.has_value() && importedPrefab->dependencies.size() >= 2 &&
          importedPrefab->importSettings.count("source") == 1);
    std::string importedThumbnailError;
    CHECK(importedPrefab && AssetMaintenance::generateThumbnail(*importedPrefab, "saved/ImportedPrefabThumbnail.ppm",
                                                                96, &importedThumbnailError));
    CHECK(std::filesystem::file_size("saved/ImportedPrefabThumbnail.ppm") > 1024);
    AssetImportQueue importQueue;
    importQueue.initialize(nullptr);
    const auto queuedImport = importQueue.submit(importFixture, "saved/QueuedImport", importOptions);
    const auto queuedSnapshot = importQueue.Get(queuedImport);
    CHECK(queuedSnapshot.status == AssetImportStatus::Completed && queuedSnapshot.result.success);
    importQueue.clearFinished();
#endif
    TextureResource texture;
    CHECK(texture.load("assets/default.ppm"));
    CHECK(texture.getWidth() == 2 && texture.getHeight() == 2 && texture.GetData().size() == 16);
    const auto compiledTextureResult =
        PlatformAssetCompiler::compile("assets/default.ppm", "saved/CompiledDefault.ppm");
    CHECK(compiledTextureResult.success && compiledTextureResult.type == CompiledAssetType::Texture);
    TextureResource compiledTexture;
    CHECK(compiledTexture.load("saved/CompiledDefault.ppm") && compiledTexture.GetData() == texture.GetData());
    const auto bakedEnvironment = EnvironmentBaker::bake("assets/default.ppm", "saved/Environment", 16);
    TextureResource bakedIrradiance, bakedPrefilter, bakedBrdf;
    CHECK(bakedEnvironment.success && bakedIrradiance.load(bakedEnvironment.irradiancePath.generic_string()) &&
          bakedPrefilter.load(bakedEnvironment.prefilterPath.generic_string()) &&
          bakedBrdf.load(bakedEnvironment.brdfLutPath.generic_string()) && bakedIrradiance.isHdr() &&
          bakedIrradiance.getWidth() == 16 * 6 && bakedIrradiance.getHeight() == 16 &&
          bakedPrefilter.getWidth() == 32 * 6 && bakedPrefilter.getHeight() == 32 * 6 && bakedBrdf.getWidth() == 128);
    CHECK(PlatformFile::readAllText(bakedEnvironment.environmentPath).find("sky \"../../assets/default.ppm\"") !=
          std::string::npos);
    ResourceManager environmentResources;
    environmentResources.initialize(nullptr, bakedEnvironment.environmentPath.parent_path());
    Renderer environmentRenderer;
    environmentRenderer.setResourceManager(&environmentResources);
    environmentRenderer.setEnvironmentPath(
        std::filesystem::absolute(bakedEnvironment.environmentPath).lexically_normal().generic_string());
    CHECK(environmentRenderer.initialize(std::make_unique<NullRenderDevice>()));
    World environmentWorld;
    environmentWorld.AddComponent(environmentWorld.createEntity("Camera"), CameraComponent{});
    environmentRenderer.buildRenderQueue(environmentWorld);
    CHECK(environmentRenderer.GetFrameView().environmentSky &&
          environmentRenderer.GetFrameView().environmentSky->isReady());
    environmentRenderer.shutdown();
    environmentResources.shutdown();
    const std::vector<std::uint8_t> pngBytes{
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0d, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
        0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    CHECK(PlatformFile::writeAllBytes("saved/Test.png", pngBytes));
    TextureResource pngTexture;
    CHECK(pngTexture.load("saved/Test.png"));
    CHECK(pngTexture.getWidth() == 1 && pngTexture.getHeight() == 1 && pngTexture.GetData().size() == 4);
    const std::string hdrHeader = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
    std::vector<std::uint8_t> hdrBytes(hdrHeader.begin(), hdrHeader.end());
    hdrBytes.insert(hdrBytes.end(), {128, 64, 32, 129});
    CHECK(PlatformFile::writeAllBytes("saved/Test.hdr", hdrBytes));
    TextureResource hdrTexture;
    CHECK(hdrTexture.load("saved/Test.hdr"));
    CHECK(hdrTexture.getWidth() == 1 && hdrTexture.getHeight() == 1 && hdrTexture.isHdr() &&
          hdrTexture.GetFloatData().size() == 4);
    CHECK(hdrTexture.GetFloatData()[0] > 0.0f && hdrTexture.getByteSize() == 4 * sizeof(float));
    CHECK(PlatformFile::writeAllText(
        "saved/TestPbr.mat",
        "color 0.2 0.3 0.4 1\nmetallic 0.8\nroughness 0.25\nemissive 0.1 0.2 0.3\ntexture "
        "saved/Test.png\nnormalTexture saved/Test.png\nmetallicTexture saved/Test.png\nroughnessTexture "
        "saved/Test.png\naoTexture saved/Test.png\nemissiveTexture saved/Test.png\n"));
    MaterialResource pbrMaterial;
    CHECK(pbrMaterial.load("saved/TestPbr.mat"));
    CHECK(std::abs(pbrMaterial.getMetallic() - 0.8f) < 0.001f && std::abs(pbrMaterial.getRoughness() - 0.25f) < 0.001f);
    CHECK(std::abs(pbrMaterial.getEmissive().y - 0.2f) < 0.001f && pbrMaterial.GetTexturePath() == "saved/Test.png");
    CHECK(pbrMaterial.GetNormalTexturePath() == "saved/Test.png" &&
          pbrMaterial.GetMetallicTexturePath() == "saved/Test.png");
    CHECK(pbrMaterial.GetRoughnessTexturePath() == "saved/Test.png" &&
          pbrMaterial.GetAoTexturePath() == "saved/Test.png" &&
          pbrMaterial.GetEmissiveTexturePath() == "saved/Test.png");
    CHECK(PlatformFile::writeAllText("saved/TestInstance.mat",
                                     "parent saved/TestPbr.mat\ncolor 0.9 0.1 0.2 1\nroughness 0.5\n"));
    MaterialResource materialInstance;
    CHECK(materialInstance.load("saved/TestInstance.mat"));
    CHECK(materialInstance.GetParentPath() == "saved/TestPbr.mat" &&
          std::abs(materialInstance.getBaseColor().x - 0.9f) < 0.001f &&
          std::abs(materialInstance.getMetallic() - 0.8f) < 0.001f &&
          std::abs(materialInstance.getRoughness() - 0.5f) < 0.001f &&
          materialInstance.GetNormalTexturePath() == "saved/Test.png");
    CHECK(PlatformFile::writeAllText(
        "saved/TestExplicitInstance.mat",
        "parent saved/TestPbr.mat\noverride color 0\noverride roughness 1\noverride packedTexture 1\n"
        "color 1 0 0 1\nroughness 0.4\npackedTexture saved/Test.png\npackedLayout RMA\n"
        "parameter uTint 0.1 0.2 0.3 1\n"));
    MaterialResource explicitInstance;
    CHECK(explicitInstance.load("saved/TestExplicitInstance.mat") && !explicitInstance.isOverridden("color") &&
          explicitInstance.isOverridden("roughness") && std::abs(explicitInstance.getBaseColor().x - 0.2f) < 0.001f &&
          std::abs(explicitInstance.getRoughness() - 0.4f) < 0.001f &&
          explicitInstance.GetPackedTextureLayout() == "RMA" && explicitInstance.GetParameters().count("uTint") == 1);
    CHECK(PlatformFile::writeAllText(
        "saved/Skinned.mesh",
        "TINY_MESH 3\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nvn 0 0 1\nvn 0 0 1\n"
        "vw 1 2 1 0 0 0 0 0 0\nvw 2 2 0.75 1 0.25 0 0 0 0\nvw 3 1 1 0 0 0 0 0 0\nf 1/1/1 2/2/2 3/3/3\n"));
    MeshResource skinnedMesh;
    CHECK(skinnedMesh.load("saved/Skinned.mesh") && skinnedMesh.GetVertices().size() == 3);
    CHECK(std::abs(skinnedMesh.GetVertices()[1].boneWeights.x - 0.75f) < 0.001f &&
          std::abs(skinnedMesh.GetVertices()[1].boneIndices.y - 1.0f) < 0.001f);
    CHECK(PlatformFile::writeAllText("saved/IndexedQuad.mesh", "TINY_MESH 2\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                                                               "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                                                               "vn 0 0 1\nvn 0 0 1\nvn 0 0 1\nvn 0 0 1\n"
                                                               "f 1/1/1 2/2/2 3/3/3\nf 1/1/1 3/3/3 4/4/4\n"));
    MeshResource indexedQuad;
    CHECK(indexedQuad.load("saved/IndexedQuad.mesh") && indexedQuad.GetVertices().size() == 4 &&
          indexedQuad.GetIndices().size() == 6 && indexedQuad.GetIndices()[0] == indexedQuad.GetIndices()[3]);
    ShaderResource shader;
    CHECK(shader.load("assets/shaders/default.vert"));
    CHECK(shader.GetSource().find("uShadowViewProjections") != std::string::npos &&
          shader.GetSource().find("normalMatrix") != std::string::npos);
    CHECK(PlatformFile::writeAllText("saved/Common.glsl", "vec3 IncludedColor() { return vec3(1.0); }\n"));
    CHECK(PlatformFile::writeAllText(
        "saved/Variant.frag",
        "#version 330 core\n#pragma variant USE_FOG 0 1\n#include \"Common.glsl\"\nvoid main(){}\n"));
    const auto shaderVariants = ShaderPipeline::BuildVariants("saved/Variant.frag");
    CHECK(shaderVariants.size() == 2 && shaderVariants[0].success &&
          shaderVariants[0].source.find("IncludedColor") != std::string::npos);
    CHECK(shaderVariants[0].dependencies.size() == 2 &&
          ShaderPipeline::writeCache(shaderVariants[0], "saved/ShaderCacheTest"));
    CHECK(PlatformFile::writeAllText("saved/Reflected.frag",
                                     "#version 330 core\nuniform float uStrength; //@range 0 4 //@default 2\n"
                                     "uniform vec4 uTint; //@default 1 0.5 0.25 1\nvoid main(){}\n"));
    const auto reflectedParameters = ShaderPipeline::ReflectParameters("saved/Reflected.frag");
    CHECK(reflectedParameters.size() == 2 && reflectedParameters[0].hasRange &&
          reflectedParameters[0].maximum == 4.0f && reflectedParameters[1].type == ShaderParameterType::Vec4);
    CHECK(PakArchive::packDirectory("assets", "saved/Test.tpak"));
    CHECK(PakArchive::List("saved/Test.tpak").size() >= 4);
    const auto packedMesh = PakArchive::ReadFile("saved/Test.tpak", "player.mesh");
    CHECK(packedMesh.has_value() && !packedMesh->empty());
    CHECK(PlatformFile::writeAllBytes("saved/FakeGame.exe", {0x4d, 0x5a, 0x00, 0x00}));
    CHECK(
        BuildCompatibility::Save(BuildCompatibility::sidecarPath("saved/FakeGame.exe"), BuildCompatibility::current()));
    const auto package =
        ProjectPackager::package({"Aster-Engine.tinyproject", "saved/TestPackage", "saved/FakeGame.exe"});
    for (const auto& error : package.errors)
        std::cerr << "Package regression: " << error << '\n';
    CHECK(package.success && std::filesystem::is_regular_file(package.archive) &&
          std::filesystem::is_regular_file(package.projectFile) && std::filesystem::is_regular_file(package.buildInfo));
    CHECK(!PakArchive::List(package.archive).empty() &&
          PlatformFile::readAllText("saved/TestPackage/Package.manifest").find("package.engineAbi=1") !=
              std::string::npos);
    auto incompatibleBuild = BuildCompatibility::current();
    ++incompatibleBuild.engineAbi;
    CHECK(BuildCompatibility::Save(BuildCompatibility::sidecarPath("saved/FakeGame.exe"), incompatibleBuild));
    const auto incompatiblePackage =
        ProjectPackager::package({"Aster-Engine.tinyproject", "saved/TestPackageRejected", "saved/FakeGame.exe"});
    CHECK(!incompatiblePackage.success && !incompatiblePackage.errors.empty());
    AssetRegistry registry;
    registry.scan("assets");
    const auto meshInfo = registry.Find("assets/player.mesh");
    CHECK(meshInfo.has_value() && meshInfo->id.size() == 32);
    CHECK(registry.FindById(meshInfo->id).has_value());
    const auto shaderInfo = registry.Find("assets/shaders/default.frag");
    CHECK(shaderInfo.has_value() && shaderInfo->type == "Shader");
    const std::filesystem::path registryMigration = "saved/RegistryMigration";
    std::filesystem::create_directories(registryMigration);
    CHECK(PlatformFile::writeAllText(registryMigration / "effect.vfx", "TINY_VFX 1\n"));
    CHECK(PlatformFile::writeAllText(registryMigration / "behavior.btgraph", "TINY_BTGRAPH 1\n"));
    CHECK(PlatformFile::writeAllText(registryMigration / "legacy.anim", "duration 1\n"));
    CHECK(PlatformFile::writeAllText(registryMigration / "graph.animgraph",
                                     "TINY_ANIMGRAPH 1\nentry \"Idle\"\nstate \"Idle\" \"assets/walk.skanim\"\n"));
    for (const auto& name : {"effect.vfx", "behavior.btgraph", "legacy.anim", "graph.animgraph"})
        CHECK(PlatformFile::writeAllText(registryMigration / (std::string(name) + ".meta"),
                                         "asset.id=" + std::string(32, name[0]) +
                                             "\nasset.type=Binary\nasset.importedStamp=0\ndependencies.count=0\n"));
    AssetRegistry migratedRegistry;
    migratedRegistry.scan(registryMigration);
    const auto migratedVfx = migratedRegistry.Find((registryMigration / "effect.vfx").generic_string());
    const auto migratedBehavior = migratedRegistry.Find((registryMigration / "behavior.btgraph").generic_string());
    const auto migratedAnimation = migratedRegistry.Find((registryMigration / "legacy.anim").generic_string());
    const auto migratedGraph = migratedRegistry.Find((registryMigration / "graph.animgraph").generic_string());
    CHECK(migratedVfx && migratedVfx->type == "VFX" && migratedBehavior && migratedBehavior->type == "BehaviorTree" &&
          migratedAnimation && migratedAnimation->type == "Animation" && migratedGraph &&
          migratedGraph->dependencies == std::vector<std::string>{"assets/walk.skanim"});
    CHECK(PlatformFile::readAllText(registryMigration / "graph.animgraph.meta").find("asset.schemaVersion=2") !=
          std::string::npos);
    const std::filesystem::path registryFilter = "saved/RegistryFilter";
    std::filesystem::remove_all(registryFilter);
    std::filesystem::create_directories(registryFilter);
    CHECK(PlatformFile::writeAllText(registryFilter / "Source.cpp", "int ignored = 0;\n"));
    CHECK(PlatformFile::writeAllText(registryFilter / "Header.hpp", "#pragma once\n"));
    CHECK(PlatformFile::writeAllText(registryFilter / "Notes.md", "not an engine asset\n"));
    CHECK(PlatformFile::writeAllBytes(registryFilter / "Payload.bin", {0x01, 0x02, 0x03}));
    CHECK(PlatformFile::writeAllText(registryFilter / "Model.obj", "v 0 0 0\n"));
    AssetRegistry filteredRegistry;
    filteredRegistry.scan(registryFilter);
    CHECK(!filteredRegistry.Find((registryFilter / "Source.cpp").generic_string()) &&
          !filteredRegistry.Find((registryFilter / "Header.hpp").generic_string()) &&
          !filteredRegistry.Find((registryFilter / "Notes.md").generic_string()));
    CHECK(!std::filesystem::exists(registryFilter / "Source.cpp.meta") &&
          !std::filesystem::exists(registryFilter / "Header.hpp.meta") &&
          !std::filesystem::exists(registryFilter / "Notes.md.meta"));
    const auto binaryPayload = filteredRegistry.Find((registryFilter / "Payload.bin").generic_string());
    const auto modelSource = filteredRegistry.Find((registryFilter / "Model.obj").generic_string());
    CHECK(binaryPayload && binaryPayload->type == "Binary" && modelSource && modelSource->type == "Binary" &&
          std::filesystem::is_regular_file(registryFilter / "Payload.bin.meta") &&
          std::filesystem::is_regular_file(registryFilter / "Model.obj.meta"));
    CHECK(registry.markImported("assets/player.mesh"));
    CHECK(!registry.needsReimport("assets/player.mesh"));
    AssetDependencyGraph dependencyGraph;
    dependencyGraph.build(registry);
    const auto dependencyClosure = dependencyGraph.ResolveClosure("assets/default.mat");
    CHECK(!dependencyClosure.empty() && !dependencyGraph.hasCycles());
    ResourceManager streamingResources;
    streamingResources.initialize(nullptr, "assets");
    AssetStreamingManager streaming;
    streaming.initialize(&streamingResources, 1);
    {
        auto handle = streaming.Request<MeshResource>("assets/player.mesh");
        CHECK(handle.wait() && handle.isReady());
    }
    streaming.Update();
    CHECK(streamingResources.getCachedResourceCount() == 0);
    streaming.shutdown();
    streamingResources.shutdown();
    const auto cook = AssetCooker::cook("assets", "saved/CookedTest", "saved/CookedTest.tpak");
    CHECK(cook.success && cook.cookedFiles + cook.skippedFiles >= 4);
    const auto incrementalCook = AssetCooker::cook("assets", "saved/CookedTest", "saved/CookedTest.tpak");
    CHECK(incrementalCook.success && incrementalCook.skippedFiles >= 4 && incrementalCook.cookedFiles == 0);
    PlatformFile::clearMounts();

    CHECK(TargetPlatformRegistry::Get().Find("windows") != nullptr);
    CHECK(TargetPlatformCooker::ParseConfiguration("shipping") == BuildConfiguration::Shipping);
    const auto platformCook = TargetPlatformCooker::cook(
        {"Aster-Engine.tinyproject", "saved/PlatformCookTest", "Windows", BuildConfiguration::Shipping, false});
    CHECK(platformCook.success && std::filesystem::is_regular_file(platformCook.archive) &&
          platformCook.shaderVariants >= 2);
    VersionedDocument cookManifest;
    CHECK(VersionedTextArchive::deserialize(PlatformFile::readAllText(platformCook.manifest), cookManifest));
    CHECK(cookManifest.fields["platform"] == "Windows" && cookManifest.fields["configuration"] == "Shipping");
    PlatformFile::clearMounts();
    CHECK(PlatformFile::mountPak(platformCook.archive));
    MeshResource platformMesh;
    CHECK(platformMesh.load("assets/player.mesh") && platformMesh.GetVertices().size() == 3);
    PlatformFile::clearMounts();
    CommandletRegistry commandlets;
    registerBuiltInCommandlets(commandlets);
    CHECK(commandlets.Run("ValidateAssets", {"assets"}).exitCode == 0);
    CHECK(commandlets.Run("ValidateScenes", {"Saved"}).exitCode == 0);
    CHECK(PlatformFile::mountPak("saved/CookedTest.tpak"));
    CHECK(!PlatformFile::ReadAllBytes("player.mesh").empty());
    PlatformFile::clearMounts();

    World physicsScene;
    const Entity body = physicsScene.createEntity("Body");
    physicsScene.GetComponent<TransformComponent>(body)->position = {0.0f, 2.0f, 0.0f};
    physicsScene.AddComponent(body, RigidBodyComponent{});
    physicsScene.AddComponent(body, ColliderComponent{});
    const Entity ground = physicsScene.createEntity("Ground");
    physicsScene.GetComponent<TransformComponent>(ground)->position = {0.0f, -0.25f, 0.0f};
    physicsScene.GetComponent<TransformComponent>(ground)->scale = {20.0f, 0.5f, 20.0f};
    physicsScene.AddComponent(ground, RigidBodyComponent{BodyType::Static, 0.0f, {}, false});
    physicsScene.AddComponent(ground, ColliderComponent{});
    PhysicsWorld physics;
    for (int index = 0; index < 180; ++index)
        physics.step(physicsScene, 1.0f / 60.0f);
    CHECK(physicsScene.GetComponent<TransformComponent>(body)->position.y >= 0.49f);
    RaycastHit queryHit;
    CHECK(physics.sphereCast(physicsScene, {{0.0f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, 0.25f, 10.0f, queryHit));
    CHECK(!physics.Overlap(physicsScene, {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}).empty());
    CHECK(physics.GetDebugBoxes().size() == 2 && length(queryHit.normal) > 0.9f);
    ShapeSweepGeometry sweepGeometry;
    sweepGeometry.shape = ColliderShape::Box;
    sweepGeometry.halfExtents = {0.2f, 0.2f, 0.2f};
    RaycastHit sweepHit;
    CHECK(physics.shapeSweep(physicsScene, sweepGeometry, {0.0f, 4.0f, 0.0f}, {0.0f, -8.0f, 0.0f}, sweepHit));
    CHECK(PhysicsWorld::isBackendAvailable(PhysicsBackend::Builtin) && physics.getBackend() == PhysicsBackend::Builtin);
    const Entity rotator = physicsScene.createEntity("Rotator");
    RigidBodyComponent rotatingBody;
    rotatingBody.useGravity = false;
    rotatingBody.angularVelocity = {0.0f, 90.0f, 0.0f};
    rotatingBody.angularDamping = 0.0f;
    physicsScene.AddComponent(rotator, rotatingBody);
    physics.step(physicsScene, 0.5f);
    CHECK(std::abs(physicsScene.GetComponent<TransformComponent>(rotator)->rotation.y - 45.0f) < 0.01f);
    const Entity character = physicsScene.createEntity("Character");
    physicsScene.GetComponent<TransformComponent>(character)->position = {2.0f, 1.3f, 0.0f};
    physicsScene.AddComponent(character, ColliderComponent{ColliderShape::Capsule, {0.4f, 0.9f, 0.4f}, 0.4f, 1.8f});
    const auto characterMove = physics.moveCharacterController(physicsScene, character, {0.5f, 0.0f, 0.0f});
    CHECK(characterMove.appliedDisplacement.x > 0.49f && characterMove.grounded);
    World jointWorld;
    const Entity jointFirst = jointWorld.createEntity("JointFirst"),
                 jointSecond = jointWorld.createEntity("JointSecond");
    jointWorld.GetComponent<TransformComponent>(jointFirst)->position = {0.0f, 0.0f, 0.0f};
    jointWorld.GetComponent<TransformComponent>(jointSecond)->position = {4.0f, 0.0f, 0.0f};
    jointWorld.AddComponent(jointFirst, RigidBodyComponent{BodyType::Dynamic, 1.0f, {}, false});
    jointWorld.AddComponent(jointSecond, RigidBodyComponent{BodyType::Dynamic, 1.0f, {}, false});
    jointWorld.AddComponent(jointFirst, JointComponent{JointType::Distance, jointSecond, 2.0f, 1.0f});
    physics.step(jointWorld, 1.0f / 60.0f);
    CHECK(std::abs(length(jointWorld.GetComponent<TransformComponent>(jointFirst)->position -
                          jointWorld.GetComponent<TransformComponent>(jointSecond)->position) -
                   2.0f) < 0.01f);

    CHECK(PlatformFile::writeAllText("saved/Idle.anim",
                                     "duration 1\nkey 0 0 0 0 0 0 0 1 1 1\nkey 1 0 0 0 0 0 0 1 1 1\n"));
    CHECK(PlatformFile::writeAllText("saved/Run.anim",
                                     "duration 1\nkey 0 10 0 0 0 0 0 1 1 1\nkey 1 10 0 0 0 0 0 1 1 1\n"));
    World animationWorld;
    animationWorld.GetSystemManager().RegisterSystem<AnimationSystem>();
    const Entity animatedEntity = animationWorld.createEntity("Blend");
    animationWorld.AddComponent(animatedEntity,
                                AnimationComponent{"saved/Idle.anim", 0.0f, 1.0f, true, true, "saved/Run.anim", 1.0f});
    animationWorld.initialize();
    animationWorld.Update(0.5f);
    CHECK(std::abs(animationWorld.GetComponent<TransformComponent>(animatedEntity)->position.x - 5.0f) < 0.01f);
    animationWorld.shutdown();

    Skeleton skeleton{{{"Root", -1, Mat4::identity()}, {"Hand", 0, Mat4::identity()}}};
    CHECK(skeleton.isValid() && skeleton.findBone("Hand") == 1);
    auto skeletalClip = std::make_shared<SkeletalClip>();
    skeletalClip->name = "Wave";
    skeletalClip->duration = 1.0f;
    skeletalClip->keyframes = {{0.0f, {BonePose{}, BonePose{}}},
                               {1.0f, {BonePose{{1.0f, 0.0f, 0.0f}}, BonePose{{0.0f, 1.0f, 0.0f}}}}};
    const auto halfPose = skeletalClip->sample(0.5f, skeleton.bones.size());
    CHECK(halfPose.size() == 2 && std::abs(halfPose[0].position.x - 0.5f) < 0.001f);
    CHECK(BuildSkinMatrices(skeleton, halfPose).size() == 2);
    CHECK(saveSkeletonAsset(skeleton, "saved/Test.skeleton") &&
          saveSkeletalClipAsset(*skeletalClip, "saved/Test.skanim"));
    Skeleton loadedSkeleton;
    SkeletalClip loadedSkeletalClip;
    CHECK(loadSkeletonAsset("saved/Test.skeleton", loadedSkeleton) &&
          loadSkeletalClipAsset("saved/Test.skanim", loadedSkeletalClip));
    SkeletalClip compressedClip;
    compressedClip.name = "Compressed";
    compressedClip.duration = 1.0f;
    compressedClip.events = {{0.25f, "Footstep", "left"}, {0.75f, "Footstep", "right"}};
    compressedClip.keyframes = {{0.0f, {BonePose{}, BonePose{}}},
                                {0.5f, {BonePose{{0.5f, 0.0f, 0.0f}}, BonePose{{0.0f, 0.5f, 0.0f}}}},
                                {1.0f, {BonePose{{1.0f, 0.0f, 0.0f}}, BonePose{{0.0f, 1.0f, 0.0f}}}}};
    const auto compression = compressedClip.compress();
    CHECK(compressedClip.isCompressed() && compression.compressedCurveKeys < compression.originalCurveKeys);
    CHECK(std::abs(compressedClip.sample(0.5f, 2).front().position.x - 0.5f) < 0.001f);
    CHECK(compressedClip.GatherEvents(0.1f, 0.8f, true).size() == 2);
    CHECK(compressedClip.GatherEvents(0.8f, 1.3f, true).size() == 1);
    CHECK(compressedClip.GatherEvents(0.8f, 0.2f, false).size() == 2);
    CHECK(saveSkeletalClipAsset(compressedClip, "saved/Compressed.skanim"));
    SkeletalClip reloadedCompressed;
    CHECK(loadSkeletalClipAsset("saved/Compressed.skanim", reloadedCompressed) && reloadedCompressed.isCompressed() &&
          reloadedCompressed.events.size() == 2);

    auto idleClip = std::make_shared<SkeletalClip>(*skeletalClip);
    auto runClip = std::make_shared<SkeletalClip>(*skeletalClip);
    runClip->name = "Run";
    runClip->keyframes[0].localPose[0].position.x = 10.0f;
    runClip->keyframes[1].localPose[0].position.x = 11.0f;
    CHECK(saveSkeletalClipAsset(*idleClip, "saved/BlendIdle.skanim") &&
          saveSkeletalClipAsset(*runClip, "saved/BlendRun.skanim"));
    BlendTree1D blendTree;
    blendTree.parameter = "Speed";
    blendTree.samples = {{0.0f, "saved/BlendIdle.skanim"}, {10.0f, "saved/BlendRun.skanim"}};
    CHECK(saveBlendTreeAsset(blendTree, "saved/Locomotion.blendtree"));
    BlendTree1D loadedBlendTree;
    CHECK(loadBlendTreeAsset("saved/Locomotion.blendtree", loadedBlendTree));
    const std::unordered_map<std::string, std::shared_ptr<SkeletalClip>> blendClips = {
        {"saved/BlendIdle.skanim", idleClip}, {"saved/BlendRun.skanim", runClip}};
    const auto blendedPose =
        loadedBlendTree.sample(5.0f, 0.5f, 2,
                               [&blendClips](const std::string& path)
                               {
                                   const auto found = blendClips.find(path);
                                   return found == blendClips.end() ? nullptr : found->second.get();
                               });
    CHECK(std::abs(blendedPose.front().position.x - 5.5f) < 0.001f &&
          loadedBlendTree.dominantClip(9.0f) == "saved/BlendRun.skanim");
    BlendTree2D blend2d;
    blend2d.samples = {{{0.0f, 0.0f}, "saved/BlendIdle.skanim"}, {{1.0f, 0.0f}, "saved/BlendRun.skanim"}};
    const auto blend2dPose = blend2d.sample({1.0f, 0.0f}, 0.5f, 2,
                                            [&blendClips](const std::string& path)
                                            {
                                                const auto found = blendClips.find(path);
                                                return found == blendClips.end() ? nullptr : found->second.get();
                                            });
    CHECK(std::abs(blend2dPose.front().position.x - 10.5f) < 0.001f);
    SkeletonRetargeter retargeter;
    CHECK(retargeter.buildNameMap(skeleton, skeleton));
    CHECK(retargeter.Retarget(halfPose, 2).size() == 2);
    const auto layeredPose = BlendAnimationLayers(halfPose, {{runClip->sample(0.5f, 2), {1.0f, 0.0f}, 0.5f, false}});
    CHECK(std::abs(layeredPose.front().position.x - 5.5f) < 0.001f);
    CHECK(buildAnimationDebugSnapshot(skeleton, halfPose).bones.size() == 1);
    compressedClip.rootMotion = {{0.0f, {}}, {1.0f, {2.0f, 0.0f, 0.0f}}};
    CHECK(std::abs(compressedClip.extractRootMotion(0.75f, 1.25f).x - 1.0f) < 0.001f);
    CHECK(saveSkeletalClipAsset(compressedClip, "saved/RootMotion.skanim"));
    SkeletalClip loadedRootMotion;
    CHECK(loadSkeletalClipAsset("saved/RootMotion.skanim", loadedRootMotion) &&
          loadedRootMotion.rootMotion.size() == 2);

    AnimationGraph graph;
    graph.entryState = "Idle";
    graph.parameters = {{"Speed", 0.0f}};
    graph.states = {{"Idle", "saved/BlendIdle.skanim"}, {"Run", "saved/BlendRun.skanim"}};
    graph.transitions = {{"Idle", "Run", "Speed", AnimationGraphComparison::Greater, 0.5f, 0.2f}};
    CHECK(saveAnimationGraphAsset(graph, "saved/Locomotion.animgraph"));
    AnimationGraph loadedGraph;
    CHECK(loadAnimationGraphAsset("saved/Locomotion.animgraph", loadedGraph));
    AnimationGraphPlayer graphPlayer;
    CHECK(graphPlayer.setGraph(std::make_shared<AnimationGraph>(loadedGraph)));
    graphPlayer.setParameter("Speed", 1.0f);
    CHECK(graphPlayer
                  .Update(0.1f, 2,
                          [&blendClips](const std::string& path, const float time, const std::size_t bones)
                          {
                              const auto found = blendClips.find(path);
                              return found == blendClips.end() ? std::vector<BonePose>(bones)
                                                               : found->second->sample(time, bones);
                          })
                  .size() == 2 &&
          graphPlayer.getState() == "Run");
    World skeletalWorld;
    auto* skeletalSystem = skeletalWorld.GetSystemManager().RegisterSystem<AnimationSystem>();
    int dispatchedAnimationEvents = 0;
    skeletalSystem->setEventCallback(
        [&dispatchedAnimationEvents](Entity, const AnimationEvent& event)
        {
            if (event.name == "Footstep")
                ++dispatchedAnimationEvents;
        });
    const Entity skeletalEntity = skeletalWorld.createEntity("Skeletal");
    AnimationComponent skeletalAnimation;
    skeletalAnimation.clipPath = "saved/Compressed.skanim";
    skeletalAnimation.skeletonPath = "saved/Test.skeleton";
    skeletalWorld.AddComponent(skeletalEntity, skeletalAnimation);
    const Entity graphEntity = skeletalWorld.createEntity("GraphSkeletal");
    AnimationComponent graphAnimation;
    graphAnimation.skeletonPath = "saved/Test.skeleton";
    graphAnimation.graphPath = "saved/Locomotion.animgraph";
    graphAnimation.graphParameter = "Speed";
    graphAnimation.graphParameterValue = 1.0f;
    skeletalWorld.AddComponent(graphEntity, graphAnimation);
    skeletalWorld.initialize();
    skeletalWorld.Update(0.5f);
    CHECK(skeletalWorld.GetComponent<AnimationComponent>(skeletalEntity)->skinMatrices.size() == 2 &&
          dispatchedAnimationEvents == 1 &&
          skeletalWorld.GetComponent<AnimationComponent>(skeletalEntity)->firedEvents.size() == 1);
    CHECK(skeletalWorld.GetComponent<AnimationComponent>(graphEntity)->graphState == "Run" &&
          skeletalWorld.GetComponent<AnimationComponent>(graphEntity)->skinMatrices.size() == 2);
    skeletalWorld.shutdown();
    AnimationStateMachine stateMachine;
    CHECK(stateMachine.addState("Wave", skeletalClip));
    CHECK(stateMachine.Update(0.25f, 2).size() == 2);

    Config config;
    config.set("render.vsync", "true");
    config.set("physics.fixedRate", "60");
    CHECK(config.Save("saved/Test.ini"));
    Config loadedConfig;
    CHECK(loadedConfig.load("saved/Test.ini"));
    CHECK(loadedConfig.getBool("render.vsync"));
    CHECK(loadedConfig.getInt("physics.fixedRate") == 60);
    CrashReporter::install("TinyEngineTest", "saved/Crashes");
    CHECK(!CrashReporter::writeReport("automated test", "saved/Crashes").empty());
    AudioEngine audioMixer;
    audioMixer.setBusVolume("Music", 0.35f);
    CHECK(std::abs(audioMixer.getBusVolume("Music") - 0.35f) < 0.001f);

    InputSystem mappedInput;
    mappedInput.clearBindings();
    mappedInput.bindAxis("Horizontal", "MoveX", 2.0f);
    mappedInput.bindAction("Fire", "Primary");
    InputState mappedState;
    mappedState.moveX = 0.25f;
    mappedState.primaryAction = true;
    mappedInput.Update(mappedState);
    CHECK(std::abs(mappedInput.getAxis("Horizontal") - 0.5f) < 0.001f && mappedInput.wasPressed("Fire"));
    CHECK(mappedInput.saveBindings("saved/Test.input"));
    InputSystem loadedInput;
    CHECK(loadedInput.loadBindings("saved/Test.input"));
    loadedInput.Update(mappedState);
    CHECK(std::abs(loadedInput.getAxis("Horizontal") - 0.5f) < 0.001f && loadedInput.isDown("Fire"));
    bool hapticCalled = false;
    loadedInput.setHapticCallback(
        [&hapticCalled](float low, float high, std::uint32_t milliseconds)
        {
            hapticCalled = low == 1.0f && high == 0.0f && milliseconds == 50;
            return true;
        });
    CHECK(loadedInput.rumble(2.0f, -1.0f, 50) && hapticCalled);
    CHECK(InputSystem::getGlyph("Jump", InputGlyphSet::PlayStation) == "Cross");

    const std::string layoutUnicode =
        TextLayout::encodeUtf8({'T', 'i', 'n', 'y', 0x5f15, 0x64ce, ' ', 0x0645, 0x0631, 0x062d, 0x0628, 0x0627});
    const auto decodedText = TextLayout::DecodeUtf8(layoutUnicode);
    CHECK(decodedText.size() == 12 && TextLayout::encodeUtf8(decodedText) == layoutUnicode);
    const auto shapedText = TextLayout::shape("[b]office[/b] " + layoutUnicode, 80.0f, {{1, 1, 1, 1}, 18.0f});
    CHECK(!shapedText.glyphs.empty() && shapedText.lineCount > 1 &&
          std::any_of(shapedText.glyphs.begin(), shapedText.glyphs.end(),
                      [](const ShapedGlyph& glyph) { return glyph.codepoint == 0xfb01; }));

    MaterialGraph materialGraph;
    const auto constantNode = materialGraph.addNode(MaterialNodeType::Constant, "Albedo");
    const auto outputNode = materialGraph.addNode(MaterialNodeType::PbrOutput, "Output");
    CHECK(materialGraph.connect(constantNode, outputNode, 0) && materialGraph.Save("saved/Test.matgraph"));
    MaterialGraph loadedMaterialGraph;
    CHECK(loadedMaterialGraph.load("saved/Test.matgraph") && loadedMaterialGraph.GetNodes().size() >= 2 &&
          loadedMaterialGraph.compile().find("EvaluateSurface") != std::string::npos);

    std::vector<std::uint8_t> chunkSource(230000);
    for (std::size_t index = 0; index < chunkSource.size(); ++index)
        chunkSource[index] = static_cast<std::uint8_t>(index % 251);
    CHECK(PlatformFile::writeAllBytes("saved/ChunkSource.bin", chunkSource));
    CHECK(AssetMaintenance::chunkAsset("saved/ChunkSource.bin", "saved/ChunkSource.chunkmanifest", 65536));
    ChunkedAssetStream chunkStream;
    CHECK(chunkStream.Open("saved/ChunkSource.chunkmanifest") && chunkStream.getSize() == chunkSource.size());
    const auto chunkRange = chunkStream.read(65000, 5000);
    CHECK(chunkRange.size() == 5000 && chunkRange.front() == chunkSource[65000] &&
          chunkRange.back() == chunkSource[69999]);

    std::vector<std::uint8_t> dds(136, 0);
    const auto writeU32 = [&dds](const std::size_t offset, const std::uint32_t value)
    {
        for (int byte = 0; byte < 4; ++byte)
            dds[offset + byte] = static_cast<std::uint8_t>((value >> (byte * 8)) & 255u);
    };
    writeU32(0, 0x20534444u);
    writeU32(4, 124u);
    writeU32(12, 4u);
    writeU32(16, 4u);
    writeU32(28, 1u);
    writeU32(76, 32u);
    writeU32(80, 4u);
    writeU32(84, 0x31545844u);
    CHECK(PlatformFile::writeAllBytes("saved/Test.dds", dds));
    TextureResource ddsTexture;
    CHECK(ddsTexture.load("saved/Test.dds") && ddsTexture.getCompression() == TextureResource::Compression::BC1 &&
          ddsTexture.GetMipLevels().size() == 1 && ddsTexture.GetData().size() == 8);

    UISystem ui;
    bool clicked = false;
    ui.AddPanel("hud", {0, 0, 300, 100});
    ui.AddButton("start", "Start", {10, 10, 120, 40}, [&clicked]() { clicked = true; });
    ui.Update(20, 20, true);
    ui.Update(20, 20, false);
    CHECK(clicked && ui.GetFocusedId() == "start" && ui.BuildDrawList().size() == 2);
    UISystem layoutUi;
    int activated = 0;
    layoutUi.setViewportSize(800.0f, 600.0f);
    layoutUi.AddPanel("menu", {0, 0, 300, 160});
    layoutUi.setAnchor("menu", UIAnchor::Center);
    layoutUi.setLayout("menu", UILayout::Vertical, 8.0f, 10.0f);
    layoutUi.AddLabel("title", "TinyEngine", {0, 0, 100, 28});
    layoutUi.AddButton("play", "Play", {0, 0, 120, 36}, [&activated]() { ++activated; });
    CHECK(layoutUi.setParent("title", "menu") && layoutUi.setParent("play", "menu"));
    const auto& layoutCommands = layoutUi.BuildDrawList();
    CHECK(layoutCommands.size() == 3 && std::abs(layoutCommands[0].rect.x - 250.0f) < 0.001f);
    CHECK(layoutCommands[1].rect.x == 260.0f &&
          layoutCommands[2].rect.y == layoutCommands[1].rect.y + layoutCommands[1].rect.height + 8.0f);
    layoutUi.focusNext();
    layoutUi.activateFocused();
    CHECK(activated == 1 && layoutUi.GetFocusedId() == "play");
    CHECK(layoutUi.setScrollable("menu", true) && layoutUi.setScrollOffset("menu", {0.0f, 12.0f}));
    layoutUi.setDpiScale(2.0f);
    const auto& scaledCommands = layoutUi.BuildDrawList();
    CHECK(scaledCommands[0].rect.width == 600.0f && scaledCommands[1].fontSize == 36.0f &&
          scaledCommands[1].clipRect.width > 0.0f);
    UISystem controls;
    controls.AddInputField("name", {}, {0, 0, 160, 30}, "Name");
    controls.Update(4, 4, true);
    controls.Update(4, 4, false);
    const std::string playerName = TextLayout::encodeUtf8({0x73a9, 0x5bb6});
    CHECK(controls.inputText(playerName) && controls.backspace() &&
          controls.Find("name")->text == TextLayout::encodeUtf8({0x73a9}));
    controls.AddList("list", {"A", "B", "C"}, {0, 40, 160, 90});
    CHECK(controls.selectItem("list", 1) && controls.Find("list")->selectedIndex == 1);
    controls.AddDropdown("quality", {"Low", "High"}, {0, 140, 160, 30});
    CHECK(controls.selectItem("quality", 1));
    controls.AddProgressBar("loading", 0.4f, {0, 180, 160, 20});
    CHECK(controls.setValue("loading", 0.75f) && controls.BuildDrawList().size() >= 5);

    std::vector<std::uint32_t> codepoints;
    const std::string unicodeText = "Tiny引擎";
    CHECK(Utf8::decode(unicodeText, codepoints) && codepoints.size() == 6 && Utf8::encode(codepoints) == unicodeText);
#ifdef _WIN32
    RasterizedGlyph chineseGlyph;
    CHECK(FontRasterizer::rasterizeSystemGlyph(0x5f15, 24, chineseGlyph) && chineseGlyph.advance > 0 &&
          !chineseGlyph.pixels.empty());
#endif
    StringTable strings;
    strings.set("en", "welcome", "Hello {0}");
    strings.set("zh-CN", "welcome", "你好，{0}");
    CHECK(strings.Save("saved/Test.loc"));
    StringTable loadedStrings;
    CHECK(loadedStrings.load("saved/Test.loc") && loadedStrings.format("zh-CN", "welcome", {"Tiny"}) == "你好，Tiny");
    ParticleEmitterSettings particleSettings;
    particleSettings.maximumParticles = 8;
    particleSettings.emissionRate = 0.0f;
    particleSettings.lifetime = 1.0f;
    particleSettings.flipbookColumns = 4;
    particleSettings.flipbookRows = 2;
    particleSettings.flipbookFramesPerSecond = 8.0f;
    ParticleEmitter particles(particleSettings);
    particles.burst(4);
    particles.Update(0.5f);
    CHECK(particles.GetParticles().size() == 4 && particles.GetParticles().front().age > 0.49f &&
          particles.GetParticles().front().flipbookFrame == 4);
    CHECK(saveParticleEffectAsset(particleSettings, "saved/Test.vfx"));
    ParticleEmitterSettings loadedEffect;
    CHECK(loadParticleEffectAsset("saved/Test.vfx", loadedEffect) && loadedEffect.flipbookColumns == 4);
    VfxSystem gpuVfx;
    loadedEffect.simulationMode = ParticleSimulationMode::Gpu;
    gpuVfx.createEmitter(loadedEffect);
    gpuVfx.Update(0.25f);
    CHECK(gpuVfx.BuildParticleList().empty() && gpuVfx.BuildGpuEmitterList().size() == 1 &&
          gpuVfx.BuildGpuEmitterList().front().elapsedTime > 0.24f);
    particles.Update(0.6f);
    CHECK(particles.GetParticles().empty());

    NavigationGrid navigation;
    CHECK(navigation.configure({}, 5, 5, 1.0f));
    for (int z = 0; z < 4; ++z)
        CHECK(navigation.setWalkable(2, z, false));
    const auto path = navigation.FindPath({0.5f, 0.0f, 0.5f}, {4.5f, 0.0f, 0.5f}, false);
    CHECK(!path.empty() && std::any_of(path.begin(), path.end(), [](const Vec3& point) { return point.z > 4.0f; }));
    PolygonNavMesh polygonNavigation;
    CHECK(polygonNavigation.build({{0, 0, 0}, {2, 0, 0}, {2, 0, 2}, {0, 0, 2}}, {0, 2, 1, 0, 3, 2}));
    CHECK(!polygonNavigation.FindPath({0.2f, 0.0f, 0.2f}, {1.8f, 0.0f, 1.8f}).empty());
    const auto obstacle = polygonNavigation.addDynamicObstacle({{1.2f, -1.0f, 0.2f}, {2.1f, 1.0f, 1.2f}});
    CHECK(obstacle != 0 && polygonNavigation.isPolygonBlocked(0));
    CHECK(polygonNavigation.removeDynamicObstacle(obstacle));
    NavCrowd crowd(&polygonNavigation);
    const auto agent = crowd.addAgent({0.2f, 0.0f, 0.2f});
    CHECK(crowd.setTarget(agent, {1.8f, 0.0f, 1.8f}));
    crowd.Update(0.1f);
    CHECK(length(crowd.GetAgents().front().velocity) > 0.0f);
    EnvironmentQuery query;
    query.generateGrid({}, 2.0f, 1.0f);
    query.addTest([](const Vec3& point) { return point.x; });
    CHECK(query.execute(2).size() == 2);

    BehaviorTree behavior;
    behavior.GetBlackboard().set("ready", true);
    int actions = 0;
    auto sequence = std::make_unique<SequenceNode>();
    sequence->Add(std::make_unique<ConditionNode>(
        [](const Blackboard& blackboard)
        {
            const bool* ready = blackboard.Get<bool>("ready");
            return ready && *ready;
        }));
    sequence->Add(std::make_unique<WaitNode>(0.1f));
    sequence->Add(std::make_unique<ActionNode>(
        [&actions](Blackboard&, float)
        {
            ++actions;
            return BehaviorStatus::Success;
        }));
    behavior.setRoot(std::move(sequence));
    CHECK(behavior.Tick(0.05f) == BehaviorStatus::Running);
    CHECK(behavior.Tick(0.05f) == BehaviorStatus::Success && actions == 1);
    BehaviorTreeGraph behaviorGraph;
    behaviorGraph.root = 1;
    behaviorGraph.nodes = {{1, "Sequence", "Root", {}, {2}}, {2, "Action", "Move", {100.0f, 0.0f}, {}}};
    CHECK(saveBehaviorTreeGraph(behaviorGraph, "saved/Test.btgraph"));
    BehaviorTreeGraph loadedBehaviorGraph;
    CHECK(loadBehaviorTreeGraph("saved/Test.btgraph", loadedBehaviorGraph) && loadedBehaviorGraph.nodes.size() == 2);

    WorldPartition partition;
    CHECK(partition.registerCell({{0, 0}, {{0, -1, 0}, {10, 1, 10}}, "near.scene"}));
    CHECK(partition.registerCell({{1, 0}, {{100, -1, 0}, {110, 1, 10}}, "far.scene"}));
    partition.setStreamingDistances(5.0f, 10.0f);
    int loadedCells = 0, unloadedCells = 0;
    const auto loadCell = [&loadedCells](const WorldPartitionCell&)
    {
        ++loadedCells;
        return true;
    };
    const auto unloadCell = [&unloadedCells](const WorldPartitionCell&) { ++unloadedCells; };
    partition.Update({2, 0, 2}, loadCell, unloadCell);
    CHECK(partition.GetLoadedCells().size() == 1 && loadedCells == 1);
    partition.Update({105, 0, 2}, loadCell, unloadCell);
    CHECK(partition.GetLoadedCells().size() == 1 && loadedCells == 2 && unloadedCells == 1);

    std::vector<std::string> moduleEvents;
    ModuleManager modules;
    CHECK(modules.Register(std::make_unique<TestModule>("Gameplay", std::vector<std::string>{"Core"}, moduleEvents)));
    CHECK(modules.Register(std::make_unique<TestModule>("Core", std::vector<std::string>{}, moduleEvents)));
    CHECK(modules.startupAll());
    modules.shutdownAll();
    CHECK(moduleEvents == std::vector<std::string>({"start:Core", "start:Gameplay", "stop:Gameplay", "stop:Core"}));
    const TinyPluginAPI testPlugin{TINY_PLUGIN_ABI_VERSION, sizeof(TinyPluginAPI), "TestPlugin",   1,
                                   &StartTestPlugin,        &StopTestPlugin,       &TickTestPlugin};
    PluginManager plugins;
    CHECK(plugins.registerStatic(&testPlugin) && pluginStarted);
    plugins.Tick(0.1);
    CHECK(pluginTicks == 1 && plugins.GetLoadedNames() == std::vector<std::string>{"TestPlugin"});
    plugins.unloadAll();
    CHECK(!pluginStarted);

    NetworkSnapshot firstSnapshot{1, 1.0, {{makeEntity(7, 2), {0, 0, 0}, {}, {1, 0, 0}}}};
    NetworkSnapshot secondSnapshot{2, 2.0, {{makeEntity(7, 2), {10, 0, 0}, {}, {1, 0, 0}}}};
    NetworkSnapshot decodedSnapshot;
    CHECK(SnapshotCodec::decode(SnapshotCodec::encode(firstSnapshot), decodedSnapshot));
    CHECK(decodedSnapshot.entities.size() == 1 && decodedSnapshot.entities[0].entity == makeEntity(7, 2));
    ReplayStream replay;
    replay.Record(firstSnapshot);
    replay.Record(secondSnapshot);
    std::vector<ReplicatedEntityState> sampledStates;
    CHECK(replay.sample(1.5, sampledStates) && sampledStates.size() == 1 &&
          std::abs(sampledStates[0].position.x - 5.0f) < 0.001f);
    CHECK(replay.Save("saved/Test.replay"));
    ReplayStream loadedReplay;
    CHECK(loadedReplay.load("saved/Test.replay") && std::abs(loadedReplay.getDuration() - 1.0) < 0.001);
    ReliableChannel reliableA, reliableB;
    CHECK(reliableA.queue({1, 2, 3}));
    const auto packetsA = reliableA.BuildPackets(0.0);
    std::vector<std::uint8_t> reliablePayload;
    CHECK(packetsA.size() == 1 && reliableB.processPacket(packetsA.front(), 0.0, reliablePayload) &&
          reliablePayload == std::vector<std::uint8_t>({1, 2, 3}));
    CHECK(reliableB.queue({9}));
    const auto packetsB = reliableB.BuildPackets(0.01);
    CHECK(reliableA.processPacket(packetsB.front(), 0.01, reliablePayload) && reliableA.pendingCount() == 0);
    int rpcCalls = 0;
    RpcRegistry rpc;
    CHECK(rpc.Register(7,
                       [&rpcCalls](std::uint32_t client, const std::vector<std::uint8_t>& payload)
                       {
                           if (client == 42 && payload == std::vector<std::uint8_t>{4, 5})
                               ++rpcCalls;
                       }));
    CHECK(rpc.dispatch(42, rpc.encode(7, {4, 5}), 1.0) && rpcCalls == 1);
    NetworkOwnership ownership;
    CHECK(ownership.assign(makeEntity(9, 1), 42) && ownership.isOwner(makeEntity(9, 1), 42) &&
          !ownership.release(makeEntity(9, 1), 7));
    ClientPrediction prediction;
    prediction.applyInput({1, {2.0f, 0.0f, 0.0f}, 0.1f});
    prediction.applyInput({2, {2.0f, 0.0f, 0.0f}, 0.1f});
    CHECK(prediction.reconcile(1, {0.2f, 0.0f, 0.0f}).x > 0.2f && prediction.pendingInputCount() == 1);
    UdpSocket udp;
    CHECK(udp.Open(0) && udp.getBoundPort() != 0 && !udp.send({"not-an-ip", 1}, {1}));

    TransformTrack transformTrack;
    transformTrack.addKey({0.0f, {{0, 0, 0}, {}, {1, 1, 1}}});
    transformTrack.addKey({1.0f, {{4, 0, 0}, {}, {1, 1, 1}}});
    CHECK(std::abs(transformTrack.sample(0.25f).position.x - 1.0f) < 0.001f);
    SequencePlayer sequencePlayer;
    std::vector<std::string> sequenceEvents;
    sequencePlayer.setDuration(1.0f);
    sequencePlayer.setEvents({{0.5f, "Half"}});
    sequencePlayer.setEventCallback([&sequenceEvents](const std::string& name) { sequenceEvents.push_back(name); });
    sequencePlayer.play();
    sequencePlayer.Update(0.75f);
    CHECK(sequenceEvents == std::vector<std::string>{"Half"} && sequencePlayer.isPlaying());

    HeightField terrain;
    CHECK(terrain.configure(2, 2, 1.0f, {0.0f, 2.0f, 2.0f, 4.0f}));
    CHECK(std::abs(terrain.sampleHeight(0.5f, 0.5f) - 2.0f) < 0.001f);
    const auto terrainMesh = terrain.buildMesh();
    CHECK(terrainMesh.vertices.size() == 4 && terrainMesh.indices.size() == 6);
    ImageSequencePlayer imageSequence;
    CHECK(imageSequence.setFrames({"frame0.png", "frame1.png", "frame2.png"}, 2.0f));
    imageSequence.play();
    imageSequence.Update(0.6f);
    CHECK(imageSequence.getFrameIndex() == 1 && imageSequence.GetCurrentFrame() == "frame1.png");

    if (failures == 0)
        std::cout << "All TinyEngine tests passed\n";
    return failures == 0 ? 0 : 1;
}
