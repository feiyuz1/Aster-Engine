#pragma once
#include "tinyengine/project_sdk/project_api.h"
#include "tinyengine/project_sdk/project_gameplay_api.h"
#include <exception>
#include <string_view>
#include <type_traits>

namespace tinyengine::project_sdk
{
/// @brief 引擎内置灯光类型，与 TinyProjectGameplayAPI::set_light 的 type 取值一致。
enum class LightType : unsigned
{
    Directional = TINY_PROJECT_LIGHT_DIRECTIONAL,
    Point = TINY_PROJECT_LIGHT_POINT,
    Spot = TINY_PROJECT_LIGHT_SPOT
};
/// @brief 射线查询结果。hit 为 false 时其余字段保持零值。
struct HitResult
{
    bool hit = false;
    TinyProjectEntity entity{};
    TinyProjectVec3 point{};
    TinyProjectVec3 normal{};
    float distance = 0.0f;
};
/// @brief 角色移动的实际结果。
struct MoveResult
{
    TinyProjectVec3 applied{};
    TinyProjectVec3 groundNormal{0.0f, 1.0f, 0.0f};
    bool grounded = false;
    bool hitCeiling = false;
    bool stepped = false;
};
namespace detail
{
inline TinyProjectString string(std::string_view text) noexcept
{
    return {text.empty() ? nullptr : text.data(), static_cast<uint64_t>(text.size())};
}
} // namespace detail
inline TinyProjectVec3 vec3(float x, float y, float z) noexcept
{
    return {x, y, z};
}
inline TinyProjectColor color(float r, float g, float b, float a = 1.0f) noexcept
{
    return {r, g, b, a};
}
inline TinyProjectUIRect rect(float x, float y, float width, float height) noexcept
{
    return {x, y, width, height};
}
class Context
{
  public:
    Context(const TinyProjectHostAPI& host, TinyProjectWorld world) noexcept : m_host(host), m_world(world) {}
    TinyProjectEntity findEntity(std::string_view);
    bool isAlive(TinyProjectEntity);
    bool getTransform(TinyProjectEntity, TinyProjectTransform&);
    bool setTransform(TinyProjectEntity, const TinyProjectTransform&);
    float getAxis(std::string_view);
    bool isDown(std::string_view);
    void log(std::string_view) const noexcept;
    TinyProjectResult getLastResult() const noexcept
    {
        return m_result;
    }

    // ---- 玩法扩展（query_interface "aster.gameplay"）----
    // 宿主未提供扩展时，以下调用全部返回 false，游戏据此降级而不是崩溃。
    /// @brief 查询并缓存玩法扩展表。
    /// @return 扩展表指针；宿主不支持时返回 `nullptr`。
    const TinyProjectGameplayAPI* gameplay() const noexcept
    {
        if (!m_gameplayQueried)
        {
            m_gameplayQueried = true;
            const void* extension = nullptr;
            const TinyProjectString id{TINY_PROJECT_GAMEPLAY_INTERFACE,
                                      sizeof(TINY_PROJECT_GAMEPLAY_INTERFACE) - 1};
            if (m_host.query_interface &&
                m_host.query_interface(m_host.host_context, id, TINY_PROJECT_GAMEPLAY_VERSION, &extension) ==
                    TINY_PROJECT_OK)
            {
                const auto* table = static_cast<const TinyProjectGameplayAPI*>(extension);
                if (table && table->struct_size >= offsetof(TinyProjectGameplayAPI, create_entity) &&
                    table->abi_version == TINY_PROJECT_GAMEPLAY_VERSION)
                    m_gameplay = table;
            }
        }
        return m_gameplay;
    }
    /// @brief 判断宿主是否提供了玩法扩展。
    bool hasGameplay() const noexcept
    {
        return gameplay() != nullptr;
    }
    /// @brief 创建运行时实体。
    /// @param name 实体名称，后续可用 findEntity 找到。
    /// @param transform 可选初始变换；为空时使用单位变换。
    /// @return 新实体句柄；失败时 value 为 0。
    TinyProjectEntity createEntity(std::string_view name, const TinyProjectTransform* transform = nullptr)
    {
        const auto* api = gameplay();
        TinyProjectEntity entity{};
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, create_entity))
            m_result = api->create_entity(api->host_context, detail::string(name), transform, &entity);
        return entity;
    }
    /// @brief 创建带位置的运行时实体。
    TinyProjectEntity createEntity(std::string_view name, float x, float y, float z)
    {
        const TinyProjectTransform transform{{x, y, z}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
        return createEntity(name, &transform);
    }
    /// @brief 销毁实体；句柄失效后再使用会被宿主拒绝。
    bool destroyEntity(TinyProjectEntity entity)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, destroy_entity))
            m_result = api->destroy_entity(api->host_context, entity);
        return m_result == TINY_PROJECT_OK;
    }
    /// @brief 设置实体的渲染网格与材质。
    bool setMesh(TinyProjectEntity entity, std::string_view mesh, std::string_view material)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, set_mesh))
            m_result = api->set_mesh(api->host_context, entity, detail::string(mesh), detail::string(material));
        return m_result == TINY_PROJECT_OK;
    }
    /// @brief 添加轴对齐盒碰撞体。
    bool setBoxCollider(TinyProjectEntity entity, TinyProjectVec3 halfExtents, bool trigger = false)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, set_box_collider))
            m_result = api->set_box_collider(api->host_context, entity, halfExtents, trigger ? 1u : 0u);
        return m_result == TINY_PROJECT_OK;
    }
    /// @brief 添加或替换灯光组件。
    bool setLight(TinyProjectEntity entity, LightType type, TinyProjectColor color, float intensity,
                  TinyProjectVec3 direction = {0.0f, -1.0f, 0.0f}, float range = 10.0f)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, set_light))
            m_result = api->set_light(api->host_context, entity, static_cast<unsigned>(type),
                                      {color.r, color.g, color.b}, intensity, direction, range);
        return m_result == TINY_PROJECT_OK;
    }
    /// @brief 执行射线查询，返回最近命中。
    bool raycast(TinyProjectVec3 origin, TinyProjectVec3 direction, float maxDistance, HitResult& hit,
                 TinyProjectEntity ignored = {})
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (!TINY_PROJECT_GAMEPLAY_HAS(api, raycast))
            return false;
        TinyProjectHit raw{};
        m_result = api->raycast(api->host_context, origin, direction, maxDistance, ignored, &raw);
        hit = {};
        if (m_result != TINY_PROJECT_OK || !raw.has_hit)
            return false;
        hit.hit = true;
        hit.entity = raw.entity;
        hit.point = raw.point;
        hit.normal = raw.normal;
        hit.distance = raw.distance;
        return true;
    }
    /// @brief 以引擎角色控制器移动实体，位移已处理碰撞、台阶和地面检测。
    bool moveCharacter(TinyProjectEntity entity, TinyProjectVec3 displacement, MoveResult& moved)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        moved = {};
        if (!TINY_PROJECT_GAMEPLAY_HAS(api, move_character))
            return false;
        TinyProjectCharacterMove raw{};
        m_result = api->move_character(api->host_context, entity, displacement, &raw);
        if (m_result != TINY_PROJECT_OK)
            return false;
        moved.applied = raw.applied;
        moved.groundNormal = raw.ground_normal;
        moved.grounded = raw.grounded != 0;
        moved.hitCeiling = raw.hit_ceiling != 0;
        moved.stepped = raw.stepped != 0;
        return true;
    }
    /// @brief 查询按键是否按住；scancode 采用 SDL2 编号。
    bool keyDown(unsigned scancode) const
    {
        const auto* api = gameplay();
        unsigned down = 0;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, key_down) &&
            api->key_down(api->host_context, scancode, &down) == TINY_PROJECT_OK)
            return down != 0;
        return false;
    }
    /// @brief 查询本帧是否发生按键按下沿。
    bool keyPressed(unsigned scancode) const
    {
        const auto* api = gameplay();
        unsigned pressed = 0;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, key_pressed) &&
            api->key_pressed(api->host_context, scancode, &pressed) == TINY_PROJECT_OK)
            return pressed != 0;
        return false;
    }
    bool primaryDown() const
    {
        const auto* api = gameplay();
        unsigned value = 0;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, primary_down) &&
            api->primary_down(api->host_context, &value) == TINY_PROJECT_OK)
            return value != 0;
        return false;
    }
    bool primaryPressed() const
    {
        const auto* api = gameplay();
        unsigned value = 0;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, primary_pressed) &&
            api->primary_pressed(api->host_context, &value) == TINY_PROJECT_OK)
            return value != 0;
        return false;
    }
    bool cursorPosition(float& x, float& y) const
    {
        const auto* api = gameplay();
        x = y = 0.0f;
        return TINY_PROJECT_GAMEPLAY_HAS(api, cursor_position) &&
               api->cursor_position(api->host_context, &x, &y) == TINY_PROJECT_OK;
    }
    bool viewportSize(float& width, float& height) const
    {
        const auto* api = gameplay();
        width = height = 0.0f;
        return TINY_PROJECT_GAMEPLAY_HAS(api, viewport_size) &&
               api->viewport_size(api->host_context, &width, &height) == TINY_PROJECT_OK;
    }
    bool setMouseCapture(bool captured)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, set_mouse_capture))
            m_result = api->set_mouse_capture(api->host_context, captured ? 1u : 0u);
        return m_result == TINY_PROJECT_OK;
    }
    /// @brief 创建或更新文本控件。
    bool uiLabel(std::string_view id, std::string_view text, TinyProjectUIRect rect, TinyProjectColor color,
                 float fontSize = 18.0f)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, ui_label))
            m_result = api->ui_label(api->host_context, detail::string(id), detail::string(text), rect, color,
                                     fontSize);
        return m_result == TINY_PROJECT_OK;
    }
    /// @brief 创建或更新面板控件。
    bool uiPanel(std::string_view id, TinyProjectUIRect rect, TinyProjectColor color)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, ui_panel))
            m_result = api->ui_panel(api->host_context, detail::string(id), rect, color);
        return m_result == TINY_PROJECT_OK;
    }
    bool uiSetText(std::string_view id, std::string_view text)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, ui_set_text))
            m_result = api->ui_set_text(api->host_context, detail::string(id), detail::string(text));
        return m_result == TINY_PROJECT_OK;
    }
    bool uiSetRect(std::string_view id, TinyProjectUIRect rect)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, ui_set_rect))
            m_result = api->ui_set_rect(api->host_context, detail::string(id), rect);
        return m_result == TINY_PROJECT_OK;
    }
    bool uiSetColor(std::string_view id, TinyProjectColor color)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, ui_set_color))
            m_result = api->ui_set_color(api->host_context, detail::string(id), color);
        return m_result == TINY_PROJECT_OK;
    }
    bool uiSetVisible(std::string_view id, bool visible)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, ui_set_visible))
            m_result = api->ui_set_visible(api->host_context, detail::string(id), visible ? 1u : 0u);
        return m_result == TINY_PROJECT_OK;
    }
    bool uiRemove(std::string_view id)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, ui_remove))
            m_result = api->ui_remove(api->host_context, detail::string(id));
        return m_result == TINY_PROJECT_OK;
    }
    /// @brief 播放一次性音效；路径按项目资产相对路径解析。
    bool playSound(std::string_view path, float volume = 1.0f)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, play_sound))
            m_result = api->play_sound(api->host_context, detail::string(path), volume);
        return m_result == TINY_PROJECT_OK;
    }
    /// @brief 生成一次性粒子爆发，由宿主按寿命自动回收。
    bool spawnBurst(TinyProjectVec3 position, TinyProjectColor start, TinyProjectColor end, float radius,
                    float lifetime, unsigned count)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, spawn_burst))
            m_result = api->spawn_burst(api->host_context, position, start, end, radius, lifetime, count);
        return m_result == TINY_PROJECT_OK;
    }
    /// @brief 开关复古像素化后处理通道。
    /// @param enabled 是否启用。
    /// @param blockSize 像素块边长（像素）。
    /// @param colorLevels 每通道色阶数。
    /// @note 旧宿主没有该成员时返回 `false`，调用方应视为不支持而不是失败。
    bool setRetroPostProcess(bool enabled, float blockSize = 3.0f, float colorLevels = 32.0f)
    {
        const auto* api = gameplay();
        m_result = TINY_PROJECT_UNSUPPORTED;
        if (TINY_PROJECT_GAMEPLAY_HAS(api, set_retro_post))
            m_result = api->set_retro_post(api->host_context, enabled ? 1u : 0u, blockSize, colorLevels);
        return m_result == TINY_PROJECT_OK;
    }

  private:
    const TinyProjectHostAPI& m_host;
    TinyProjectWorld m_world;
    TinyProjectResult m_result = TINY_PROJECT_OK;
    mutable const TinyProjectGameplayAPI* m_gameplay = nullptr;
    mutable bool m_gameplayQueried = false;
};
class Session
{
  public:
    virtual ~Session() = default;
    virtual void onBeginPlay(Context&) {}
    virtual void onUpdate(Context&, float) {}
    virtual void onFixedUpdate(Context&, float) {}
    virtual void onLateUpdate(Context&, float) {}
    virtual void onEndPlay(Context&) {}
};
namespace detail
{
template <class T> struct Adapter
{
    struct Module
    {
        const TinyProjectHostAPI* host;
    };
    struct Instance
    {
        Context context;
        T session;
        Instance(Module& m, TinyProjectWorld w) : context(*m.host, w) {}
    };
    template <class F> static TinyProjectResult invoke(void* p, F&& f) noexcept
    {
        if (!p)
            return TINY_PROJECT_INVALID_ARGUMENT;
        auto& i = *static_cast<Instance*>(p);
        try
        {
            f(i.session, i.context);
            return TINY_PROJECT_OK;
        }
        catch (const std::exception& e)
        {
            i.context.log("Project session exception");
            i.context.log(e.what());
        }
        catch (...)
        {
            i.context.log("Unknown project session exception");
        }
        return TINY_PROJECT_FAILED;
    }
    static TinyProjectResult TINY_PROJECT_CALL load(const TinyProjectHostAPI* host, void** out) noexcept
    {
        if (out)
            *out = nullptr;
        if (!out || !host || host->abi_version != TINY_PROJECT_ABI_VERSION ||
            host->struct_size < sizeof(TinyProjectHostAPI))
            return TINY_PROJECT_INCOMPATIBLE;
        try
        {
            *out = new Module{host};
            return TINY_PROJECT_OK;
        }
        catch (...)
        {
            return TINY_PROJECT_FAILED;
        }
    }
    static void TINY_PROJECT_CALL Unload(void* p) noexcept
    {
        delete static_cast<Module*>(p);
    }
    static TinyProjectResult TINY_PROJECT_CALL Create(void* p, TinyProjectWorld world, void** out) noexcept
    {
        if (out)
            *out = nullptr;
        if (!p || !out || !world.id)
            return TINY_PROJECT_INVALID_ARGUMENT;
        try
        {
            *out = new Instance(*static_cast<Module*>(p), world);
            return TINY_PROJECT_OK;
        }
        catch (...)
        {
            return TINY_PROJECT_FAILED;
        }
    }
    static void TINY_PROJECT_CALL Destroy(void* p) noexcept
    {
        delete static_cast<Instance*>(p);
    }
    static TinyProjectResult TINY_PROJECT_CALL Begin(void* p) noexcept
    {
        return invoke(p, [](T& s, Context& c) { s.onBeginPlay(c); });
    }
    static TinyProjectResult TINY_PROJECT_CALL Update(void* p, float dt) noexcept
    {
        return invoke(p, [dt](T& s, Context& c) { s.onUpdate(c, dt); });
    }
    static TinyProjectResult TINY_PROJECT_CALL Fixed(void* p, float dt) noexcept
    {
        return invoke(p, [dt](T& s, Context& c) { s.onFixedUpdate(c, dt); });
    }
    static TinyProjectResult TINY_PROJECT_CALL Late(void* p, float dt) noexcept
    {
        return invoke(p, [dt](T& s, Context& c) { s.onLateUpdate(c, dt); });
    }
    static void TINY_PROJECT_CALL End(void* p) noexcept
    {
        (void)invoke(p, [](T& s, Context& c) { s.onEndPlay(c); });
    }
};
} // namespace detail
template <class TSession>
const TinyProjectAPI* ExportProject(uint32_t requestedAbi, const TinyProjectModuleInfo& info) noexcept
{
    static_assert(std::is_base_of<Session, TSession>::value, "Project session must derive from SDK Session");
    if (requestedAbi != TINY_PROJECT_ABI_VERSION)
        return nullptr;
    using A = detail::Adapter<TSession>;
    static const TinyProjectAPI api{TINY_PROJECT_ABI_VERSION,
                                    sizeof(TinyProjectAPI),
                                    info,
                                    &A::load,
                                    &A::Unload,
                                    &A::Create,
                                    &A::Destroy,
                                    &A::Begin,
                                    &A::Update,
                                    &A::End,
                                    &A::Fixed,
                                    &A::Late};
    return &api;
}
} // namespace tinyengine::project_sdk
