#pragma once
#include "tinyengine/project_sdk/project_gameplay_api.h"
#include "tinyengine/project_sdk/project_api.h"
#include <cstdint>
#include <string>
#include <thread>
#include <vector>
namespace tinyengine
{
class World;
class InputSystem;
class Logger;
class PhysicsWorld;
class UISystem;
class VfxSystem;
class AudioEngine;
class Renderer;
// 项目模块与引擎之间的唯一桥。基表只覆盖 P0 能力，玩法扩展通过 query_interface 暴露，
// 因此两者的 ABI 可以各自演进：新增能力只追加到扩展表尾部。
class ProjectHostBridge
{
  public:
    ProjectHostBridge(World&, InputSystem&, Logger&);
    ProjectHostBridge(const ProjectHostBridge&) = delete;
    ProjectHostBridge& operator=(const ProjectHostBridge&) = delete;
    const TinyProjectHostAPI& GetAPI() const noexcept
    {
        return m_api;
    }
    TinyProjectWorld getWorld() const noexcept
    {
        return m_worldId;
    }
    const TinyProjectGameplayAPI& GetGameplayAPI() const noexcept
    {
        return m_gameplay;
    }
    void setWorldAccess(bool ready, bool writable) noexcept;
    void invalidateWorld() noexcept;
    // 扩展表需要物理、UI、音频、特效与渲染设备；这些服务由 Engine 在构造后注入，
    // 未注入时扩展表仍然可用，但相关调用返回 TINY_PROJECT_NOT_READY。
    void setServices(PhysicsWorld*, UISystem*, VfxSystem*, AudioEngine*, Renderer*) noexcept;
    // 推进一次性特效生命周期；Engine 每帧调用一次。
    void update(float deltaTime);

  private:
    TinyProjectResult access(bool write = false) const noexcept;
    TinyProjectResult entityAccess(TinyProjectEntity, bool write = false) const noexcept;
    void buildGameplayAPI() noexcept;
    void clearSessionResources() noexcept;
    bool spawnBurstEmitter(const TinyProjectVec3& position, const TinyProjectColor& start,
                           const TinyProjectColor& end, float radius, float lifetime, std::uint32_t count);
    struct OneShotEffect
    {
        std::uint64_t emitter = 0;
        float remaining = 0.0f;
    };
    World& m_world;
    InputSystem& m_input;
    Logger& m_logger;
    PhysicsWorld* m_physics = nullptr;
    UISystem* m_ui = nullptr;
    VfxSystem* m_vfx = nullptr;
    AudioEngine* m_audio = nullptr;
    Renderer* m_renderer = nullptr;
    std::thread::id m_thread;
    TinyProjectWorld m_worldId{};
    bool m_ready = false, m_writable = false;
    TinyProjectHostAPI m_api{};
    TinyProjectGameplayAPI m_gameplay{};
    std::vector<OneShotEffect> m_oneShots;
    std::vector<std::string> m_ownedWidgets;
    std::uint64_t m_uiOwnerToken = 0;
    bool m_mouseCaptureChanged = false;
    bool m_retroChanged = false;
    bool m_savedRetroEnabled = false;
    float m_savedRetroBlockSize = 3.0f, m_savedRetroColorLevels = 32.0f;
};
} // namespace tinyengine
