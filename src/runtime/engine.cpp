// 文件：src/runtime/engine.cpp
// 作用：组装全部运行时子系统并驱动初始化、主循环、游戏态切换和关闭。
// 约定：子系统初始化与关闭顺序严格相反，避免悬空服务指针。
// 类型：实现文件。

#include "tinyengine/runtime/engine.h"

#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/ecs/systems.h"
#include "tinyengine/render/sdl_gl_render_device.h"

#include "tinyengine/platform/platform.h"
#include <algorithm>
#include <chrono>
#include <thread>

namespace tinyengine
{
// 初始化按“基础服务 -> 资源与世界 -> 表现系统 -> 项目内容”的依赖方向进行；任一步失败都由 Shutdown 逆序收尾。
bool Engine::initialize(const std::filesystem::path& projectFile, const bool startInPlayMode)
{
    if (m_initialized)
        return true;
    m_diagnostics.initialize();
    m_logger.info("Engine initialize begin.");
    m_projectCodeError.clear();
    m_useProjectCode = false;
    m_projectBridge.setWorldAccess(false, false);
    m_projectBridge.invalidateWorld();
    m_projectModuleHost = std::make_unique<ProjectModuleHost>();
    const bool projectLoaded = !projectFile.empty() && m_projectSettings.load(projectFile);
    if (!projectFile.empty() && !projectLoaded)
    {
        m_projectCodeError = "Cannot load explicit project: " + toUtf8Path(projectFile);
        m_logger.error(m_projectCodeError);
        shutdown();
        return false;
    }
    m_logger.setStructuredOutput(projectLoaded ? m_projectSettings.resolve("Logs/TinyEngine.jsonl")
                                               : std::filesystem::path{"Logs/TinyEngine.jsonl"});
    if (projectLoaded && !m_projectSettings.packageArchive.empty())
    {
        PlatformFile::clearMounts();
        m_projectPakMounted = PlatformFile::mountPak(m_projectSettings.resolve(m_projectSettings.packageArchive), 100);
        if (!m_projectPakMounted)
        {
            m_logger.error("Cannot mount project package: " + m_projectSettings.packageArchive.generic_string());
            return false;
        }
        PlatformFile::setReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
    }
    m_memoryManager.initialize();
    const auto hardwareCount = std::thread::hardware_concurrency();
    m_jobSystem.initialize(std::max(1u, hardwareCount > 1 ? hardwareCount - 1 : 1u));
    if (projectLoaded && !m_projectSettings.codeModule.empty())
    {
        ProjectModuleSelection selection;
#ifdef NDEBUG
        constexpr auto kProfile = ProjectCodeProfile::Shipping;
#else
        constexpr auto kProfile = ProjectCodeProfile::Development;
#endif
        if (loadProjectModuleSelection(m_projectSettings, kProfile, selection, &m_projectCodeError))
        {
            m_useProjectCode = m_projectModuleHost->load(selection, m_projectBridge.GetAPI());
            if (!m_useProjectCode)
                m_projectCodeError = m_projectModuleHost->GetLastError();
        }
        if (!m_projectCodeError.empty())
        {
            m_logger.error(m_projectCodeError);
            if (startInPlayMode && m_projectSettings.codeRequired)
            {
                shutdown();
                return false;
            }
        }
    }
    m_resourceManager.initialize(&m_jobSystem,
                                 projectLoaded ? m_projectSettings.resolve(m_projectSettings.assetDirectory)
                                               : std::filesystem::path{"assets"},
                                 projectLoaded ? m_projectSettings.assetDirectory : std::filesystem::path{"assets"});
    m_assetStreaming.initialize(&m_resourceManager);
    if (projectLoaded && !m_projectSettings.inputBindings.empty())
    {
        const auto inputPath = m_projectPakMounted ? m_projectSettings.inputBindings
                                                   : m_projectSettings.resolve(m_projectSettings.inputBindings);
        if (!m_inputSystem.loadBindings(inputPath.generic_string()))
            m_inputSystem.resetDefaultBindings();
    }
    else
        m_inputSystem.resetDefaultBindings();
    m_world.GetSystemManager().RegisterSystem<MovementSystem>();
    m_world.GetSystemManager().RegisterSystem<AnimationSystem>();
    m_world.addListener(this);
    m_world.initialize();
    m_gameplay.initialize(&m_world, &m_inputSystem, &m_physicsWorld);
    m_renderer.setLogger(&m_logger);
    m_renderer.setResourceManager(&m_resourceManager);
    m_renderer.setUiSystem(&m_uiSystem);
    m_renderer.setVfxSystem(&m_vfxSystem);
    if (projectLoaded && !m_projectSettings.environmentTexture.empty())
        m_renderer.setEnvironmentPath(
            m_projectPakMounted ? m_projectSettings.environmentTexture.generic_string()
                                : m_projectSettings.resolve(m_projectSettings.environmentTexture).generic_string());
    if (!m_renderer.initialize(std::make_unique<SdlGlRenderDevice>(
            1280, 720, projectLoaded ? m_projectSettings.name : "Aster-Engine SDL2 + OpenGL")))
    {
        m_logger.error("Renderer initialize failed.");
        shutdown();
        return false;
    }
    if (auto* device = dynamic_cast<SdlGlRenderDevice*>(m_renderer.getDevice()))
        m_inputSystem.setHapticCallback([device](const float low, const float high, const std::uint32_t milliseconds)
                                        { return device->rumbleController(low, high, milliseconds); });
    m_audioEngine.initialize(&m_logger);
    m_scriptEngine.initialize(&m_logger);
    auto& console = m_diagnostics.GetConsole();
    console.registerVariable("t.TimeScale", 1.0f, "Global gameplay time scale", false,
                             [this](const ConsoleValue& value) { setTimeScale(std::get<float>(value)); });
    console.registerVariable("r.FXAA", true, "Enable post-process FXAA", false,
                             [this](const ConsoleValue& value)
                             {
                                 if (auto* device = dynamic_cast<SdlGlRenderDevice*>(m_renderer.getDevice()))
                                     device->setFxaaEnabled(std::get<bool>(value));
                             });
    console.registerVariable("r.Bloom", true, "Enable multi-level bloom", false,
                             [this](const ConsoleValue& value)
                             {
                                 if (auto* device = dynamic_cast<SdlGlRenderDevice*>(m_renderer.getDevice()))
                                     device->setBloomEnabled(std::get<bool>(value));
                             });
    console.registerVariable("r.GTAO", true, "Enable ground-truth ambient occlusion approximation", false,
                             [this](const ConsoleValue& value)
                             {
                                 if (auto* device = dynamic_cast<SdlGlRenderDevice*>(m_renderer.getDevice()))
                                     device->setGtaoEnabled(std::get<bool>(value));
                             });
    console.registerVariable("r.TAA", true, "Enable motion-vector temporal anti-aliasing", false,
                             [this](const ConsoleValue& value)
                             {
                                 if (auto* device = dynamic_cast<SdlGlRenderDevice*>(m_renderer.getDevice()))
                                     device->setTaaEnabled(std::get<bool>(value));
                             });
    console.registerVariable("asset.StreamingBudgetMB", 256, "Runtime resource memory budget in MiB", false,
                             [this](const ConsoleValue& value) {
                                 m_assetStreaming.setMemoryBudget(
                                     static_cast<std::size_t>(std::max(1, std::get<int>(value))) * 1024u * 1024u);
                             });
    console.registerVariable("r.GpuBudgetMB", 512, "OpenGL mesh and texture memory budget in MiB", false,
                             [this](const ConsoleValue& value)
                             {
                                 if (auto* device = m_renderer.getDevice())
                                     device->setGpuMemoryBudget(
                                         static_cast<std::size_t>(std::max(1, std::get<int>(value))) * 1024u * 1024u);
                             });
    console.registerCommand("asset.gc",
                            [this](const std::vector<std::string>&)
                            {
                                m_resourceManager.garbageCollect();
                                return std::string{"Resource garbage collection complete"};
                            });
    console.registerCommand("r.TrimGpu",
                            [this](const std::vector<std::string>&)
                            {
                                if (auto* device = m_renderer.getDevice())
                                    device->trimGpuResources();
                                return std::string{"GPU resource caches cleared"};
                            });
    m_context = {&m_logger,       &m_memoryManager,   &m_jobSystem,    &m_resourceManager, &m_world,
                 &m_renderer,     &m_inputSystem,     &m_physicsWorld, &m_audioEngine,     &m_scriptEngine,
                 &m_sceneManager, &m_projectSettings, &m_uiSystem,     &m_assetStreaming,  &m_gameplay,
                 &m_diagnostics,  &m_vfxSystem,       &m_navigation,   &m_worldPartition,  &m_moduleManager,
                 &m_pluginManager};
    m_pluginManager.setLogCallback(
        [this](const TinyPluginLogLevel level, const std::string& message)
        {
            if (level == TINY_PLUGIN_LOG_ERROR)
                m_logger.error(message);
            else if (level == TINY_PLUGIN_LOG_WARNING)
                m_logger.warning(message);
            else
                m_logger.info(message);
        });
    if (!m_moduleManager.startupAll())
    {
        m_logger.error("Module startup failed because a dependency is missing, cyclic, or failed to initialize.");
        shutdown();
        return false;
    }
    bool sceneLoaded = false;
    if (projectLoaded)
        sceneLoaded =
            m_sceneManager.loadScene(m_projectPakMounted ? m_projectSettings.startupScene
                                                         : m_projectSettings.resolve(m_projectSettings.startupScene));
    if (projectLoaded && !sceneLoaded)
    {
        m_projectCodeError =
            "Cannot load startup scene: " + toUtf8Path(m_projectSettings.resolve(m_projectSettings.startupScene));
        m_logger.error(m_projectCodeError);
        shutdown();
        return false;
    }
    if (!projectLoaded)
        setupDemoScene();
    m_projectBridge.setWorldAccess(true, false);
    if (m_gameInstance)
        m_gameInstance->onInitialize();
    m_initialized = true;
    if (startInPlayMode)
    {
        if (!enterPlayMode())
        {
            shutdown();
            return false;
        }
    }
    else
        m_paused = true;
    const auto* glDevice = dynamic_cast<const SdlGlRenderDevice*>(m_renderer.getDevice());
    m_logger.info(std::string{"Engine initialize complete. OpenGL 4.3 core renderer active; GPU Driven: "} +
                  (glDevice && glDevice->isGpuDrivenCullingSupported() ? "Compute + Indirect." : "fallback."));
    return true;
}

// maxFrames 为测试提供确定的退出条件；传 0 时由窗口关闭或外部状态结束循环。
void Engine::Run(const std::uint32_t maxFrames)
{
    if (!m_initialized)
        return;
    m_running = true;
    m_frameIndex = 0;
    m_clock.reset();
    const bool unlimited = maxFrames == 0;
    m_logger.info("Engine loop start.");
    while (m_running && (unlimited || m_frameIndex < maxFrames))
    {
        const auto frameStart = std::chrono::steady_clock::now();
        if (!m_renderer.pumpEvents())
            break;
        const float deltaTime = std::clamp(m_clock.Tick(), 0.0f, 0.25f);
        Tick(deltaTime);
        ++m_frameIndex;
        const auto elapsed = std::chrono::steady_clock::now() - frameStart;
        constexpr auto kMinimumFrame = std::chrono::milliseconds(1);
        if (elapsed < kMinimumFrame)
            std::this_thread::sleep_for(kMinimumFrame - elapsed);
    }
    m_running = false;
    m_logger.info("Engine loop end.");
}

// 先停止产生新工作的上层系统，再等待作业和资源，最后销毁平台/渲染后端。
void Engine::shutdown()
{
    m_running = false;
    if (m_playing)
        exitPlayMode();
    if (m_gameInstance && m_initialized)
        m_gameInstance->onShutdown();
    m_pluginManager.unloadAll();
    m_moduleManager.shutdownAll();
    m_projectBridge.setWorldAccess(false, false);
    m_projectModuleHost->Close();
    m_worldPartition.unloadAll();
    m_navigation.clear();
    m_scriptEngine.shutdown();
    m_gameplay.shutdown();
    m_vfxSystem.clear();
    m_audioEngine.shutdown();
    m_renderer.shutdown();
    m_physicsWorld.clear();
    m_world.shutdown();
    m_world.removeListener(this);
    m_resourceManager.shutdown();
    m_assetStreaming.shutdown();
    m_jobSystem.shutdown();
    m_memoryManager.shutdown();
    if (m_projectPakMounted)
    {
        PlatformFile::clearMounts();
        m_projectPakMounted = false;
    }
    m_context = {};
    if (m_initialized)
        m_logger.info("Engine shutdown complete.");
    m_initialized = false;
    m_playing = false;
    m_diagnostics.shutdown();
}

const EngineContext& Engine::GetContext() const
{
    return m_context;
}
void Engine::setPaused(const bool paused)
{
    m_paused = paused;
}
bool Engine::isPaused() const
{
    return m_paused;
}
void Engine::setTimeScale(const float scale)
{
    m_timeScale = std::max(0.0f, scale);
}

bool Engine::loadScene(const std::filesystem::path& path)
{
    if (m_playing)
        return false;
    m_scriptEngine.clearInstances();
    const bool loaded = m_sceneManager.loadScene(path);
    if (loaded && m_gameInstance)
        m_gameInstance->onSceneLoaded(path);
    return loaded;
}

bool Engine::saveScene(const std::filesystem::path& path)
{
    return !m_playing && m_sceneManager.saveScene(path);
}

// Play 前保存完整编辑态快照，使运行时产生、删除和修改的实体都能一次性回滚。
bool Engine::enterPlayMode()
{
    if (!m_initialized || m_playing)
        return false;
    if (m_projectSettings.codeRequired && !m_useProjectCode)
        return false;
    if (m_useProjectCode && m_projectModuleHost->getState() != ProjectModuleState::Registered)
    {
        m_projectCodeError = m_projectModuleHost->GetLastError();
        return false;
    }
    m_editSnapshot = m_sceneManager.createSnapshot();
    if (m_editSnapshot.empty())
        return false;
    m_scriptEngine.rebuildWorldInstances(m_world);
    m_gameplay.startPlay();
    m_projectBridge.setWorldAccess(true, true);
    if (m_useProjectCode && !m_projectModuleHost->beginPlay(m_projectBridge.getWorld()))
    {
        m_projectCodeError = m_projectModuleHost->GetLastError();
        m_logger.error(m_projectCodeError);
        m_scriptEngine.clearInstances();
        m_gameplay.stopPlay();
        m_sceneManager.restoreSnapshot(m_editSnapshot);
        m_editSnapshot.clear();
        m_projectBridge.setWorldAccess(true, false);
        m_paused = true;
        return false;
    }
    m_fixedAccumulator = 0.0f;
    m_paused = false;
    m_playing = true;
    return true;
}

bool Engine::exitPlayMode()
{
    if (!m_initialized || !m_playing)
        return false;
    if (m_useProjectCode)
        m_projectModuleHost->endPlay();
    m_scriptEngine.clearInstances();
    m_gameplay.stopPlay();
    const bool restored = m_sceneManager.restoreSnapshot(m_editSnapshot);
    m_editSnapshot.clear();
    m_fixedAccumulator = 0.0f;
    m_paused = true;
    m_playing = false;
    m_projectBridge.setWorldAccess(true, false);
    return restored;
}

bool Engine::isPlaying() const
{
    return m_playing;
}
SceneManager& Engine::GetSceneManager()
{
    return m_sceneManager;
}
ProjectSettings& Engine::getProjectSettings()
{
    return m_projectSettings;
}
void Engine::setGameInstance(std::unique_ptr<GameInstance> gameInstance)
{
    m_gameInstance = std::move(gameInstance);
}

void Engine::setTextInputManagedByUi(const bool enabled)
{
    m_textInputManagedByUi = enabled;
}

ModuleManager& Engine::GetModuleManager()
{
    return m_moduleManager;
}

PluginManager& Engine::GetPluginManager()
{
    return m_pluginManager;
}

// 编辑器退出确认与场景另存为流程通过它请求结束主循环，避免直接触达私有 m_running。
void Engine::requestExit()
{
    m_running = false;
}

// 可变步长驱动表现层，物理通过累加器以固定步长追帧，避免不同刷新率得到不同结果。
void Engine::Tick(const float rawDeltaTime)
{
    const float deltaTime = m_paused ? 0.0f : rawDeltaTime * m_timeScale;
    const InputState& input = m_renderer.GetInputState();
    m_uiSystem.Update(input.cursorX, input.cursorY, input.primaryAction);
    if (m_textInputManagedByUi)
    {
        if (auto* device = dynamic_cast<SdlGlRenderDevice*>(m_renderer.getDevice()))
        {
            const auto* focused = m_uiSystem.Find(m_uiSystem.GetFocusedId());
            device->setTextInputEnabled(focused && focused->type == UIWidgetType::InputField && focused->visible &&
                                        focused->enabled);
        }
    }
    if (!input.textInput.empty())
        m_uiSystem.inputText(input.textInput);
    if (input.uiBackspace)
        m_uiSystem.backspace();
    if (input.uiNavigateNext)
        m_uiSystem.focusNext();
    if (input.uiAccept)
        m_uiSystem.activateFocused();
    m_inputSystem.Update(input);
    if (deltaTime > 0.0f)
    {
        m_gameplay.Update(deltaTime);
        m_scriptEngine.updateAll(deltaTime);
        m_pluginManager.Tick(deltaTime);
        if (m_useProjectCode && !m_projectModuleHost->Update(deltaTime))
        {
            m_projectCodeError = m_projectModuleHost->GetLastError();
            m_logger.error(m_projectCodeError);
            exitPlayMode();
            return;
        }
        if (!m_useProjectCode && m_gameInstance)
            m_gameInstance->onUpdate(deltaTime);
        m_world.Update(deltaTime);
        m_vfxSystem.Update(deltaTime);
        const float fixedStep = m_projectSettings.fixedStep;
        m_fixedAccumulator = std::min(m_fixedAccumulator + deltaTime, 0.25f);
        while (m_fixedAccumulator >= fixedStep)
        {
            if (m_useProjectCode && !m_projectModuleHost->fixedUpdate(fixedStep))
            {
                m_projectCodeError = m_projectModuleHost->GetLastError();
                m_logger.error(m_projectCodeError);
                exitPlayMode();
                return;
            }
            m_physicsWorld.step(m_world, fixedStep);
            m_fixedAccumulator -= fixedStep;
        }
        m_audioEngine.updateWorld(m_world);
        if (m_useProjectCode && !m_projectModuleHost->lateUpdate(deltaTime))
        {
            m_projectCodeError = m_projectModuleHost->GetLastError();
            m_logger.error(m_projectCodeError);
            exitPlayMode();
            return;
        }
    }
    m_renderer.renderFrame(m_world);
    const auto& profile = m_renderer.GetLastFrameProfile();
    auto& stats = m_diagnostics.GetStats();
    stats.set("frame.delta_ms", rawDeltaTime * 1000.0);
    stats.set("render.total_ms", profile.totalMilliseconds);
    stats.set("render.draw_calls", static_cast<double>(m_renderer.getLastDrawCallCount()));
    stats.set("render.graph_passes", static_cast<double>(profile.graphPasses));
    stats.set("render.graph_physical_resources", static_cast<double>(profile.graphPhysicalResources));
    stats.set("render.cluster_light_references", static_cast<double>(profile.clusteredLightReferences));
    stats.set("render.gpu_resident_bytes",
              m_renderer.getDevice() ? static_cast<double>(m_renderer.getDevice()->getResidentGpuBytes()) : 0.0);
    stats.set("asset.resident_bytes", static_cast<double>(m_assetStreaming.getResidentBytes()));
    stats.set("job.outstanding", static_cast<double>(m_jobSystem.getOutstandingJobCount()));
    m_memoryManager.resetFrame();
    if ((m_frameIndex % 60) == 0)
        m_assetStreaming.Update();
}

void Engine::setupDemoScene()
{
    m_playerEntity = m_world.createEntity("Player");
    if (auto* transform = m_world.GetComponent<TransformComponent>(m_playerEntity))
        transform->position = {0.0f, 1.0f, 0.0f};
    m_world.AddComponent(m_playerEntity, MeshComponent{"assets/player.mesh", "assets/default.mat"});
    m_world.AddComponent(m_playerEntity, ScriptComponent{"OrbitScript"});
    m_world.AddComponent(m_playerEntity, PawnComponent{});
    m_world.AddComponent(m_playerEntity, CharacterControllerComponent{});
    m_world.AddComponent(m_playerEntity, RigidBodyComponent{BodyType::Kinematic, 1.0f, {}, false});
    m_world.AddComponent(m_playerEntity, ColliderComponent{});

    const Entity camera = m_world.createEntity("MainCamera");
    if (auto* transform = m_world.GetComponent<TransformComponent>(camera))
    {
        transform->position = {0.0f, 2.5f, 7.0f};
        transform->rotation = {-10.0f, 0.0f, 0.0f};
    }
    m_world.AddComponent(camera, CameraComponent{});

    const Entity light = m_world.createEntity("Sun");
    m_world.AddComponent(light, LightComponent{});

    const Entity floor = m_world.createEntity("GroundCollider");
    if (auto* transform = m_world.GetComponent<TransformComponent>(floor))
    {
        transform->position = {0.0f, -0.25f, 0.0f};
        transform->scale = {20.0f, 0.5f, 20.0f};
    }
    m_world.AddComponent(floor, RigidBodyComponent{BodyType::Static, 0.0f, {}, false});
    m_world.AddComponent(floor, ColliderComponent{});

    const Entity animated = m_world.createEntity("AnimatedMesh");
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
    const auto emitterId = m_vfxSystem.createEmitter(demoParticles);
    if (auto* emitter = m_vfxSystem.findEmitter(emitterId))
        emitter->setPosition({0.0f, 0.25f, 0.0f});
    m_uiSystem.AddPanel("demo.hud", {16.0f, 16.0f, 270.0f, 112.0f}, {0.05f, 0.08f, 0.13f, 0.88f});
    m_uiSystem.setLayout("demo.hud", UILayout::Vertical, 6.0f, 10.0f);
    m_uiSystem.AddLabel("demo.title", "Aster-Engine Runtime UI", {0.0f, 0.0f, 200.0f, 30.0f}).fontSize = 18.0f;
    m_uiSystem.AddButton("demo.action", "Activate", {0.0f, 0.0f, 150.0f, 40.0f},
                         [this]() { m_logger.info("Runtime UI button activated."); });
    m_uiSystem.setParent("demo.title", "demo.hud");
    m_uiSystem.setParent("demo.action", "demo.hud");
    SceneSerializer::Save(m_world, "saved/Demo.scene");
}

void Engine::onEntityDestroying(World&, const Entity entity)
{
    m_scriptEngine.destroyEntity(entity);
}
void Engine::onWorldCleared(World&)
{
    m_projectBridge.invalidateWorld();
    m_scriptEngine.clearInstances();
    m_audioEngine.clearWorldState();
    m_playerEntity = kInvalidEntity;
}
} // namespace tinyengine
