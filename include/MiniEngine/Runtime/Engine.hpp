#pragma once

#include "MiniEngine/Core/Logger.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/Memory/Allocators.hpp"
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
        ScriptEngine* scriptEngine = nullptr;
    };

    class Engine
    {
    public:
        bool Initialize();
        void Run(std::uint32_t maxFrames = 0);
        void Shutdown();

        const EngineContext& GetContext() const;

    private:
        void Tick(float deltaTime);
        void SetupDemoScene();

    private:
        Logger m_logger;
        MemoryManager m_memoryManager;
        JobSystem m_jobSystem;
        ResourceManager m_resourceManager;
        World m_world;
        Renderer m_renderer;
        ScriptEngine m_scriptEngine;
        EngineContext m_context;
        bool m_running = false;
        std::uint32_t m_frameIndex = 0;
        Entity m_playerEntity = InvalidEntity;
    };
}
