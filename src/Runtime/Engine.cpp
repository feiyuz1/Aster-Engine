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
    bool Engine::Initialize(const std::filesystem::path& projectFile, const bool startInPlayMode)
    {
        if (m_initialized) return true;
        m_logger.Info("Engine initialize begin.");
        const bool projectLoaded = !projectFile.empty() && m_projectSettings.Load(projectFile);
        m_logger.SetStructuredOutput(projectLoaded ? m_projectSettings.Resolve("Logs/TinyEngine.jsonl") : std::filesystem::path{"Logs/TinyEngine.jsonl"});
        if (projectLoaded && !m_projectSettings.packageArchive.empty())
        {
            PlatformFile::ClearMounts();
            m_projectPakMounted = PlatformFile::MountPak(m_projectSettings.Resolve(m_projectSettings.packageArchive), 100);
            if (!m_projectPakMounted)
            {
                m_logger.Error("Cannot mount project package: " + m_projectSettings.packageArchive.generic_string());
                return false;
            }
        }
        m_memoryManager.Initialize();
        const auto hardwareCount = std::thread::hardware_concurrency();
        m_jobSystem.Initialize(std::max(1u, hardwareCount > 1 ? hardwareCount - 1 : 1u));
        m_resourceManager.Initialize(&m_jobSystem, projectLoaded ? m_projectSettings.Resolve(m_projectSettings.assetDirectory) : std::filesystem::path{"assets"});
        if (projectLoaded && !m_projectSettings.inputBindings.empty())
        {
            const auto inputPath = m_projectPakMounted ? m_projectSettings.inputBindings : m_projectSettings.Resolve(m_projectSettings.inputBindings);
            if (!m_inputSystem.LoadBindings(inputPath.generic_string())) m_inputSystem.ResetDefaultBindings();
        }
        else m_inputSystem.ResetDefaultBindings();
        m_world.GetSystemManager().RegisterSystem<MovementSystem>();
        m_world.GetSystemManager().RegisterSystem<AnimationSystem>();
        m_world.AddListener(this);
        m_world.Initialize();
        m_renderer.SetLogger(&m_logger);
        m_renderer.SetResourceManager(&m_resourceManager);
        m_renderer.SetUISystem(&m_uiSystem);
        if (projectLoaded && !m_projectSettings.environmentTexture.empty()) m_renderer.SetEnvironmentPath(m_projectPakMounted ? m_projectSettings.environmentTexture.generic_string() : m_projectSettings.Resolve(m_projectSettings.environmentTexture).generic_string());
        if (!m_renderer.Initialize(std::make_unique<SdlGlRenderDevice>(1280, 720, projectLoaded ? m_projectSettings.name : "TinyEngine SDL2 + OpenGL")))
        {
            m_logger.Error("Renderer initialize failed.");
            Shutdown();
            return false;
        }
        m_audioEngine.Initialize(&m_logger);
        m_scriptEngine.Initialize(&m_logger);
        m_context = {&m_logger, &m_memoryManager, &m_jobSystem, &m_resourceManager, &m_world, &m_renderer, &m_inputSystem, &m_physicsWorld, &m_audioEngine, &m_scriptEngine, &m_sceneManager, &m_projectSettings, &m_uiSystem};
        bool sceneLoaded = false;
        if (projectLoaded)
            sceneLoaded = m_sceneManager.LoadScene(m_projectPakMounted ? m_projectSettings.startupScene : m_projectSettings.Resolve(m_projectSettings.startupScene));
        if (!sceneLoaded) SetupDemoScene();
        if (m_gameInstance) m_gameInstance->OnInitialize();
        m_initialized = true;
        if (startInPlayMode) EnterPlayMode(); else m_paused = true;
        m_logger.Info("Engine initialize complete. OpenGL 3.3 core renderer active.");
        return true;
    }

    void Engine::Run(const std::uint32_t maxFrames)
    {
        if (!m_initialized) return;
        m_running = true; m_frameIndex = 0; m_clock.Reset();
        const bool unlimited = maxFrames == 0;
        m_logger.Info("Engine loop start.");
        while (m_running && (unlimited || m_frameIndex < maxFrames))
        {
            const auto frameStart = std::chrono::steady_clock::now();
            if (!m_renderer.PumpEvents()) break;
            const float deltaTime = std::clamp(m_clock.Tick(), 0.0f, 0.25f);
            Tick(deltaTime); ++m_frameIndex;
            const auto elapsed = std::chrono::steady_clock::now() - frameStart;
            constexpr auto minimumFrame = std::chrono::milliseconds(1);
            if (elapsed < minimumFrame) std::this_thread::sleep_for(minimumFrame - elapsed);
        }
        m_running = false;
        m_logger.Info("Engine loop end.");
    }

    void Engine::Shutdown()
    {
        m_running = false;
        if (m_gameInstance) m_gameInstance->OnShutdown();
        m_scriptEngine.Shutdown();
        m_audioEngine.Shutdown();
        m_renderer.Shutdown();
        m_physicsWorld.Clear();
        m_world.Shutdown();
        m_world.RemoveListener(this);
        m_resourceManager.Shutdown();
        m_jobSystem.Shutdown();
        m_memoryManager.Shutdown();
        if (m_projectPakMounted) { PlatformFile::ClearMounts(); m_projectPakMounted = false; }
        m_context = {};
        if (m_initialized) m_logger.Info("Engine shutdown complete.");
        m_initialized = false;
        m_playing = false;
    }

    const EngineContext& Engine::GetContext() const { return m_context; }
    void Engine::SetPaused(const bool paused) { m_paused = paused; }
    bool Engine::IsPaused() const { return m_paused; }
    void Engine::SetTimeScale(const float scale) { m_timeScale = std::max(0.0f, scale); }

    bool Engine::LoadScene(const std::filesystem::path& path)
    {
        if (m_playing) return false;
        m_scriptEngine.ClearInstances();
        const bool loaded = m_sceneManager.LoadScene(path);
        if (loaded && m_gameInstance) m_gameInstance->OnSceneLoaded(path);
        return loaded;
    }

    bool Engine::SaveScene(const std::filesystem::path& path) { return !m_playing && m_sceneManager.SaveScene(path); }

    bool Engine::EnterPlayMode()
    {
        if (!m_initialized || m_playing) return false;
        m_editSnapshot = m_sceneManager.CreateSnapshot();
        if (m_editSnapshot.empty()) return false;
        m_scriptEngine.RebuildWorldInstances(m_world);
        m_fixedAccumulator = 0.0f; m_paused = false; m_playing = true;
        return true;
    }

    bool Engine::ExitPlayMode()
    {
        if (!m_initialized || !m_playing) return false;
        m_scriptEngine.ClearInstances();
        const bool restored = m_sceneManager.RestoreSnapshot(m_editSnapshot);
        m_editSnapshot.clear(); m_fixedAccumulator = 0.0f; m_paused = true; m_playing = false;
        return restored;
    }

    bool Engine::IsPlaying() const { return m_playing; }
    SceneManager& Engine::GetSceneManager() { return m_sceneManager; }
    ProjectSettings& Engine::GetProjectSettings() { return m_projectSettings; }
    void Engine::SetGameInstance(std::unique_ptr<GameInstance> gameInstance) { m_gameInstance = std::move(gameInstance); }

    void Engine::Tick(const float rawDeltaTime)
    {
        const float deltaTime = m_paused ? 0.0f : rawDeltaTime * m_timeScale;
        const InputState& input = m_renderer.GetInputState();
        m_uiSystem.Update(input.cursorX, input.cursorY, input.primaryAction);
        if (input.uiNavigateNext) m_uiSystem.FocusNext();
        if (input.uiAccept) m_uiSystem.ActivateFocused();
        m_inputSystem.Update(input);
        if (deltaTime > 0.0f)
        {
            if (auto* transform = m_world.GetComponent<TransformComponent>(m_playerEntity))
            {
                const float moveSpeed = m_inputSystem.IsDown("Sprint") ? 8.0f : 3.0f;
                transform->position += Vec3{m_inputSystem.GetAxis("MoveX"), m_inputSystem.GetAxis("MoveY"), m_inputSystem.GetAxis("MoveZ")} * (moveSpeed * deltaTime);
                transform->rotation += Vec3{m_inputSystem.GetAxis("LookY"), m_inputSystem.GetAxis("LookX"), m_inputSystem.GetAxis("RotateZ")} * (90.0f * deltaTime);
                const float uniformScale = std::clamp(transform->scale.x + m_inputSystem.GetAxis("Scale") * deltaTime, 0.2f, 4.0f);
                transform->scale = {uniformScale, uniformScale, uniformScale};
            }
            m_scriptEngine.UpdateAll(deltaTime);
            if (m_gameInstance) m_gameInstance->OnUpdate(deltaTime);
            m_world.Update(deltaTime);
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
        m_memoryManager.ResetFrame();
        if ((m_frameIndex % 300) == 0) m_resourceManager.GarbageCollect();
    }

    void Engine::SetupDemoScene()
    {
        m_playerEntity = m_world.CreateEntity("Player");
        if (auto* transform = m_world.GetComponent<TransformComponent>(m_playerEntity)) transform->position = {0.0f, 1.0f, 0.0f};
        m_world.AddComponent(m_playerEntity, MeshComponent{"assets/player.mesh", "assets/default.mat"});
        m_world.AddComponent(m_playerEntity, ScriptComponent{"OrbitScript"});
        m_world.AddComponent(m_playerEntity, RigidBodyComponent{});
        m_world.AddComponent(m_playerEntity, ColliderComponent{});

        const Entity camera = m_world.CreateEntity("MainCamera");
        if (auto* transform = m_world.GetComponent<TransformComponent>(camera)) { transform->position = {0.0f, 2.5f, 7.0f}; transform->rotation = {-10.0f, 0.0f, 0.0f}; }
        m_world.AddComponent(camera, CameraComponent{});

        const Entity light = m_world.CreateEntity("Sun");
        m_world.AddComponent(light, LightComponent{});

        const Entity floor = m_world.CreateEntity("GroundCollider");
        if (auto* transform = m_world.GetComponent<TransformComponent>(floor)) { transform->position = {0.0f, -0.25f, 0.0f}; transform->scale = {20.0f, 0.5f, 20.0f}; }
        m_world.AddComponent(floor, RigidBodyComponent{BodyType::Static, 0.0f, {}, false});
        m_world.AddComponent(floor, ColliderComponent{});

        const Entity animated = m_world.CreateEntity("AnimatedMesh");
        m_world.AddComponent(animated, MeshComponent{"assets/player.mesh", "assets/default.mat"});
        m_world.AddComponent(animated, AnimationComponent{"assets/player.anim"});

        m_resourceManager.LoadAsync<MeshResource>("assets/player.mesh");
        m_resourceManager.LoadAsync<MaterialResource>("assets/default.mat");
        m_uiSystem.AddPanel("demo.hud", {16.0f, 16.0f, 270.0f, 112.0f}, {0.05f, 0.08f, 0.13f, 0.88f});
        m_uiSystem.SetLayout("demo.hud", UILayout::Vertical, 6.0f, 10.0f);
        m_uiSystem.AddLabel("demo.title", "TinyEngine Runtime UI", {0.0f, 0.0f, 200.0f, 30.0f}).fontSize = 18.0f;
        m_uiSystem.AddButton("demo.action", "Activate", {0.0f, 0.0f, 150.0f, 40.0f}, [this]() { m_logger.Info("Runtime UI button activated."); });
        m_uiSystem.SetParent("demo.title", "demo.hud"); m_uiSystem.SetParent("demo.action", "demo.hud");
        SceneSerializer::Save(m_world, "Saved/Demo.scene");
    }

    void Engine::OnEntityDestroying(World&, const Entity entity) { m_scriptEngine.DestroyEntity(entity); }
    void Engine::OnWorldCleared(World&) { m_scriptEngine.ClearInstances(); m_audioEngine.ClearWorldState(); m_playerEntity = InvalidEntity; }
}
