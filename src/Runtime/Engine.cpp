// 文件：src/Runtime/Engine.cpp
// 作用：组装全部运行时子系统并驱动初始化、主循环、游戏态切换和关闭。
// 约定：子系统初始化与关闭顺序严格相反，避免悬空服务指针。
// 类型：实现文件。

#include "MiniEngine/Runtime/Engine.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/ECS/Systems.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace MiniEngine
{
// 初始化按“基础服务 -> 资源与世界 -> 表现系统 -> 项目内容”的依赖方向进行；任一步失败都由 Shutdown 逆序收尾。
bool Engine::Initialize(const std::filesystem::path &projectFile, const bool startInPlayMode)
{
    if (m_initialized)
        return true;
    m_diagnostics.Initialize();
    m_logger.Info("Engine initialize begin.");
    const bool projectLoaded = !projectFile.empty() && m_projectSettings.Load(projectFile);
    m_logger.SetStructuredOutput(projectLoaded ? m_projectSettings.Resolve("Logs/TinyEngine.jsonl")
                                               : std::filesystem::path{"Logs/TinyEngine.jsonl"});
    if (projectLoaded && !m_projectSettings.packageArchive.empty())
    {
        PlatformFile::ClearMounts();
        m_projectPakMounted = PlatformFile::MountPak(m_projectSettings.Resolve(m_projectSettings.packageArchive), 100);
        if (!m_projectPakMounted)
        {
            m_logger.Error("Cannot mount project package: " + m_projectSettings.packageArchive.generic_string());
            return false;
        }
        PlatformFile::SetReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
    }
    m_memoryManager.Initialize();
    const auto hardwareCount = std::thread::hardware_concurrency();
    m_jobSystem.Initialize(std::max(1u, hardwareCount > 1 ? hardwareCount - 1 : 1u));
    m_resourceManager.Initialize(&m_jobSystem, projectLoaded ? m_projectSettings.Resolve(m_projectSettings.assetDirectory)
                                                             : std::filesystem::path{"assets"});
    m_assetStreaming.Initialize(&m_resourceManager);
    if (projectLoaded && !m_projectSettings.inputBindings.empty())
    {
        const auto inputPath =
            m_projectPakMounted ? m_projectSettings.inputBindings : m_projectSettings.Resolve(m_projectSettings.inputBindings);
        if (!m_inputSystem.LoadBindings(inputPath.generic_string()))
            m_inputSystem.ResetDefaultBindings();
    }
    else
        m_inputSystem.ResetDefaultBindings();
    m_world.GetSystemManager().RegisterSystem<MovementSystem>();
    m_world.GetSystemManager().RegisterSystem<AnimationSystem>();
    m_world.AddListener(this);
    m_world.Initialize();
    m_gameplay.Initialize(&m_world, &m_inputSystem, &m_physicsWorld);
    m_renderer.SetLogger(&m_logger);
    m_renderer.SetResourceManager(&m_resourceManager);
    m_renderer.SetUISystem(&m_uiSystem);
    m_renderer.SetVfxSystem(&m_vfxSystem);
    if (projectLoaded && !m_projectSettings.environmentTexture.empty())
        m_renderer.SetEnvironmentPath(m_projectPakMounted
                                          ? m_projectSettings.environmentTexture.generic_string()
                                          : m_projectSettings.Resolve(m_projectSettings.environmentTexture).generic_string());
    if (!m_renderer.Initialize(
            std::make_unique<SdlGlRenderDevice>(1280, 720, projectLoaded ? m_projectSettings.name : "TinyEngine SDL2 + OpenGL")))
    {
        m_logger.Error("Renderer initialize failed.");
        Shutdown();
        return false;
    }
    if (auto *device = dynamic_cast<SdlGlRenderDevice *>(m_renderer.GetDevice()))
        m_inputSystem.SetHapticCallback([device](const float low, const float high, const std::uint32_t milliseconds) {
            return device->RumbleController(low, high, milliseconds);
        });
    m_audioEngine.Initialize(&m_logger);
    m_scriptEngine.Initialize(&m_logger);
    auto &console = m_diagnostics.GetConsole();
    console.RegisterVariable("t.TimeScale", 1.0f, "Global gameplay time scale", false,
                             [this](const ConsoleValue &value) { SetTimeScale(std::get<float>(value)); });
    console.RegisterVariable("r.FXAA", true, "Enable post-process FXAA", false, [this](const ConsoleValue &value) {
        if (auto *device = dynamic_cast<SdlGlRenderDevice *>(m_renderer.GetDevice()))
            device->SetFxaaEnabled(std::get<bool>(value));
    });
    console.RegisterVariable("r.Bloom", true, "Enable multi-level bloom", false, [this](const ConsoleValue &value) {
        if (auto *device = dynamic_cast<SdlGlRenderDevice *>(m_renderer.GetDevice()))
            device->SetBloomEnabled(std::get<bool>(value));
    });
    console.RegisterVariable("r.GTAO", true, "Enable ground-truth ambient occlusion approximation", false,
                             [this](const ConsoleValue &value) {
                                 if (auto *device = dynamic_cast<SdlGlRenderDevice *>(m_renderer.GetDevice()))
                                     device->SetGtaoEnabled(std::get<bool>(value));
                             });
    console.RegisterVariable("r.TAA", true, "Enable motion-vector temporal anti-aliasing", false, [this](const ConsoleValue &value) {
        if (auto *device = dynamic_cast<SdlGlRenderDevice *>(m_renderer.GetDevice()))
            device->SetTaaEnabled(std::get<bool>(value));
    });
    console.RegisterVariable(
        "asset.StreamingBudgetMB", 256, "Runtime resource memory budget in MiB", false, [this](const ConsoleValue &value) {
            m_assetStreaming.SetMemoryBudget(static_cast<std::size_t>(std::max(1, std::get<int>(value))) * 1024u * 1024u);
        });
    console.RegisterVariable(
        "r.GpuBudgetMB", 512, "OpenGL mesh and texture memory budget in MiB", false, [this](const ConsoleValue &value) {
            if (auto *device = m_renderer.GetDevice())
                device->SetGpuMemoryBudget(static_cast<std::size_t>(std::max(1, std::get<int>(value))) * 1024u * 1024u);
        });
    console.RegisterCommand("asset.gc", [this](const std::vector<std::string> &) {
        m_resourceManager.GarbageCollect();
        return std::string{"Resource garbage collection complete"};
    });
    console.RegisterCommand("r.TrimGpu", [this](const std::vector<std::string> &) {
        if (auto *device = m_renderer.GetDevice())
            device->TrimGpuResources();
        return std::string{"GPU resource caches cleared"};
    });
    m_context = {&m_logger,         &m_memoryManager,  &m_jobSystem,    &m_resourceManager, &m_world,        &m_renderer,
                 &m_inputSystem,    &m_physicsWorld,   &m_audioEngine,  &m_scriptEngine,    &m_sceneManager, &m_projectSettings,
                 &m_uiSystem,       &m_assetStreaming, &m_gameplay,     &m_diagnostics,     &m_vfxSystem,    &m_navigation,
                 &m_worldPartition, &m_moduleManager,  &m_pluginManager};
    m_pluginManager.SetLogCallback([this](const TinyPluginLogLevel level, const std::string &message) {
        if (level == TINY_PLUGIN_LOG_ERROR)
            m_logger.Error(message);
        else if (level == TINY_PLUGIN_LOG_WARNING)
            m_logger.Warning(message);
        else
            m_logger.Info(message);
    });
    if (!m_moduleManager.StartupAll())
    {
        m_logger.Error("Module startup failed because a dependency is missing, cyclic, or failed to initialize.");
        Shutdown();
        return false;
    }
    bool sceneLoaded = false;
    if (projectLoaded)
        sceneLoaded = m_sceneManager.LoadScene(m_projectPakMounted ? m_projectSettings.startupScene
                                                                   : m_projectSettings.Resolve(m_projectSettings.startupScene));
    if (!sceneLoaded)
        SetupDemoScene();
    if (m_gameInstance)
        m_gameInstance->OnInitialize();
    m_initialized = true;
    if (startInPlayMode)
        EnterPlayMode();
    else
        m_paused = true;
    const auto *glDevice = dynamic_cast<const SdlGlRenderDevice *>(m_renderer.GetDevice());
    m_logger.Info(std::string{"Engine initialize complete. OpenGL 4.3 core renderer active; GPU Driven: "} +
                  (glDevice && glDevice->IsGpuDrivenCullingSupported() ? "Compute + Indirect." : "fallback."));
    return true;
}

// maxFrames 为测试提供确定的退出条件；传 0 时由窗口关闭或外部状态结束循环。
void Engine::Run(const std::uint32_t maxFrames)
{
    if (!m_initialized)
        return;
    m_running = true;
    m_frameIndex = 0;
    m_clock.Reset();
    const bool unlimited = maxFrames == 0;
    m_logger.Info("Engine loop start.");
    while (m_running && (unlimited || m_frameIndex < maxFrames))
    {
        const auto frameStart = std::chrono::steady_clock::now();
        if (!m_renderer.PumpEvents())
            break;
        const float deltaTime = std::clamp(m_clock.Tick(), 0.0f, 0.25f);
        Tick(deltaTime);
        ++m_frameIndex;
        const auto elapsed = std::chrono::steady_clock::now() - frameStart;
        constexpr auto minimumFrame = std::chrono::milliseconds(1);
        if (elapsed < minimumFrame)
            std::this_thread::sleep_for(minimumFrame - elapsed);
    }
    m_running = false;
    m_logger.Info("Engine loop end.");
}

// 先停止产生新工作的上层系统，再等待作业和资源，最后销毁平台/渲染后端。
void Engine::Shutdown()
{
    m_running = false;
    if (m_gameInstance)
        m_gameInstance->OnShutdown();
    m_pluginManager.UnloadAll();
    m_moduleManager.ShutdownAll();
    m_worldPartition.UnloadAll();
    m_navigation.Clear();
    m_scriptEngine.Shutdown();
    m_gameplay.Shutdown();
    m_vfxSystem.Clear();
    m_audioEngine.Shutdown();
    m_renderer.Shutdown();
    m_physicsWorld.Clear();
    m_world.Shutdown();
    m_world.RemoveListener(this);
    m_resourceManager.Shutdown();
    m_assetStreaming.Shutdown();
    m_jobSystem.Shutdown();
    m_memoryManager.Shutdown();
    if (m_projectPakMounted)
    {
        PlatformFile::ClearMounts();
        m_projectPakMounted = false;
    }
    m_context = {};
    if (m_initialized)
        m_logger.Info("Engine shutdown complete.");
    m_initialized = false;
    m_playing = false;
    m_diagnostics.Shutdown();
}

const EngineContext &Engine::GetContext() const
{
    return m_context;
}
void Engine::SetPaused(const bool paused)
{
    m_paused = paused;
}
bool Engine::IsPaused() const
{
    return m_paused;
}
void Engine::SetTimeScale(const float scale)
{
    m_timeScale = std::max(0.0f, scale);
}

bool Engine::LoadScene(const std::filesystem::path &path)
{
    if (m_playing)
        return false;
    m_scriptEngine.ClearInstances();
    const bool loaded = m_sceneManager.LoadScene(path);
    if (loaded && m_gameInstance)
        m_gameInstance->OnSceneLoaded(path);
    return loaded;
}

bool Engine::SaveScene(const std::filesystem::path &path)
{
    return !m_playing && m_sceneManager.SaveScene(path);
}

// Play 前保存完整编辑态快照，使运行时产生、删除和修改的实体都能一次性回滚。
bool Engine::EnterPlayMode()
{
    if (!m_initialized || m_playing)
        return false;
    m_editSnapshot = m_sceneManager.CreateSnapshot();
    if (m_editSnapshot.empty())
        return false;
    m_scriptEngine.RebuildWorldInstances(m_world);
    m_gameplay.StartPlay();
    m_fixedAccumulator = 0.0f;
    m_paused = false;
    m_playing = true;
    return true;
}

bool Engine::ExitPlayMode()
{
    if (!m_initialized || !m_playing)
        return false;
    m_scriptEngine.ClearInstances();
    m_gameplay.StopPlay();
    const bool restored = m_sceneManager.RestoreSnapshot(m_editSnapshot);
    m_editSnapshot.clear();
    m_fixedAccumulator = 0.0f;
    m_paused = true;
    m_playing = false;
    return restored;
}

bool Engine::IsPlaying() const
{
    return m_playing;
}
SceneManager &Engine::GetSceneManager()
{
    return m_sceneManager;
}
ProjectSettings &Engine::GetProjectSettings()
{
    return m_projectSettings;
}
void Engine::SetGameInstance(std::unique_ptr<GameInstance> gameInstance)
{
    m_gameInstance = std::move(gameInstance);
}

ModuleManager &Engine::GetModuleManager()
{
    return m_moduleManager;
}

PluginManager &Engine::GetPluginManager()
{
    return m_pluginManager;
}

// 编辑器退出确认与场景另存为流程通过它请求结束主循环，避免直接触达私有 m_running。
void Engine::RequestExit()
{
    m_running = false;
}

// 可变步长驱动表现层，物理通过累加器以固定步长追帧，避免不同刷新率得到不同结果。
void Engine::Tick(const float rawDeltaTime)
{
    const float deltaTime = m_paused ? 0.0f : rawDeltaTime * m_timeScale;
    const InputState &input = m_renderer.GetInputState();
    m_uiSystem.Update(input.cursorX, input.cursorY, input.primaryAction);
    if (!input.textInput.empty())
        m_uiSystem.InputText(input.textInput);
    if (input.uiBackspace)
        m_uiSystem.Backspace();
    if (input.uiNavigateNext)
        m_uiSystem.FocusNext();
    if (input.uiAccept)
        m_uiSystem.ActivateFocused();
    m_inputSystem.Update(input);
    if (deltaTime > 0.0f)
    {
        m_gameplay.Update(deltaTime);
        m_scriptEngine.UpdateAll(deltaTime);
        m_pluginManager.Tick(deltaTime);
        if (m_gameInstance)
            m_gameInstance->OnUpdate(deltaTime);
        m_world.Update(deltaTime);
        m_vfxSystem.Update(deltaTime);
        const float fixedStep = m_projectSettings.fixedStep;
        m_fixedAccumulator = std::min(m_fixedAccumulator + deltaTime, 0.25f);
        while (m_fixedAccumulator >= fixedStep)
        {
            m_physicsWorld.Step(m_world, fixedStep);
            m_fixedAccumulator -= fixedStep;
        }
        m_audioEngine.UpdateWorld(m_world);
    }
    m_renderer.RenderFrame(m_world);
    const auto &profile = m_renderer.GetLastFrameProfile();
    auto &stats = m_diagnostics.GetStats();
    stats.Set("frame.delta_ms", rawDeltaTime * 1000.0);
    stats.Set("render.total_ms", profile.totalMilliseconds);
    stats.Set("render.draw_calls", static_cast<double>(m_renderer.GetLastDrawCallCount()));
    stats.Set("render.graph_passes", static_cast<double>(profile.graphPasses));
    stats.Set("render.graph_physical_resources", static_cast<double>(profile.graphPhysicalResources));
    stats.Set("render.cluster_light_references", static_cast<double>(profile.clusteredLightReferences));
    stats.Set("render.gpu_resident_bytes",
              m_renderer.GetDevice() ? static_cast<double>(m_renderer.GetDevice()->GetResidentGpuBytes()) : 0.0);
    stats.Set("asset.resident_bytes", static_cast<double>(m_assetStreaming.GetResidentBytes()));
    stats.Set("job.outstanding", static_cast<double>(m_jobSystem.GetOutstandingJobCount()));
    m_memoryManager.ResetFrame();
    if ((m_frameIndex % 60) == 0)
        m_assetStreaming.Update();
}

void Engine::SetupDemoScene()
{
    m_playerEntity = m_world.CreateEntity("Player");
    if (auto *transform = m_world.GetComponent<TransformComponent>(m_playerEntity))
        transform->position = {0.0f, 1.0f, 0.0f};
    m_world.AddComponent(m_playerEntity, MeshComponent{"assets/player.mesh", "assets/default.mat"});
    m_world.AddComponent(m_playerEntity, ScriptComponent{"OrbitScript"});
    m_world.AddComponent(m_playerEntity, PawnComponent{});
    m_world.AddComponent(m_playerEntity, CharacterControllerComponent{});
    m_world.AddComponent(m_playerEntity, RigidBodyComponent{BodyType::Kinematic, 1.0f, {}, false});
    m_world.AddComponent(m_playerEntity, ColliderComponent{});

    const Entity camera = m_world.CreateEntity("MainCamera");
    if (auto *transform = m_world.GetComponent<TransformComponent>(camera))
    {
        transform->position = {0.0f, 2.5f, 7.0f};
        transform->rotation = {-10.0f, 0.0f, 0.0f};
    }
    m_world.AddComponent(camera, CameraComponent{});

    const Entity light = m_world.CreateEntity("Sun");
    m_world.AddComponent(light, LightComponent{});

    const Entity floor = m_world.CreateEntity("GroundCollider");
    if (auto *transform = m_world.GetComponent<TransformComponent>(floor))
    {
        transform->position = {0.0f, -0.25f, 0.0f};
        transform->scale = {20.0f, 0.5f, 20.0f};
    }
    m_world.AddComponent(floor, RigidBodyComponent{BodyType::Static, 0.0f, {}, false});
    m_world.AddComponent(floor, ColliderComponent{});

    const Entity animated = m_world.CreateEntity("AnimatedMesh");
    m_world.AddComponent(animated, MeshComponent{"assets/player.mesh", "assets/default.mat"});
    m_world.AddComponent(animated, AnimationComponent{"assets/player.anim"});

    m_assetStreaming.Request<MeshResource>("assets/player.mesh", true);
    m_assetStreaming.Request<MaterialResource>("assets/default.mat", true);
    ParticleEmitterSettings demoParticles;
    demoParticles.maximumParticles = 64;
    demoParticles.emissionRate = 6.0f;
    demoParticles.lifetime = 1.5f;
    demoParticles.speed = 1.2f;
    demoParticles.gravity = {0.0f, -0.4f, 0.0f};
    demoParticles.startColor = {1.0f, 0.55f, 0.12f, 0.9f};
    demoParticles.endColor = {0.8f, 0.1f, 0.02f, 0.0f};
    const auto emitterId = m_vfxSystem.CreateEmitter(demoParticles);
    if (auto *emitter = m_vfxSystem.FindEmitter(emitterId))
        emitter->SetPosition({0.0f, 0.25f, 0.0f});
    m_uiSystem.AddPanel("demo.hud", {16.0f, 16.0f, 270.0f, 112.0f}, {0.05f, 0.08f, 0.13f, 0.88f});
    m_uiSystem.SetLayout("demo.hud", UILayout::Vertical, 6.0f, 10.0f);
    m_uiSystem.AddLabel("demo.title", "TinyEngine Runtime UI", {0.0f, 0.0f, 200.0f, 30.0f}).fontSize = 18.0f;
    m_uiSystem.AddButton("demo.action", "Activate", {0.0f, 0.0f, 150.0f, 40.0f},
                         [this]() { m_logger.Info("Runtime UI button activated."); });
    m_uiSystem.SetParent("demo.title", "demo.hud");
    m_uiSystem.SetParent("demo.action", "demo.hud");
    SceneSerializer::Save(m_world, "Saved/Demo.scene");
}

void Engine::OnEntityDestroying(World &, const Entity entity)
{
    m_scriptEngine.DestroyEntity(entity);
}
void Engine::OnWorldCleared(World &)
{
    m_scriptEngine.ClearInstances();
    m_audioEngine.ClearWorldState();
    m_playerEntity = InvalidEntity;
}
} // MiniEngine 命名空间
