// 文件：include/MiniEngine/Physics/PhysicsWorld.hpp
// 作用：管理碰撞体、刚体仿真、射线/形状查询和角色控制器。
// 约定：仿真使用固定时间步；编辑器帧率不能直接改变物理积分步长。
// 类型：公开声明。

#pragma once

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Types.hpp"

#include <vector>

namespace MiniEngine
{
class World;
enum class PhysicsBackend
{
    Builtin,
    Jolt
};
enum class CollisionEventType
{
    Enter,
    Stay,
    Exit
};
struct CollisionEvent
{
    Entity first = InvalidEntity;
    Entity second = InvalidEntity;
    CollisionEventType type = CollisionEventType::Enter;
    bool trigger = false;
};
struct RaycastHit
{
    Entity entity = InvalidEntity;
    Vec3 point;
    Vec3 normal;
    float distance = 0.0f;
};
struct PhysicsDebugBox
{
    Entity entity = InvalidEntity;
    Aabb bounds;
    bool colliding = false;
    bool trigger = false;
};
// 所有场景查询共用的过滤条件；ignoredEntity 用于避免角色扫掠命中自身。
struct PhysicsQuery
{
    std::uint32_t layerMask = 0xffffffffu;
    bool includeTriggers = true;
    Entity ignoredEntity = InvalidEntity;
};
// 角色移动的最终结果，而不是期望位移；调用方应使用 appliedDisplacement 更新表现层。
struct CharacterMoveResult
{
    Vec3 appliedDisplacement{};
    Vec3 groundNormal{0.0f, 1.0f, 0.0f};
    bool grounded = false;
    bool hitCeiling = false;
    bool stepped = false;
};
struct ShapeSweepGeometry
{
    ColliderShape shape = ColliderShape::Box;
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius = 0.5f;
    float height = 1.0f;
    std::string meshPath;
};

// ECS 物理门面。内置后端提供确定性的轻量实现，也可在构建时切换到 Jolt。
class PhysicsWorld
{
  public:
    /// @brief 以固定时间步推进一次物理仿真。
    /// @param world 要读取或修改的 World。
    /// @param fixedDeltaTime 时间值，单位按接口约定为秒。
    void Step(World &world, float fixedDeltaTime);
    /// @brief 执行射线检测并返回最近命中。
    /// @param world 要读取或修改的 World。
    /// @param ray 包含原点和方向的查询射线。
    /// @param maxDistance 查询允许的最大距离。
    /// @param hit 接收最近命中信息的输出参数。
    /// @param query 场景查询使用的过滤条件。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Raycast(const World &world, const Ray &ray, float maxDistance, RaycastHit &hit, const PhysicsQuery &query = {}) const;
    /// @brief 沿射线方向执行球体扫掠。
    /// @param world 要读取或修改的 World。
    /// @param ray 包含原点和方向的查询射线。
    /// @param radius 球体、影响范围或查询半径。
    /// @param maxDistance 查询允许的最大距离。
    /// @param hit 接收最近命中信息的输出参数。
    /// @param query 场景查询使用的过滤条件。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SphereCast(const World &world, const Ray &ray, float radius, float maxDistance, RaycastHit &hit,
                    const PhysicsQuery &query = {}) const;
    /// @brief 沿位移方向执行通用形状扫掠。
    /// @param world 要读取或修改的 World。
    /// @param geometry 接口所需的输入参数。
    /// @param origin 射线、查询或坐标系原点。
    /// @param displacement 期望移动的位移向量。
    /// @param hit 接收最近命中信息的输出参数。
    /// @param query 场景查询使用的过滤条件。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ShapeSweep(const World &world, const ShapeSweepGeometry &geometry, const Vec3 &origin, const Vec3 &displacement, RaycastHit &hit,
                    const PhysicsQuery &query = {}) const;
    /// @brief 查询与指定范围重叠的对象。
    /// @param world 要读取或修改的 World。
    /// @param bounds 查询或对象使用的轴对齐包围盒。
    /// @param query 场景查询使用的过滤条件。
    /// @return 返回实体句柄；失败时返回 `InvalidEntity`。
    std::vector<Entity> Overlap(const World &world, const Aabb &bounds, const PhysicsQuery &query = {}) const;
    // 兼容旧调用的简化移动接口；新代码优先使用可报告坡面、台阶和顶碰撞的控制器版本。
    /// @brief 移动 `Character`。
    /// @param world 要读取或修改的 World。
    /// @param entity 目标实体句柄。
    /// @param displacement 期望移动的位移向量。
    /// @param skinWidth 角色与碰撞面的安全间隙。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 MoveCharacter(World &world, Entity entity, const Vec3 &displacement, float skinWidth = 0.02f) const;
    /// @brief 移动 `CharacterController`。
    /// @param world 要读取或修改的 World。
    /// @param entity 目标实体句柄。
    /// @param displacement 期望移动的位移向量。
    /// @param stepHeight 目标尺寸、数量、索引或限制值。
    /// @param skinWidth 角色与碰撞面的安全间隙。
    /// @param maximumSlopeDegrees 角色可站立坡面的最大角度。
    /// @return 返回接口计算或查询得到的结果。
    CharacterMoveResult MoveCharacterController(World &world, Entity entity, const Vec3 &displacement, float stepHeight = 0.3f,
                                                float skinWidth = 0.02f, float maximumSlopeDegrees = 50.0f) const;
    /// @brief 获取 `Events`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<CollisionEvent> &GetEvents() const;
    /// @brief 获取 `DebugBoxes`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<PhysicsDebugBox> &GetDebugBoxes() const;
    /// @brief 清空对象保存的状态和数据。
    void Clear();
    /// @brief 设置 `Backend`。
    /// @param backend 要使用的实现后端。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SetBackend(PhysicsBackend backend);
    /// @brief 获取 `Backend`。
    /// @return 返回接口计算或查询得到的结果。
    PhysicsBackend GetBackend() const;
    /// @brief 判断是否满足 `BackendAvailable` 条件。
    /// @param backend 要使用的实现后端。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    static bool IsBackendAvailable(PhysicsBackend backend);

  private:
    /// @brief 计算 `Bounds`。
    /// @param world 要读取或修改的 World。
    /// @param entity 目标实体句柄。
    /// @return 返回接口计算或查询得到的结果。
    static Aabb ComputeBounds(const World &world, Entity entity);
    std::vector<CollisionEvent> m_events;
    std::vector<std::pair<Entity, Entity>> m_previousPairs;
    std::vector<PhysicsDebugBox> m_debugBoxes;
    PhysicsBackend m_backend = PhysicsBackend::Builtin;
};
} // namespace MiniEngine
