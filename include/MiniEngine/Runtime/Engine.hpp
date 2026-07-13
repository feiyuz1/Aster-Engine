#pragma once

#include "MiniEngine/Audio/AudioEngine.hpp"
#include "MiniEngine/Core/Logger.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/Memory/Allocators.hpp"
#include "MiniEngine/Input/InputSystem.hpp"
#include "MiniEngine/Physics/PhysicsWorld.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Render/Renderer.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"
#include "MiniEngine/Script/ScriptEngine.hpp"
#include "MiniEngine/Task/JobSystem.hpp"

#include <cstdint>

namespace MiniEngine
{
    struct EngineContext
    {
        Logger* logger = nullptr;
        MemoryManager* memoryManager = nullptr;
        JobSystem* jobSystem = nullptr;
        ResourceManager* resourceManager = nullptr;
        World* world = nullptr;
        Renderer* renderer = nullptr;
        InputSystem* inputSystem = nullptr;
        PhysicsWorld* physicsWorld = nullptr;
        AudioEngine* audioEngine = nullptr;
        ScriptEngine* scriptEngine = nullptr;
    };

    class Engine
    {
    public:
        bool Initialize();
        void Run(std::uint32_t maxFrames = 0);
        void Shutdown();
        const EngineContext& GetContext() const;
        void SetPaused(bool paused);
        bool IsPaused() const;
        void SetTimeScale(float scale);
    private:
        void Tick(float deltaTime);
        void SetupDemoScene();

        Logger m_logger;
        MemoryManager m_memoryManager;
        JobSystem m_jobSystem;
        ResourceManager m_resourceManager;
        World m_world;
        Renderer m_renderer;
        InputSystem m_inputSystem;
        PhysicsWorld m_physicsWorld;
        AudioEngine m_audioEngine;
        ScriptEngine m_scriptEngine;
        EngineClock m_clock;
        EngineContext m_context;
        bool m_running = false;
        bool m_initialized = false;
        bool m_paused = false;
        float m_timeScale = 1.0f;
        float m_fixedAccumulator = 0.0f;
        std::uint32_t m_frameIndex = 0;
        Entity m_playerEntity = InvalidEntity;
    };
}
