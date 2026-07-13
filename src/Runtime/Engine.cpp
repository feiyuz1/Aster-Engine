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
    bool Engine::Initialize()
    {
        if (m_initialized) return true;
        m_logger.Info("Engine initialize begin.");
        m_memoryManager.Initialize();
        const auto hardwareCount = std::thread::hardware_concurrency();
        m_jobSystem.Initialize(std::max(1u, hardwareCount > 1 ? hardwareCount - 1 : 1u));
        m_resourceManager.Initialize(&m_jobSystem);
        m_world.GetSystemManager().RegisterSystem<MovementSystem>();
        m_world.GetSystemManager().RegisterSystem<AnimationSystem>();
        m_world.Initialize();
        m_renderer.SetLogger(&m_logger);
        m_renderer.SetResourceManager(&m_resourceManager);
        if (!m_renderer.Initialize(std::make_unique<SdlGlRenderDevice>()))
        {
            m_logger.Error("Renderer initialize failed.");
            Shutdown();
            return false;
        }
        m_audioEngine.Initialize(&m_logger);
        m_scriptEngine.Initialize(&m_logger);
        m_context = {&m_logger, &m_memoryManager, &m_jobSystem, &m_resourceManager, &m_world, &m_renderer, &m_inputSystem, &m_physicsWorld, &m_audioEngine, &m_scriptEngine};
        SetupDemoScene();
        m_initialized = true;
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
        m_scriptEngine.Shutdown();
        m_audioEngine.Shutdown();
        m_renderer.Shutdown();
        m_physicsWorld.Clear();
        m_world.Shutdown();
        m_resourceManager.Shutdown();
        m_jobSystem.Shutdown();
        m_memoryManager.Shutdown();
        m_context = {};
        if (m_initialized) m_logger.Info("Engine shutdown complete.");
        m_initialized = false;
    }

    const EngineContext& Engine::GetContext() const { return m_context; }
    void Engine::SetPaused(const bool paused) { m_paused = paused; }
    bool Engine::IsPaused() const { return m_paused; }
    void Engine::SetTimeScale(const float scale) { m_timeScale = std::max(0.0f, scale); }

    void Engine::Tick(const float rawDeltaTime)
    {
        const float deltaTime = m_paused ? 0.0f : rawDeltaTime * m_timeScale;
        const InputState& input = m_renderer.GetInputState();
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
            m_world.Update(deltaTime);
            constexpr float fixedStep = 1.0f / 60.0f;
            m_fixedAccumulator = std::min(m_fixedAccumulator + deltaTime, 0.25f);
            while (m_fixedAccumulator >= fixedStep)
            {
                m_physicsWorld.Step(m_world, fixedStep);
                m_fixedAccumulator -= fixedStep;
            }
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

        m_scriptEngine.CreateInstance("OrbitScript", m_playerEntity, &m_world);
        m_resourceManager.LoadAsync<MeshResource>("assets/player.mesh");
        m_resourceManager.LoadAsync<MaterialResource>("assets/default.mat");
        SceneSerializer::Save(m_world, "Saved/Demo.scene");
    }
}
