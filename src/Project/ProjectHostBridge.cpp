#include "MiniEngine/Project/ProjectHostBridge.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/Input/InputSystem.hpp"
#include "MiniEngine/Core/Logger.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <malloc.h>

namespace MiniEngine
{
namespace
{
std::atomic<uint64_t> nextWorld{1};
bool Valid(TinyProjectString text) { return text.size <= 1048576 && (text.data || !text.size); }
std::string String(TinyProjectString text) { return text.size ? std::string(text.data, static_cast<size_t>(text.size)) : std::string{}; }
template<class F> TinyProjectResult Guard(F &&f) noexcept { try { return f(); } catch (...) { return TINY_PROJECT_FAILED; } }
bool Finite(TinyProjectVec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
}
ProjectHostBridge::ProjectHostBridge(World &world, InputSystem &input, Logger &logger)
    : m_world(world), m_input(input), m_logger(logger), m_thread(std::this_thread::get_id())
{
    InvalidateWorld();
    m_api.abi_version = TINY_PROJECT_ABI_VERSION; m_api.struct_size = sizeof(m_api); m_api.host_context = this;
    m_api.log = [](void *p, uint32_t level, TinyProjectString text) {
        try { if (Valid(text)) static_cast<ProjectHostBridge *>(p)->m_logger.Log(
            level == TINY_PROJECT_LOG_ERROR ? LogLevel::Error : level == TINY_PROJECT_LOG_WARNING ? LogLevel::Warning : LogLevel::Info, String(text)); } catch (...) {}
    };
    m_api.allocate = [](void *, uint64_t size, uint32_t alignment, void **out) -> TinyProjectResult {
        if (out) *out = nullptr;
        if (!out || !size || size > std::numeric_limits<size_t>::max() || !alignment || (alignment & (alignment - 1))) return TINY_PROJECT_INVALID_ARGUMENT;
#ifdef _WIN32
        *out = _aligned_malloc(static_cast<size_t>(size), std::max<size_t>(alignment, alignof(void *)));
#else
        if (posix_memalign(out, std::max<size_t>(alignment, alignof(void *)), static_cast<size_t>(size)) != 0) *out = nullptr;
#endif
        return *out ? TINY_PROJECT_OK : TINY_PROJECT_FAILED;
    };
    m_api.deallocate = [](void *, void *memory, uint32_t) {
#ifdef _WIN32
        _aligned_free(memory);
#else
        std::free(memory);
#endif
    };
    m_api.find_entity = [](void *p, TinyProjectWorld worldId, TinyProjectString name, TinyProjectEntity *out) -> TinyProjectResult {
        if (out) *out = {};
        if (!out || !Valid(name)) return TINY_PROJECT_INVALID_ARGUMENT;
        return Guard([&]() -> TinyProjectResult {
            auto &b = *static_cast<ProjectHostBridge *>(p); auto result = b.Access();
            if (result != TINY_PROJECT_OK) return result;
            if (worldId.id != b.m_worldId.id) return TINY_PROJECT_NOT_FOUND;
            Entity first = InvalidEntity; const auto wanted = String(name);
            for (auto e : b.m_world.GetEntityManager().GetAliveEntities())
                if (const auto *n = b.m_world.GetComponent<NameComponent>(e); n && n->name == wanted && (first == InvalidEntity || e < first)) first = e;
            if (first == InvalidEntity) return TINY_PROJECT_NOT_FOUND;
            *out = {worldId.id, first}; return TINY_PROJECT_OK;
        });
    };
    m_api.is_alive = [](void *p, TinyProjectEntity e, uint32_t *out) -> TinyProjectResult {
        if (out) *out = 0;
        if (!out) return TINY_PROJECT_INVALID_ARGUMENT;
        return Guard([&]() { auto result = static_cast<ProjectHostBridge *>(p)->EntityAccess(e); if (result == TINY_PROJECT_OK) *out = 1; return result; });
    };
    m_api.get_transform = [](void *p, TinyProjectEntity e, TinyProjectTransform *out) -> TinyProjectResult {
        if (out) *out = {};
        if (!out) return TINY_PROJECT_INVALID_ARGUMENT;
        return Guard([&]() -> TinyProjectResult {
            auto &b = *static_cast<ProjectHostBridge *>(p); auto result = b.EntityAccess(e); if (result != TINY_PROJECT_OK) return result;
            const auto *v = b.m_world.GetComponent<TransformComponent>(e.value); if (!v) return TINY_PROJECT_NOT_FOUND;
            *out = {{v->position.x,v->position.y,v->position.z},{v->rotation.x,v->rotation.y,v->rotation.z},{v->scale.x,v->scale.y,v->scale.z}};
            return TINY_PROJECT_OK;
        });
    };
    m_api.set_transform = [](void *p, TinyProjectEntity e, const TinyProjectTransform *v) -> TinyProjectResult {
        if (!v || !Finite(v->position) || !Finite(v->rotation) || !Finite(v->scale)) return TINY_PROJECT_INVALID_ARGUMENT;
        return Guard([&]() -> TinyProjectResult {
            auto &b = *static_cast<ProjectHostBridge *>(p); auto result = b.EntityAccess(e, true); if (result != TINY_PROJECT_OK) return result;
            auto *target = b.m_world.GetComponent<TransformComponent>(e.value); if (!target) return TINY_PROJECT_NOT_FOUND;
            target->position = {v->position.x,v->position.y,v->position.z}; target->rotation = {v->rotation.x,v->rotation.y,v->rotation.z};
            target->scale = {v->scale.x,v->scale.y,v->scale.z}; return TINY_PROJECT_OK;
        });
    };
    m_api.get_axis = [](void *p, TinyProjectString name, float *out) -> TinyProjectResult {
        if (out) *out = 0;
        if (!out || !Valid(name)) return TINY_PROJECT_INVALID_ARGUMENT;
        return Guard([&]() { auto &b = *static_cast<ProjectHostBridge *>(p); auto result = b.Access(); if (result == TINY_PROJECT_OK) *out = b.m_input.GetAxis(String(name)); return result; });
    };
    m_api.is_down = [](void *p, TinyProjectString name, uint32_t *out) -> TinyProjectResult {
        if (out) *out = 0;
        if (!out || !Valid(name)) return TINY_PROJECT_INVALID_ARGUMENT;
        return Guard([&]() { auto &b = *static_cast<ProjectHostBridge *>(p); auto result = b.Access(); if (result == TINY_PROJECT_OK) *out = b.m_input.IsDown(String(name)) ? 1u : 0u; return result; });
    };
    m_api.query_interface = [](void *, TinyProjectString id, uint32_t, const void **out) -> TinyProjectResult {
        if (out) *out = nullptr;
        return out && Valid(id) ? TINY_PROJECT_UNSUPPORTED : TINY_PROJECT_INVALID_ARGUMENT;
    };
}
TinyProjectResult ProjectHostBridge::Access(bool write) const noexcept
{
    if (std::this_thread::get_id() != m_thread) return TINY_PROJECT_WRONG_THREAD;
    if (!m_ready) return TINY_PROJECT_NOT_READY;
    return write && !m_writable ? TINY_PROJECT_READ_ONLY : TINY_PROJECT_OK;
}
TinyProjectResult ProjectHostBridge::EntityAccess(TinyProjectEntity e, bool write) const noexcept
{
    const auto result = Access(write); if (result != TINY_PROJECT_OK) return result;
    return e.world_id == m_worldId.id && e.value && m_world.IsAlive(e.value) ? TINY_PROJECT_OK : TINY_PROJECT_NOT_FOUND;
}
void ProjectHostBridge::SetWorldAccess(bool ready, bool writable) noexcept { m_ready = ready; m_writable = ready && writable; }
void ProjectHostBridge::InvalidateWorld() noexcept { m_worldId.id = nextWorld.fetch_add(1); }
}
