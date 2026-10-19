#pragma once
#include "MiniEngine/ProjectSDK/ProjectAPI.h"
#include <thread>
namespace MiniEngine
{
class World; class InputSystem; class Logger;
class ProjectHostBridge
{
public:
    ProjectHostBridge(World &, InputSystem &, Logger &);
    ProjectHostBridge(const ProjectHostBridge &) = delete;
    ProjectHostBridge &operator=(const ProjectHostBridge &) = delete;
    const TinyProjectHostAPI &GetAPI() const noexcept { return m_api; }
    TinyProjectWorld GetWorld() const noexcept { return m_worldId; }
    void SetWorldAccess(bool ready, bool writable) noexcept;
    void InvalidateWorld() noexcept;
private:
    TinyProjectResult Access(bool write = false) const noexcept;
    TinyProjectResult EntityAccess(TinyProjectEntity, bool write = false) const noexcept;
    World &m_world; InputSystem &m_input; Logger &m_logger;
    std::thread::id m_thread;
    TinyProjectWorld m_worldId{};
    bool m_ready = false, m_writable = false;
    TinyProjectHostAPI m_api{};
};
}
