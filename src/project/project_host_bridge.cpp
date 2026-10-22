#include "tinyengine/project/project_host_bridge.h"
#include "tinyengine/core/logger.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/input/input_system.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <malloc.h>

namespace tinyengine
{
namespace
{
std::atomic<uint64_t> nextWorld{1};
bool valid(TinyProjectString text)
{
    return text.size <= 1048576 && (text.data || !text.size);
}
std::string String(TinyProjectString text)
{
    return text.size ? std::string(text.data, static_cast<size_t>(text.size)) : std::string{};
}
template <class F> TinyProjectResult guard(F&& f) noexcept
{
    try
    {
        return f();
    }
    catch (...)
    {
        return TINY_PROJECT_FAILED;
    }
}
bool finite(TinyProjectVec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace
ProjectHostBridge::ProjectHostBridge(World& world, InputSystem& input, Logger& logger)
    : m_world(world), m_input(input), m_logger(logger), m_thread(std::this_thread::get_id())
{
    invalidateWorld();
    m_api.abi_version = TINY_PROJECT_ABI_VERSION;
    m_api.struct_size = sizeof(m_api);
    m_api.host_context = this;
    m_api.log = [](void* p, uint32_t level, TinyProjectString text)
    {
        try
        {
            if (valid(text))
                static_cast<ProjectHostBridge*>(p)->m_logger.log(level == TINY_PROJECT_LOG_ERROR     ? LogLevel::Error
                                                                 : level == TINY_PROJECT_LOG_WARNING ? LogLevel::Warning
                                                                                                     : LogLevel::Info,
                                                                 String(text));
        }
        catch (...)
        {
        }
    };
    m_api.allocate = [](void*, uint64_t size, uint32_t alignment, void** out) -> TinyProjectResult
    {
        if (out)
            *out = nullptr;
        if (!out || !size || size > std::numeric_limits<size_t>::max() || !alignment || (alignment & (alignment - 1)))
            return TINY_PROJECT_INVALID_ARGUMENT;
#ifdef _WIN32
        *out = _aligned_malloc(static_cast<size_t>(size), std::max<size_t>(alignment, alignof(void*)));
#else
        if (posix_memalign(out, std::max<size_t>(alignment, alignof(void*)), static_cast<size_t>(size)) != 0)
            *out = nullptr;
#endif
        return *out ? TINY_PROJECT_OK : TINY_PROJECT_FAILED;
    };
    m_api.deallocate = [](void*, void* memory, uint32_t)
    {
#ifdef _WIN32
        _aligned_free(memory);
#else
        std::free(memory);
#endif
    };
    m_api.find_entity = [](void* p, TinyProjectWorld worldId, TinyProjectString name,
                           TinyProjectEntity* out) -> TinyProjectResult
    {
        if (out)
            *out = {};
        if (!out || !valid(name))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access();
                if (result != TINY_PROJECT_OK)
                    return result;
                if (worldId.id != b.m_worldId.id)
                    return TINY_PROJECT_NOT_FOUND;
                Entity first = kInvalidEntity;
                const auto wanted = String(name);
                for (auto e : b.m_world.GetEntityManager().GetAliveEntities())
                    if (const auto* n = b.m_world.GetComponent<NameComponent>(e);
                        n && n->name == wanted && (first == kInvalidEntity || e < first))
                        first = e;
                if (first == kInvalidEntity)
                    return TINY_PROJECT_NOT_FOUND;
                *out = {worldId.id, first};
                return TINY_PROJECT_OK;
            });
    };
    m_api.is_alive = [](void* p, TinyProjectEntity e, uint32_t* out) -> TinyProjectResult
    {
        if (out)
            *out = 0;
        if (!out)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]()
            {
                auto result = static_cast<ProjectHostBridge*>(p)->entityAccess(e);
                if (result == TINY_PROJECT_OK)
                    *out = 1;
                return result;
            });
    };
    m_api.get_transform = [](void* p, TinyProjectEntity e, TinyProjectTransform* out) -> TinyProjectResult
    {
        if (out)
            *out = {};
        if (!out)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.entityAccess(e);
                if (result != TINY_PROJECT_OK)
                    return result;
                const auto* v = b.m_world.GetComponent<TransformComponent>(e.value);
                if (!v)
                    return TINY_PROJECT_NOT_FOUND;
                *out = {{v->position.x, v->position.y, v->position.z},
                        {v->rotation.x, v->rotation.y, v->rotation.z},
                        {v->scale.x, v->scale.y, v->scale.z}};
                return TINY_PROJECT_OK;
            });
    };
    m_api.set_transform = [](void* p, TinyProjectEntity e, const TinyProjectTransform* v) -> TinyProjectResult
    {
        if (!v || !finite(v->position) || !finite(v->rotation) || !finite(v->scale))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.entityAccess(e, true);
                if (result != TINY_PROJECT_OK)
                    return result;
                auto* target = b.m_world.GetComponent<TransformComponent>(e.value);
                if (!target)
                    return TINY_PROJECT_NOT_FOUND;
                target->position = {v->position.x, v->position.y, v->position.z};
                target->rotation = {v->rotation.x, v->rotation.y, v->rotation.z};
                target->scale = {v->scale.x, v->scale.y, v->scale.z};
                return TINY_PROJECT_OK;
            });
    };
    m_api.get_axis = [](void* p, TinyProjectString name, float* out) -> TinyProjectResult
    {
        if (out)
            *out = 0;
        if (!out || !valid(name))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]()
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access();
                if (result == TINY_PROJECT_OK)
                    *out = b.m_input.getAxis(String(name));
                return result;
            });
    };
    m_api.is_down = [](void* p, TinyProjectString name, uint32_t* out) -> TinyProjectResult
    {
        if (out)
            *out = 0;
        if (!out || !valid(name))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]()
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access();
                if (result == TINY_PROJECT_OK)
                    *out = b.m_input.isDown(String(name)) ? 1u : 0u;
                return result;
            });
    };
    m_api.query_interface = [](void*, TinyProjectString id, uint32_t, const void** out) -> TinyProjectResult
    {
        if (out)
            *out = nullptr;
        return out && valid(id) ? TINY_PROJECT_UNSUPPORTED : TINY_PROJECT_INVALID_ARGUMENT;
    };
}
TinyProjectResult ProjectHostBridge::access(bool write) const noexcept
{
    if (std::this_thread::get_id() != m_thread)
        return TINY_PROJECT_WRONG_THREAD;
    if (!m_ready)
        return TINY_PROJECT_NOT_READY;
    return write && !m_writable ? TINY_PROJECT_READ_ONLY : TINY_PROJECT_OK;
}
TinyProjectResult ProjectHostBridge::entityAccess(TinyProjectEntity e, bool write) const noexcept
{
    const auto result = access(write);
    if (result != TINY_PROJECT_OK)
        return result;
    return e.world_id == m_worldId.id && e.value && m_world.isAlive(e.value) ? TINY_PROJECT_OK : TINY_PROJECT_NOT_FOUND;
}
void ProjectHostBridge::setWorldAccess(bool ready, bool writable) noexcept
{
    m_ready = ready;
    m_writable = ready && writable;
}
void ProjectHostBridge::invalidateWorld() noexcept
{
    m_worldId.id = nextWorld.fetch_add(1);
}
} // namespace tinyengine
