#include "MiniEngine/Runtime/Engine.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/Systems.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#include "MiniEngine/Resource/Resource.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace MiniEngine
{
    bool Engine::Initialize()
    {
        m_logger.Info("Engine initialize begin.");

        m_memoryManager.Initialize();

        const auto hardwareCount = std::thread::hardware_concurrency();
        const std::uint32_t workerCount = std::max(1u, hardwareCount > 1 ? hardwareCount - 1 : 1u);
        m_jobSystem.Initialize(workerCount);

        m_resourceManager.Initialize(&m_jobSystem);

        m_world.GetSystemManager().RegisterSystem<MovementSystem>();
        m_world.Initialize();

        m_renderer.SetLogger(&m_logger);
        if (!m_renderer.Initialize(std::make_unique<SdlGlRenderDevice>()))
        {
            m_logger.Error("Renderer initialize failed.");
            return false;
        }

        m_scriptEngine.Initialize(&m_logger);

        m_context.logger = &m_logger;
        m_context.memoryManager = &m_memoryManager;
        m_context.jobSystem = &m_jobSystem;
        m_context.resourceManager = &m_resourceManager;
        m_context.world = &m_world;
        m_context.renderer = &m_renderer;
        m_context.scriptEngine = &m_scriptEngine;

        SetupDemoScene();

        m_logger.Info("Controls:");
        m_logger.Info("Move: W/S(front/back), A/D(left/right), R/F(up/down), Shift(accelerate)");
        m_logger.Info("Rotate: Arrow Up/Down(X), Arrow Left/Right(Y), Q/E(Z)");
        m_logger.Info("Scale: Z enlarge, X shrink, Esc or close window to quit");

        m_logger.Info("Engine initialize complete.");
        return true;
    }

    void Engine::Run(const std::uint32_t maxFrames)
    {
        m_running = true;
        m_frameIndex = 0;
        constexpr float targetDeltaTime = 1.0f / 60.0f;
        const bool runUnlimited = (maxFrames == 0);

        m_logger.Info("Engine loop start.");

        while (m_running && (runUnlimited || m_frameIndex < maxFrames))
        {
            if (!m_renderer.PumpEvents())
            {
                m_running = false;
                break;
            }

            const auto frameStart = std::chrono::steady_clock::now();
            Tick(targetDeltaTime);
            ++m_frameIndex;

            const auto frameEnd = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(frameEnd - frameStart);
            constexpr auto targetFrameTime = std::chrono::milliseconds(16);
            if (elapsed < targetFrameTime)
            {
                std::this_thread::sleep_for(targetFrameTime - elapsed);
            }
        }

        m_running = false;
        m_logger.Info("Engine loop end.");
    }

    void Engine::Shutdown()
    {
        m_running = false;

        m_scriptEngine.Shutdown();
        m_renderer.Shutdown();
        m_world.Shutdown();
        m_resourceManager.Shutdown();
        m_jobSystem.Shutdown();
        m_memoryManager.Shutdown();

        m_logger.Info("Engine shutdown complete.");
    }

    const EngineContext& Engine::GetContext() const
    {
        return m_context;
    }

    void Engine::Tick(const float deltaTime)
    {
        const InputState& input = m_renderer.GetInputState();
        if (auto* transform = m_world.GetComponent<TransformComponent>(m_playerEntity); transform != nullptr)
        {
            const float moveSpeed = input.sprint ? 8.0f : 3.0f;
            const float rotateSpeed = 90.0f;
            const float scaleSpeed = 1.2f;

            transform->position.x += input.moveX * moveSpeed * deltaTime;
            transform->position.y += input.moveY * moveSpeed * deltaTime;
            transform->position.z += input.moveZ * moveSpeed * deltaTime;

            transform->rotation.x += input.rotateX * rotateSpeed * deltaTime;
            transform->rotation.y += input.rotateY * rotateSpeed * deltaTime;
            transform->rotation.z += input.rotateZ * rotateSpeed * deltaTime;

            const float uniformScale = std::clamp(
                transform->scale.x + input.scale * scaleSpeed * deltaTime,
                0.2f,
                4.0f);
            transform->scale = {uniformScale, uniformScale, uniformScale};
        }

        m_scriptEngine.UpdateAll(deltaTime);
        m_world.Update(deltaTime);
        m_renderer.RenderFrame(m_world);
        m_memoryManager.ResetFrame();

        if (m_frameIndex % 30 == 0)
        {
            const auto* transform = m_world.GetComponent<TransformComponent>(m_playerEntity);
            if (transform != nullptr)
            {
                m_logger.Info(
                    "Player position: (" +
                    std::to_string(transform->position.x) + ", " +
                    std::to_string(transform->position.y) + ", " +
                    std::to_string(transform->position.z) + ")"
                    + "  rotation: (" +
                    std::to_string(transform->rotation.x) + ", " +
                    std::to_string(transform->rotation.y) + ", " +
                    std::to_string(transform->rotation.z) + ")"
                    + "  scale: " +
                    std::to_string(transform->scale.x));
            }
        }
    }

    void Engine::SetupDemoScene()
    {
        m_playerEntity = m_world.GetEntityManager().Create();

        TransformComponent transform{};
        transform.position = {0.0f, 0.0f, 0.0f};

        MeshComponent mesh{};
        mesh.meshPath = "assets/player.mesh";
        mesh.materialPath = "assets/default.mat";

        ScriptComponent script{};
        script.scriptClassName = "OrbitScript";

        m_world.AddComponent(m_playerEntity, transform);
        m_world.AddComponent(m_playerEntity, mesh);
        m_world.AddComponent(m_playerEntity, script);

        m_scriptEngine.CreateInstance(script.scriptClassName);
        m_resourceManager.LoadAsync<MeshResource>(mesh.meshPath);
    }
}
