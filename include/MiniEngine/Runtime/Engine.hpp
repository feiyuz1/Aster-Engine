#pragma once

#include "MiniEngine/Audio/AudioEngine.hpp"
#include "MiniEngine/Core/Logger.hpp"
#include "MiniEngine/Core/Module.hpp"
#include "MiniEngine/Diagnostics/Diagnostics.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/Gameplay/Gameplay.hpp"
#include "MiniEngine/Input/InputSystem.hpp"
#include "MiniEngine/Memory/Allocators.hpp"
#include "MiniEngine/Navigation/Navigation.hpp"
#include "MiniEngine/Physics/PhysicsWorld.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/Render/Renderer.hpp"
#include "MiniEngine/Resource/AssetStreaming.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"
#include "MiniEngine/Script/ScriptEngine.hpp"
#include "MiniEngine/Task/JobSystem.hpp"
#include "MiniEngine/UI/UISystem.hpp"
#include "MiniEngine/VFX/ParticleSystem.hpp"
#include "MiniEngine/World/WorldPartition.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace MiniEngine
{
struct EngineContext
{
    Logger *logger = nullptr;
    MemoryManager *memoryManager = nullptr;
    JobSystem *jobSystem = nullptr;
    ResourceManager *resourceManager = nullptr;
    World *world = nullptr;
    Renderer *renderer = nullptr;
    InputSystem *inputSystem = nullptr;
    PhysicsWorld *physicsWorld = nullptr;
    AudioEngine *audioEngine = nullptr;
    ScriptEngine *scriptEngine = nullptr;
    SceneManager *sceneManager = nullptr;
    ProjectSettings *project = nullptr;
    UISystem *uiSystem = nullptr;
    AssetStreamingManager *assetStreaming = nullptr;
    GameplayFramework *gameplay = nullptr;
    DiagnosticsService *diagnostics = nullptr;
    VfxSystem *vfxSystem = nullptr;
    NavigationGrid *navigation = nullptr;
    WorldPartition *worldPartition = nullptr;
    ModuleManager *moduleManager = nullptr;
};

class Engine : private WorldListener
{
  public:
    bool Initialize(const std::filesystem::path &projectFile = {}, bool startInPlayMode = true);
    void Run(std::uint32_t maxFrames = 0);
    void Shutdown();
    const EngineContext &GetContext() const;
    void SetPaused(bool paused);
    bool IsPaused() const;
    void SetTimeScale(float scale);
    bool LoadScene(const std::filesystem::path &path);
    bool SaveScene(const std::filesystem::path &path = {});
    bool EnterPlayMode();
    bool ExitPlayMode();
    bool IsPlaying() const;
    SceneManager &GetSceneManager();
    ProjectSettings &GetProjectSettings();
    void SetGameInstance(std::unique_ptr<GameInstance> gameInstance);
    ModuleManager &GetModuleManager();

  private:
    void Tick(float deltaTime);
    void SetupDemoScene();
    void OnEntityDestroying(World &, Entity entity) override;
    void OnWorldCleared(World &) override;

    Logger m_logger;
    MemoryManager m_memoryManager;
    JobSystem m_jobSystem;
    ResourceManager m_resourceManager;
    AssetStreamingManager m_assetStreaming;
    World m_world;
    Renderer m_renderer;
    InputSystem m_inputSystem;
    PhysicsWorld m_physicsWorld;
    AudioEngine m_audioEngine;
    ScriptEngine m_scriptEngine;
    UISystem m_uiSystem;
    GameplayFramework m_gameplay;
    DiagnosticsService m_diagnostics;
    VfxSystem m_vfxSystem;
    NavigationGrid m_navigation;
    WorldPartition m_worldPartition;
    ModuleManager m_moduleManager;
    ProjectSettings m_projectSettings;
    SceneManager m_sceneManager{&m_world};
    std::unique_ptr<GameInstance> m_gameInstance;
    EngineClock m_clock;
    EngineContext m_context;
    bool m_running = false;
    bool m_initialized = false;
    bool m_paused = false;
    float m_timeScale = 1.0f;
    float m_fixedAccumulator = 0.0f;
    std::uint32_t m_frameIndex = 0;
    Entity m_playerEntity = InvalidEntity;
    bool m_playing = false;
    bool m_projectPakMounted = false;
    std::string m_editSnapshot;
};
} // namespace MiniEngine
