// 文件：include/MiniEngine/Runtime/Engine.hpp
// 作用：组装全部运行时子系统并驱动初始化、主循环、游戏态切换和关闭。
// 约定：子系统初始化与关闭顺序严格相反，避免悬空服务指针。
// 类型：公开声明。

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
#include "MiniEngine/Plugin/PluginManager.hpp"
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
// 各子系统的非拥有型服务视图。Engine 负责这些对象的实际生命周期，外部代码不得缓存到 Engine::Shutdown 之后。
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
    PluginManager *pluginManager = nullptr;
};

// 引擎总控对象。它按照依赖顺序启动模块，以固定步长更新物理，并在每帧末提交渲染与诊断数据。
// 一个 Engine 实例只对应一个活动 World；编辑器的 Play 操作通过场景快照隔离运行态修改。
class Engine : private WorldListener
{
  public:
    /// @brief 初始化对象及其依赖资源。
    /// @param projectFile TinyEngine 项目文件路径。
    /// @param startInPlayMode 目标状态、模式或类型。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Initialize(const std::filesystem::path &projectFile = {}, bool startInPlayMode = true);
    /// @brief 运行主循环或任务流程。
    /// @param maxFrames 目标尺寸、数量、索引或限制值。
    void Run(std::uint32_t maxFrames = 0);
    /// @brief 停止服务并释放对象持有的资源。
    void Shutdown();
    /// @brief 获取 `Context`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const EngineContext &GetContext() const;
    /// @brief 设置 `Paused`。
    /// @param paused 是否启用对应行为。
    void SetPaused(bool paused);
    /// @brief 判断是否满足 `Paused` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsPaused() const;
    /// @brief 设置 `TimeScale`。
    /// @param scale 缩放比例或缩放向量。
    void SetTimeScale(float scale);
    /// @brief 加载 `Scene`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool LoadScene(const std::filesystem::path &path);
    /// @brief 保存 `Scene`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SaveScene(const std::filesystem::path &path = {});
    /// @brief 进入 `PlayMode`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool EnterPlayMode();
    /// @brief 退出 `PlayMode`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ExitPlayMode();
    /// @brief 判断是否满足 `Playing` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsPlaying() const;
    /// @brief 获取 `SceneManager`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    SceneManager &GetSceneManager();
    /// @brief 获取 `ProjectSettings`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    ProjectSettings &GetProjectSettings();
    /// @brief 设置 `GameInstance`。
    /// @param gameInstance 接口所需的输入参数。
    void SetGameInstance(std::unique_ptr<GameInstance> gameInstance);
    /// @brief 获取 `ModuleManager`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    ModuleManager &GetModuleManager();
    /// @brief 获取 `PluginManager`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    PluginManager &GetPluginManager();
    /// @brief 请求退出主循环；下一帧检查点结束 Run。
    void RequestExit();

  private:
    // Tick 只处理一帧编排；帧率相关更新和固定步长更新在此处明确分流。
    /// @brief 推进一次按帧更新。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void Tick(float deltaTime);
    /// @brief 设置 `upDemoScene`。
    void SetupDemoScene();
    /// @brief 响应 `EntityDestroying`。
    /// @param entity 目标实体句柄。
    void OnEntityDestroying(World &, Entity entity) override;
    /// @brief 响应 `WorldCleared`。
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
    PluginManager m_pluginManager;
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
} // 结束 MiniEngine 命名空间
