#include "tinyengine/project/project_host_bridge.h"
#include "tinyengine/audio/audio_engine.h"
#include "tinyengine/core/logger.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/input/input_system.h"
#include "tinyengine/physics/physics_world.h"
#include "tinyengine/render/renderer.h"
#include "tinyengine/ui/ui_system.h"
#include "tinyengine/vfx/particle_system.h"
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
std::atomic<uint64_t> nextUiOwner{1};
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
bool finite(const TinyProjectUIRect& r)
{
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height);
}
bool finite(const TinyProjectColor& c)
{
    return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b) && std::isfinite(c.a);
}
Vec3 vec3(TinyProjectVec3 v)
{
    return {v.x, v.y, v.z};
}
Vec4 vec4(const TinyProjectColor& c)
{
    return {c.r, c.g, c.b, c.a};
}
UIRect rect(const TinyProjectUIRect& r)
{
    return {r.x, r.y, r.width, r.height};
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
    m_api.query_interface = [](void* p, TinyProjectString id, const uint32_t requested,
                               const void** out) -> TinyProjectResult
    {
        if (out)
            *out = nullptr;
        if (!out || !valid(id))
            return TINY_PROJECT_INVALID_ARGUMENT;
        auto& b = *static_cast<ProjectHostBridge*>(p);
        if (String(id) != TINY_PROJECT_GAMEPLAY_INTERFACE)
            return TINY_PROJECT_UNSUPPORTED;
        // 版本只允许向下兼容的请求；未知的更高版本由模块自己决定是否降级。
        if (!requested || requested > TINY_PROJECT_GAMEPLAY_VERSION)
            return TINY_PROJECT_INCOMPATIBLE;
        *out = &b.m_gameplay;
        return TINY_PROJECT_OK;
    };
    buildGameplayAPI();
}
void ProjectHostBridge::setServices(PhysicsWorld* physics, UISystem* ui, VfxSystem* vfx, AudioEngine* audio,
                                    Renderer* renderer) noexcept
{
    m_physics = physics;
    m_ui = ui;
    m_vfx = vfx;
    m_audio = audio;
    m_renderer = renderer;
}
void ProjectHostBridge::buildGameplayAPI() noexcept
{
    m_gameplay.abi_version = TINY_PROJECT_GAMEPLAY_VERSION;
    m_gameplay.struct_size = sizeof(m_gameplay);
    m_gameplay.host_context = this;
    m_gameplay.create_entity = [](void* p, TinyProjectString name, const TinyProjectTransform* transform,
                                  TinyProjectEntity* out) -> TinyProjectResult
    {
        if (out)
            *out = {};
        if (!out || !valid(name))
            return TINY_PROJECT_INVALID_ARGUMENT;
        if (transform && (!finite(transform->position) || !finite(transform->rotation) || !finite(transform->scale)))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                const Entity entity = b.m_world.createEntity(String(name));
                if (entity == kInvalidEntity)
                    return TINY_PROJECT_FAILED;
                if (transform)
                {
                    if (auto* target = b.m_world.GetComponent<TransformComponent>(entity))
                    {
                        target->position = vec3(transform->position);
                        target->rotation = vec3(transform->rotation);
                        target->scale = vec3(transform->scale);
                    }
                }
                *out = {b.m_worldId.id, entity};
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.destroy_entity = [](void* p, TinyProjectEntity e) -> TinyProjectResult
    {
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.entityAccess(e, true);
                if (result != TINY_PROJECT_OK)
                    return result;
                b.m_world.destroyEntity(e.value);
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.set_mesh = [](void* p, TinyProjectEntity e, TinyProjectString mesh,
                             TinyProjectString material) -> TinyProjectResult
    {
        if (!valid(mesh) || !valid(material))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.entityAccess(e, true);
                if (result != TINY_PROJECT_OK)
                    return result;
                b.m_world.AddComponent(e.value, MeshComponent{String(mesh), String(material)});
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.set_box_collider = [](void* p, TinyProjectEntity e, TinyProjectVec3 halfExtents,
                                     uint32_t trigger) -> TinyProjectResult
    {
        if (!finite(halfExtents) || halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.entityAccess(e, true);
                if (result != TINY_PROJECT_OK)
                    return result;
                ColliderComponent collider;
                collider.shape = ColliderShape::Box;
                collider.halfExtents = vec3(halfExtents);
                collider.trigger = trigger != 0;
                b.m_world.AddComponent(e.value, collider);
                if (!collider.trigger && !b.m_world.HasComponent<RigidBodyComponent>(e.value))
                    b.m_world.AddComponent(e.value, RigidBodyComponent{BodyType::Static, 0.0f, {}, false});
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.set_light = [](void* p, TinyProjectEntity e, uint32_t type, TinyProjectVec3 color, float intensity,
                              TinyProjectVec3 direction, float range) -> TinyProjectResult
    {
        if (!finite(color) || !finite(direction) || !std::isfinite(intensity) || !std::isfinite(range) || type > 2)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.entityAccess(e, true);
                if (result != TINY_PROJECT_OK)
                    return result;
                LightComponent light;
                light.type = static_cast<LightType>(type);
                light.color = vec3(color);
                light.intensity = intensity;
                light.direction = vec3(direction);
                light.range = range;
                b.m_world.AddComponent(e.value, light);
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.raycast = [](void* p, TinyProjectVec3 origin, TinyProjectVec3 direction, float maxDistance,
                            TinyProjectEntity ignored, TinyProjectHit* out) -> TinyProjectResult
    {
        if (out)
            *out = {};
        if (!out || !finite(origin) || !finite(direction) || !std::isfinite(maxDistance) || maxDistance <= 0.0f)
            return TINY_PROJECT_INVALID_ARGUMENT;
        const Vec3 dir = vec3(direction);
        if (length(dir) < 0.000001f)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access();
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_physics)
                    return TINY_PROJECT_NOT_READY;
                PhysicsQuery query;
                query.ignoredEntity = ignored.world_id == b.m_worldId.id ? ignored.value : kInvalidEntity;
                RaycastHit hit;
                if (!b.m_physics->raycast(b.m_world, {vec3(origin), normalize(dir)}, maxDistance, hit, query))
                {
                    out->has_hit = 0;
                    return TINY_PROJECT_NOT_FOUND;
                }
                out->has_hit = 1;
                out->entity = {b.m_worldId.id, hit.entity};
                out->point = {hit.point.x, hit.point.y, hit.point.z};
                out->normal = {hit.normal.x, hit.normal.y, hit.normal.z};
                out->distance = hit.distance;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.move_character = [](void* p, TinyProjectEntity e, TinyProjectVec3 displacement,
                                   TinyProjectCharacterMove* out) -> TinyProjectResult
    {
        if (out)
            *out = {};
        if (!out || !finite(displacement))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.entityAccess(e, true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_physics)
                    return TINY_PROJECT_NOT_READY;
                const CharacterMoveResult moved =
                    b.m_physics->moveCharacterController(b.m_world, e.value, vec3(displacement), 0.35f, 0.02f, 50.0f);
                out->applied = {moved.appliedDisplacement.x, moved.appliedDisplacement.y,
                                moved.appliedDisplacement.z};
                out->ground_normal = {moved.groundNormal.x, moved.groundNormal.y, moved.groundNormal.z};
                out->grounded = moved.grounded ? 1u : 0u;
                out->hit_ceiling = moved.hitCeiling ? 1u : 0u;
                out->stepped = moved.stepped ? 1u : 0u;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.key_down = [](void* p, uint32_t scancode, uint32_t* out) -> TinyProjectResult
    {
        if (out)
            *out = 0;
        if (!out || scancode >= InputState::kMaxScancodes)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access();
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_renderer)
                    return TINY_PROJECT_NOT_READY;
                *out = b.m_renderer->GetInputState().keysDown[scancode] ? 1u : 0u;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.key_pressed = [](void* p, uint32_t scancode, uint32_t* out) -> TinyProjectResult
    {
        if (out)
            *out = 0;
        if (!out || scancode >= InputState::kMaxScancodes)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access();
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_renderer)
                    return TINY_PROJECT_NOT_READY;
                *out = b.m_renderer->GetInputState().keysPressed[scancode] ? 1u : 0u;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.primary_down = [](void* p, uint32_t* out) -> TinyProjectResult
    {
        if (out)
            *out = 0;
        if (!out)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access();
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_renderer)
                    return TINY_PROJECT_NOT_READY;
                *out = b.m_renderer->GetInputState().primaryAction ? 1u : 0u;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.primary_pressed = [](void* p, uint32_t* out) -> TinyProjectResult
    {
        if (out)
            *out = 0;
        if (!out)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access();
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_renderer)
                    return TINY_PROJECT_NOT_READY;
                *out = b.m_renderer->GetInputState().primaryPressed ? 1u : 0u;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.cursor_position = [](void* p, float* x, float* y) -> TinyProjectResult
    {
        if (x)
            *x = 0;
        if (y)
            *y = 0;
        if (!x || !y)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access();
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_renderer)
                    return TINY_PROJECT_NOT_READY;
                const auto& input = b.m_renderer->GetInputState();
                *x = input.cursorX;
                *y = input.cursorY;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.viewport_size = [](void* p, float* width, float* height) -> TinyProjectResult
    {
        if (width)
            *width = 0;
        if (height)
            *height = 0;
        if (!width || !height)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access();
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_renderer || !b.m_renderer->getDevice())
                    return TINY_PROJECT_NOT_READY;
                const Vec2 viewport = b.m_renderer->getDevice()->getViewportSize();
                *width = viewport.x;
                *height = viewport.y;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.set_mouse_capture = [](void* p, uint32_t captured) -> TinyProjectResult
    {
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_renderer)
                    return TINY_PROJECT_NOT_READY;
                b.m_mouseCaptureChanged = true;
                b.m_renderer->setMouseCapture(captured != 0);
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.ui_label = [](void* p, TinyProjectString id, TinyProjectString text, TinyProjectUIRect r,
                             TinyProjectColor color, float fontSize) -> TinyProjectResult
    {
        if (!valid(id) || !valid(text) || !finite(r) || !finite(color) || !std::isfinite(fontSize))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_ui)
                    return TINY_PROJECT_NOT_READY;
                const std::string key = String(id);
                UIWidget* widget = b.m_ui->Find(key);
                if (!widget)
                {
                    b.m_ownedWidgets.push_back(key);
                    widget = &b.m_ui->AddLabel(key, String(text), rect(r));
                    widget->ownerToken = b.m_uiOwnerToken;
                }
                if (!widget)
                    return TINY_PROJECT_FAILED;
                widget->text = String(text);
                widget->rect = rect(r);
                widget->color = vec4(color);
                widget->fontSize = fontSize;
                widget->visible = true;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.ui_panel = [](void* p, TinyProjectString id, TinyProjectUIRect r,
                             TinyProjectColor color) -> TinyProjectResult
    {
        if (!valid(id) || !finite(r) || !finite(color))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_ui)
                    return TINY_PROJECT_NOT_READY;
                const std::string key = String(id);
                UIWidget* widget = b.m_ui->Find(key);
                if (!widget)
                {
                    b.m_ownedWidgets.push_back(key);
                    widget = &b.m_ui->AddPanel(key, rect(r), vec4(color));
                    widget->ownerToken = b.m_uiOwnerToken;
                }
                if (!widget)
                    return TINY_PROJECT_FAILED;
                widget->rect = rect(r);
                widget->color = vec4(color);
                widget->visible = true;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.ui_set_text = [](void* p, TinyProjectString id, TinyProjectString text) -> TinyProjectResult
    {
        if (!valid(id) || !valid(text))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_ui)
                    return TINY_PROJECT_NOT_READY;
                UIWidget* widget = b.m_ui->Find(String(id));
                if (!widget)
                    return TINY_PROJECT_NOT_FOUND;
                widget->text = String(text);
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.ui_set_rect = [](void* p, TinyProjectString id, TinyProjectUIRect r) -> TinyProjectResult
    {
        if (!valid(id) || !finite(r))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_ui)
                    return TINY_PROJECT_NOT_READY;
                UIWidget* widget = b.m_ui->Find(String(id));
                if (!widget)
                    return TINY_PROJECT_NOT_FOUND;
                widget->rect = rect(r);
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.ui_set_color = [](void* p, TinyProjectString id, TinyProjectColor color) -> TinyProjectResult
    {
        if (!valid(id) || !finite(color))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_ui)
                    return TINY_PROJECT_NOT_READY;
                UIWidget* widget = b.m_ui->Find(String(id));
                if (!widget)
                    return TINY_PROJECT_NOT_FOUND;
                widget->color = vec4(color);
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.ui_set_visible = [](void* p, TinyProjectString id, uint32_t visible) -> TinyProjectResult
    {
        if (!valid(id))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_ui)
                    return TINY_PROJECT_NOT_READY;
                UIWidget* widget = b.m_ui->Find(String(id));
                if (!widget)
                    return TINY_PROJECT_NOT_FOUND;
                widget->visible = visible != 0;
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.ui_remove = [](void* p, TinyProjectString id) -> TinyProjectResult
    {
        if (!valid(id))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_ui)
                    return TINY_PROJECT_NOT_READY;
                const std::string key = String(id);
                b.m_ui->Remove(key);
                b.m_ownedWidgets.erase(std::remove(b.m_ownedWidgets.begin(), b.m_ownedWidgets.end(), key),
                                       b.m_ownedWidgets.end());
                return TINY_PROJECT_OK;
            });
    };
    m_gameplay.play_sound = [](void* p, TinyProjectString path, float volume) -> TinyProjectResult
    {
        if (!valid(path) || !std::isfinite(volume))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_audio)
                    return TINY_PROJECT_NOT_READY;
                return b.m_audio->playOneShot(String(path), std::clamp(volume, 0.0f, 1.0f)) ? TINY_PROJECT_OK
                                                                                          : TINY_PROJECT_FAILED;
            });
    };
    m_gameplay.spawn_burst = [](void* p, TinyProjectVec3 position, TinyProjectColor start, TinyProjectColor end,
                                float radius, float lifetime, uint32_t count) -> TinyProjectResult
    {
        if (!finite(position) || !finite(start) || !finite(end) || !std::isfinite(radius) ||
            !std::isfinite(lifetime) || radius <= 0.0f || lifetime <= 0.0f || !count)
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_vfx)
                    return TINY_PROJECT_NOT_READY;
                return b.spawnBurstEmitter(position, start, end, radius, lifetime, count) ? TINY_PROJECT_OK
                                                                                         : TINY_PROJECT_FAILED;
            });
    };
    m_gameplay.set_retro_post = [](void* p, uint32_t enabled, float blockSize,
                                   float colorLevels) -> TinyProjectResult
    {
        if (!std::isfinite(blockSize) || !std::isfinite(colorLevels))
            return TINY_PROJECT_INVALID_ARGUMENT;
        return guard(
            [&]() -> TinyProjectResult
            {
                auto& b = *static_cast<ProjectHostBridge*>(p);
                auto result = b.access(true);
                if (result != TINY_PROJECT_OK)
                    return result;
                if (!b.m_renderer)
                    return TINY_PROJECT_NOT_READY;
                if (!b.m_retroChanged)
                {
                    b.m_savedRetroEnabled = b.m_renderer->isRetroPostProcessEnabled();
                    b.m_savedRetroBlockSize = b.m_renderer->getRetroBlockSize();
                    b.m_savedRetroColorLevels = b.m_renderer->getRetroColorLevels();
                    b.m_retroChanged = true;
                }
                b.m_renderer->setRetroPostProcess(enabled != 0, blockSize, colorLevels);
                // 设备不支持该通道时明确回报，便于项目侧区分“已关闭”和“不支持”。
                return b.m_renderer->isRetroPostProcessEnabled() == (enabled != 0) ? TINY_PROJECT_OK
                                                                                   : TINY_PROJECT_UNSUPPORTED;
            });
    };
}
bool ProjectHostBridge::spawnBurstEmitter(const TinyProjectVec3& position, const TinyProjectColor& start,
                                          const TinyProjectColor& end, const float radius, const float lifetime,
                                          const std::uint32_t count)
{
    if (!m_vfx)
        return false;
    ParticleEmitterSettings settings;
    settings.maximumParticles = std::max<std::size_t>(count * 2, 16);
    settings.emissionRate = 0.0f;
    settings.lifetime = lifetime;
    settings.speed = radius * 2.5f;
    settings.spread = 1.0f;
    settings.startSize = std::max(0.02f, radius * 0.35f);
    settings.endSize = 0.0f;
    settings.gravity = {0.0f, -3.0f, 0.0f};
    settings.startColor = vec4(start);
    settings.endColor = vec4(end);
    const std::uint64_t emitterId = m_vfx->createEmitter(settings);
    if (auto* emitter = m_vfx->findEmitter(emitterId))
    {
        emitter->setPosition(vec3(position));
        emitter->burst(count);
    }
    // 生命周期结束后仍保留发射器只会累积空对象，因此按最迟粒子寿命回收。
    m_oneShots.push_back({emitterId, lifetime * 1.5f + 0.2f});
    return true;
}
void ProjectHostBridge::update(const float deltaTime)
{
    if (!m_vfx || m_oneShots.empty() || deltaTime <= 0.0f)
        return;
    for (std::size_t index = 0; index < m_oneShots.size();)
    {
        m_oneShots[index].remaining -= deltaTime;
        if (m_oneShots[index].remaining > 0.0f)
        {
            ++index;
            continue;
        }
        m_vfx->destroyEmitter(m_oneShots[index].emitter);
        m_oneShots[index] = m_oneShots.back();
        m_oneShots.pop_back();
    }
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
void ProjectHostBridge::clearSessionResources() noexcept
{
    if (m_vfx)
        for (const auto& effect : m_oneShots)
            m_vfx->destroyEmitter(effect.emitter);
    m_oneShots.clear();
    if (m_ui)
        for (const auto& id : m_ownedWidgets)
            if (const auto* widget = m_ui->Find(id); widget && widget->ownerToken == m_uiOwnerToken)
                m_ui->Remove(id);
    m_ownedWidgets.clear();
    if (m_renderer)
    {
        if (m_mouseCaptureChanged)
            m_renderer->setMouseCapture(false);
        if (m_retroChanged)
            m_renderer->setRetroPostProcess(m_savedRetroEnabled, m_savedRetroBlockSize, m_savedRetroColorLevels);
    }
    m_mouseCaptureChanged = false;
    m_retroChanged = false;
    m_uiOwnerToken = 0;
}
void ProjectHostBridge::setWorldAccess(bool ready, bool writable) noexcept
{
    const bool nextWritable = ready && writable;
    // This transition also runs after a failed begin callback, before any further simulation tick.
    if (m_writable && !nextWritable)
        clearSessionResources();
    else if (!m_writable && nextWritable)
        m_uiOwnerToken = nextUiOwner.fetch_add(1);
    m_ready = ready;
    m_writable = nextWritable;
}
void ProjectHostBridge::invalidateWorld() noexcept
{
    m_worldId.id = nextWorld.fetch_add(1);
}
} // namespace tinyengine
