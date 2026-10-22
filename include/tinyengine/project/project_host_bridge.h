#pragma once
#include "tinyengine/project_sdk/project_api.h"
#include <thread>
namespace tinyengine
{
class World;
class InputSystem;
class Logger;
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
    void setWorldAccess(bool ready, bool writable) noexcept;
    void invalidateWorld() noexcept;

  private:
    TinyProjectResult access(bool write = false) const noexcept;
    TinyProjectResult entityAccess(TinyProjectEntity, bool write = false) const noexcept;
    World& m_world;
    InputSystem& m_input;
    Logger& m_logger;
    std::thread::id m_thread;
    TinyProjectWorld m_worldId{};
    bool m_ready = false, m_writable = false;
    TinyProjectHostAPI m_api{};
};
} // namespace tinyengine
