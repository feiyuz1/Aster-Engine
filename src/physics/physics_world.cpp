// 文件：src/physics/physics_world.cpp
// 作用：管理碰撞体、刚体仿真、射线/形状查询和角色控制器。
// 约定：仿真使用固定时间步；编辑器帧率不能直接改变物理积分步长。
// 类型：实现文件。

#include "tinyengine/physics/physics_world.h"

#include "physics_acceleration.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/resource/resource.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace tinyengine
{
namespace
{
std::pair<Entity, Entity> OrderedPair(Entity a, Entity b)
{
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}
bool rayAabb(const Ray& ray, const Aabb& bounds, const float maxDistance, float& distance, Vec3* hitNormal = nullptr)
{
    float tMin = 0.0f, tMax = maxDistance;
    Vec3 normal{};
    for (int axis = 0; axis < 3; ++axis)
    {
        const float origin = axis == 0 ? ray.origin.x : axis == 1 ? ray.origin.y : ray.origin.z;
        const float direction = axis == 0 ? ray.direction.x : axis == 1 ? ray.direction.y : ray.direction.z;
        const float minimum = axis == 0 ? bounds.minimum.x : axis == 1 ? bounds.minimum.y : bounds.minimum.z;
        const float maximum = axis == 0 ? bounds.maximum.x : axis == 1 ? bounds.maximum.y : bounds.maximum.z;
        if (direction == 0.0f)
        {
            if (origin < minimum || origin > maximum)
                return false;
            continue;
        }
        float nearValue = (minimum - origin) / direction, farValue = (maximum - origin) / direction;
        float normalSign = -1.0f;
        if (nearValue > farValue)
        {
            std::swap(nearValue, farValue);
            normalSign = 1.0f;
        }
        if (nearValue > tMin)
        {
            tMin = nearValue;
            normal = {};
            if (axis == 0)
                normal.x = normalSign;
            else if (axis == 1)
                normal.y = normalSign;
            else
                normal.z = normalSign;
        }
        tMax = std::min(tMax, farValue);
        if (tMin > tMax)
            return false;
    }
    distance = tMin;
    if (hitNormal)
        *hitNormal = normal;
    return true;
}
Vec3 transformPoint(const Mat4& matrix, const Vec3& point)
{
    return {matrix.at(0, 0) * point.x + matrix.at(0, 1) * point.y + matrix.at(0, 2) * point.z + matrix.at(0, 3),
            matrix.at(1, 0) * point.x + matrix.at(1, 1) * point.y + matrix.at(1, 2) * point.z + matrix.at(1, 3),
            matrix.at(2, 0) * point.x + matrix.at(2, 1) * point.y + matrix.at(2, 2) * point.z + matrix.at(2, 3)};
}
bool rayTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, const float maximum, float& distance,
                 Vec3& normal)
{
    const Vec3 edge1 = b - a, edge2 = c - a, p = cross(ray.direction, edge2);
    const float determinant = dot(edge1, p);
    if (std::abs(determinant) < 1e-7f)
        return false;
    const float inverse = 1.0f / determinant;
    const Vec3 t = ray.origin - a;
    const float u = dot(t, p) * inverse;
    if (u < 0.0f || u > 1.0f)
        return false;
    const Vec3 q = cross(t, edge1);
    const float v = dot(ray.direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f)
        return false;
    distance = dot(edge2, q) * inverse;
    if (distance < 0.0f || distance > maximum)
        return false;
    normal = normalize(cross(edge1, edge2));
    if (dot(normal, ray.direction) > 0.0f)
        normal = normal * -1.0f;
    return true;
}

bool rayMesh(detail::PhysicsAcceleration& acceleration, const World& world, const Entity entity, const Ray& ray,
             const float maximum, float& closest, Vec3& hitNormal)
{
    const auto* collider = world.GetComponent<ColliderComponent>(entity);
    const auto* transform = world.GetComponent<TransformComponent>(entity);
    if (!collider || !transform || collider->meshPath.empty())
        return false;
    const auto mesh = acceleration.meshFor(collider->meshPath);
    if (!mesh)
        return false;
    const Mat4 matrix = transformMatrix(transform->position, transform->rotation, transform->scale);
    bool found = false;
    mesh->tree.visit(
        [&](const Aabb& localBounds)
        {
            float distance;
            return rayAabb(ray, detail::transformBounds(localBounds, matrix), std::min(maximum, closest), distance);
        },
        [&](std::uint64_t index)
        {
            ++acceleration.statistics.triangleTests;
            const auto& triangle = mesh->triangles[index];
            float distance = 0.0f;
            Vec3 normal;
            if (rayTriangle(ray, transformPoint(matrix, mesh->vertices[triangle[0]]),
                            transformPoint(matrix, mesh->vertices[triangle[1]]),
                            transformPoint(matrix, mesh->vertices[triangle[2]]), std::min(maximum, closest), distance,
                            normal))
            {
                closest = distance;
                hitNormal = normal;
                found = true;
            }
            return true;
        },
        acceleration.statistics.triangleBoundsTests);
    return found;
}
bool meshTrianglesOverlapBounds(detail::PhysicsAcceleration& acceleration, const World& world, const Entity meshEntity,
                                const Aabb& otherBounds)
{
    const auto* collider = world.GetComponent<ColliderComponent>(meshEntity);
    const auto* transform = world.GetComponent<TransformComponent>(meshEntity);
    if (!collider || !transform || collider->meshPath.empty())
        return false;
    const auto mesh = acceleration.meshFor(collider->meshPath);
    if (!mesh)
        return false;
    const Mat4 matrix = transformMatrix(transform->position, transform->rotation, transform->scale);
    bool found = false;
    mesh->tree.visit([&](const Aabb& bounds)
                     { return detail::transformBounds(bounds, matrix).intersects(otherBounds); },
                     [&](std::uint64_t index)
                     {
                         ++acceleration.statistics.triangleTests;
                         const auto& triangle = mesh->triangles[index];
                         const Vec3 a = transformPoint(matrix, mesh->vertices[triangle[0]]);
                         const Vec3 b = transformPoint(matrix, mesh->vertices[triangle[1]]);
                         const Vec3 c = transformPoint(matrix, mesh->vertices[triangle[2]]);
                         const Aabb triangleBounds{
                             {std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})},
                             {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})}};
                         found = triangleBounds.intersects(otherBounds);
                         return !found;
                     },
                     acceleration.statistics.triangleBoundsTests);
    return found;
}

Vec3 closestPointOnTriangle(const Vec3& point, const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 ab = b - a, ac = c - a, ap = point - a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f)
        return a;
    const Vec3 bp = point - b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3)
        return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        return a + ab * (d1 / (d1 - d3));
    const Vec3 cp = point - c;
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6)
        return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float denominator = 1.0f / (va + vb + vc);
    return a + ab * (vb * denominator) + ac * (vc * denominator);
}

bool meshSphereContact(detail::PhysicsAcceleration& acceleration, const World& world, const Entity meshEntity,
                       const Entity sphereEntity, Vec3& meshToSphere, float& penetration)
{
    const auto* meshCollider = world.GetComponent<ColliderComponent>(meshEntity);
    const auto* meshTransform = world.GetComponent<TransformComponent>(meshEntity);
    const auto* sphereCollider = world.GetComponent<ColliderComponent>(sphereEntity);
    const auto* sphereTransform = world.GetComponent<TransformComponent>(sphereEntity);
    if (!meshCollider || !meshTransform || meshCollider->meshPath.empty() || !sphereCollider || !sphereTransform ||
        sphereCollider->shape != ColliderShape::Sphere)
        return false;
    const auto mesh = acceleration.meshFor(meshCollider->meshPath);
    if (!mesh)
        return false;
    const Mat4 matrix = transformMatrix(meshTransform->position, meshTransform->rotation, meshTransform->scale);
    const float radius =
        sphereCollider->radius * std::max({std::abs(sphereTransform->scale.x), std::abs(sphereTransform->scale.y),
                                           std::abs(sphereTransform->scale.z)});
    bool found = false;
    penetration = 0.0f;
    const Vec3 extent{radius, radius, radius};
    const Aabb sphereBounds{sphereTransform->position - extent, sphereTransform->position + extent};
    std::uint64_t bestIndex = std::numeric_limits<std::uint64_t>::max();
    mesh->tree.visit([&](const Aabb& bounds)
                     { return detail::transformBounds(bounds, matrix).intersects(sphereBounds); },
                     [&](std::uint64_t index)
                     {
                         ++acceleration.statistics.triangleTests;
                         const auto& triangle = mesh->triangles[index];
                         const Vec3 a = transformPoint(matrix, mesh->vertices[triangle[0]]);
                         const Vec3 b = transformPoint(matrix, mesh->vertices[triangle[1]]);
                         const Vec3 c = transformPoint(matrix, mesh->vertices[triangle[2]]);
                         const Vec3 closest = closestPointOnTriangle(sphereTransform->position, a, b, c);
                         const Vec3 delta = sphereTransform->position - closest;
                         const float distanceSquared = lengthSquared(delta);
                         if (distanceSquared > radius * radius)
                             return true;
                         const float distance = std::sqrt(std::max(0.0f, distanceSquared));
                         const float candidatePenetration = radius - distance;
                         if (!found || candidatePenetration > penetration ||
                             (candidatePenetration == penetration && index < bestIndex))
                         {
                             Vec3 normal = distance > 0.00001f ? delta / distance : normalize(cross(b - a, c - a));
                             if (lengthSquared(normal) < 0.00001f)
                                 normal = {0.0f, 1.0f, 0.0f};
                             if (dot(normal, sphereTransform->position - a) < 0.0f)
                                 normal = normal * -1.0f;
                             meshToSphere = normal;
                             penetration = candidatePenetration;
                             found = true;
                             bestIndex = index;
                         }
                         return true;
                     },
                     acceleration.statistics.triangleBoundsTests);
    return found;
}
Vec3 shapeSupport(detail::PhysicsAcceleration& acceleration, const World& world, const Entity entity,
                  const Vec3& direction)
{
    const auto* collider = world.GetComponent<ColliderComponent>(entity);
    const auto* transform = world.GetComponent<TransformComponent>(entity);
    if (!collider || !transform)
        return {};
    const Vec3 normalized = lengthSquared(direction) > 1e-10f ? normalize(direction) : Vec3{1.0f, 0.0f, 0.0f};
    const Mat4 matrix = transformMatrix(transform->position, transform->rotation, transform->scale);
    if (collider->shape == ColliderShape::Sphere)
    {
        const float scale =
            std::max({std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z)});
        return transform->position + normalized * (collider->radius * scale);
    }
    if (collider->shape == ColliderShape::Capsule)
    {
        const Vec3 top = transformPoint(matrix, {0.0f, collider->height * 0.5f, 0.0f});
        const Vec3 bottom = transformPoint(matrix, {0.0f, -collider->height * 0.5f, 0.0f});
        const Vec3 end = dot(top, direction) > dot(bottom, direction) ? top : bottom;
        const float scale = std::max(std::abs(transform->scale.x), std::abs(transform->scale.z));
        return end + normalized * (collider->radius * scale);
    }
    Vec3 best{};
    float bestProjection = std::numeric_limits<float>::lowest();
    const auto consider = [&](const Vec3& point)
    {
        const float projection = dot(point, direction);
        if (projection > bestProjection)
        {
            bestProjection = projection;
            best = point;
        }
    };
    if (collider->shape == ColliderShape::ConvexHull && !collider->meshPath.empty())
    {
        if (const auto mesh = acceleration.meshFor(collider->meshPath))
        {
            for (const auto& vertex : mesh->vertices)
                consider(transformPoint(matrix, vertex));
            return best;
        }
    }
    for (int x = -1; x <= 1; x += 2)
        for (int y = -1; y <= 1; y += 2)
            for (int z = -1; z <= 1; z += 2)
                consider(transformPoint(
                    matrix, {collider->halfExtents.x * x, collider->halfExtents.y * y, collider->halfExtents.z * z}));
    return best;
}

Vec3 minkowskiSupport(detail::PhysicsAcceleration& acceleration, const World& world, const Entity first,
                      const Entity second, const Vec3& direction)
{
    return shapeSupport(acceleration, world, first, direction) -
           shapeSupport(acceleration, world, second, direction * -1.0f);
}

Vec3 tripleCross(const Vec3& a, const Vec3& b, const Vec3& c)
{
    return cross(cross(a, b), c);
}

bool updateSimplex(std::vector<Vec3>& simplex, Vec3& direction)
{
    const Vec3 a = simplex.back(), ao = a * -1.0f;
    if (simplex.size() == 2)
    {
        const Vec3 b = simplex[0], ab = b - a;
        if (dot(ab, ao) > 0.0f)
            direction = tripleCross(ab, ao, ab);
        else
        {
            simplex = {a};
            direction = ao;
        }
        return false;
    }
    if (simplex.size() == 3)
    {
        const Vec3 b = simplex[1], c = simplex[0], ab = b - a, ac = c - a;
        const Vec3 abc = cross(ab, ac);
        if (dot(cross(abc, ac), ao) > 0.0f)
        {
            if (dot(ac, ao) > 0.0f)
            {
                simplex = {c, a};
                direction = tripleCross(ac, ao, ac);
            }
            else
            {
                simplex = {b, a};
                direction = dot(ab, ao) > 0.0f ? tripleCross(ab, ao, ab) : ao;
                if (dot(ab, ao) <= 0.0f)
                    simplex = {a};
            }
        }
        else if (dot(cross(ab, abc), ao) > 0.0f)
        {
            simplex = {b, a};
            direction = dot(ab, ao) > 0.0f ? tripleCross(ab, ao, ab) : ao;
            if (dot(ab, ao) <= 0.0f)
                simplex = {a};
        }
        else if (dot(abc, ao) > 0.0f)
            direction = abc;
        else
        {
            simplex = {b, c, a};
            direction = abc * -1.0f;
        }
        return false;
    }
    const Vec3 b = simplex[2], c = simplex[1], d = simplex[0];
    const Vec3 ab = b - a, ac = c - a, ad = d - a;
    const Vec3 abc = cross(ab, ac), acd = cross(ac, ad), adb = cross(ad, ab);
    if (dot(abc, ao) > 0.0f)
    {
        simplex = {c, b, a};
        direction = abc;
        return false;
    }
    if (dot(acd, ao) > 0.0f)
    {
        simplex = {d, c, a};
        direction = acd;
        return false;
    }
    if (dot(adb, ao) > 0.0f)
    {
        simplex = {b, d, a};
        direction = adb;
        return false;
    }
    return true;
}

bool convexOverlap(detail::PhysicsAcceleration& acceleration, const World& world, const Entity first,
                   const Entity second)
{
    const auto* firstTransform = world.GetComponent<TransformComponent>(first);
    const auto* secondTransform = world.GetComponent<TransformComponent>(second);
    Vec3 direction = firstTransform && secondTransform ? secondTransform->position - firstTransform->position
                                                       : Vec3{1.0f, 0.0f, 0.0f};
    if (lengthSquared(direction) < 1e-8f)
        direction = {1.0f, 0.0f, 0.0f};
    std::vector<Vec3> simplex{minkowskiSupport(acceleration, world, first, second, direction)};
    direction = simplex.front() * -1.0f;
    for (int iteration = 0; iteration < 32; ++iteration)
    {
        const Vec3 point = minkowskiSupport(acceleration, world, first, second, direction);
        if (dot(point, direction) < 0.0f)
            return false;
        simplex.push_back(point);
        if (updateSimplex(simplex, direction))
            return true;
        if (lengthSquared(direction) < 1e-10f)
            return true;
    }
    return false;
}
} // namespace

detail::PhysicsAcceleration& PhysicsWorld::accelerationState() const
{
    if (!m_acceleration)
        m_acceleration = std::make_shared<detail::PhysicsAcceleration>();
    else if (m_acceleration.use_count() != 1)
        m_acceleration = std::make_shared<detail::PhysicsAcceleration>(*m_acceleration);
    return *m_acceleration;
}

Aabb PhysicsWorld::computeBounds(const World& world, const Entity entity) const
{
    const auto* transform = world.GetComponent<TransformComponent>(entity);
    const auto* collider = world.GetComponent<ColliderComponent>(entity);
    if (!transform || !collider)
        return {};
    const Mat4 matrix = transformMatrix(transform->position, transform->rotation, transform->scale);
    if (collider->shape == ColliderShape::Sphere)
    {
        const float radius = collider->radius * std::max({std::abs(transform->scale.x), std::abs(transform->scale.y),
                                                          std::abs(transform->scale.z)});
        const Vec3 extent{radius, radius, radius};
        return {transform->position - extent, transform->position + extent};
    }
    if (collider->shape == ColliderShape::Capsule)
    {
        const Vec3 top = transformPoint(matrix, {0.0f, collider->height * 0.5f, 0.0f});
        const Vec3 bottom = transformPoint(matrix, {0.0f, -collider->height * 0.5f, 0.0f});
        const float radius = collider->radius * std::max(std::abs(transform->scale.x), std::abs(transform->scale.z));
        return {{std::min(top.x, bottom.x) - radius, std::min(top.y, bottom.y) - radius,
                 std::min(top.z, bottom.z) - radius},
                {std::max(top.x, bottom.x) + radius, std::max(top.y, bottom.y) + radius,
                 std::max(top.z, bottom.z) + radius}};
    }
    if ((collider->shape == ColliderShape::ConvexHull || collider->shape == ColliderShape::TriangleMesh) &&
        !collider->meshPath.empty())
        if (const auto mesh = accelerationState().meshFor(collider->meshPath))
            return detail::transformBounds(mesh->bounds, matrix);
    const Vec3 extent = collider->shape == ColliderShape::Box
                            ? collider->halfExtents
                            : Vec3{collider->radius, collider->radius, collider->radius};
    return detail::transformBounds({extent * -1.0f, extent}, matrix);
}

// 差异同步：缓存每个碰撞体"决定包围盒的全部输入"，无变化的实体零开销跳过。
// 每帧的射线/角色移动/物理步都会调到这里；大场景下全量重算包围盒是主要帧耗，
// 而一帧内真正移动的通常只有几个动态体。遍历次序与旧实现一致（存活实体集合次序），
// 保证 broadphase 配对与接触求解顺序不变。
void PhysicsWorld::synchronizeScene(const World& world) const
{
    auto& acceleration = accelerationState();
    auto& records = acceleration.sceneRecords;
    auto& index = acceleration.sceneIndex;
    const std::uint64_t stamp = ++acceleration.syncStamp;
    ++acceleration.statistics.sceneSyncs;

    bool structural = acceleration.sceneStructureDirty;
    bool staticsChanged = false;
    bool dynamicsChanged = false;

    for (const Entity entity : world.GetEntityManager().GetAliveEntities())
    {
        const auto* collider = world.GetComponent<ColliderComponent>(entity);
        const auto* transform = world.GetComponent<TransformComponent>(entity);
        if (!collider || !transform)
            continue;
        const auto* body = world.GetComponent<RigidBodyComponent>(entity);
        const auto found = index.find(entity);
        if (found != index.end())
        {
            detail::SceneSyncRecord& record = records[found->second];
            record.lastSeen = stamp;
            const std::uint32_t previousBodyType = record.bodyType;
            const std::uint32_t newBodyType = static_cast<std::uint32_t>(body ? body->type : BodyType::Static);
            // 网格/凸包形状的包围盒依赖网格资产内容（可能热重载），不做签名缓存。
            const bool meshShaped = (collider->shape == ColliderShape::TriangleMesh ||
                                     collider->shape == ColliderShape::ConvexHull) &&
                                    !collider->meshPath.empty();
            if (!meshShaped &&
                record.matches(transform->position, transform->rotation, transform->scale,
                               static_cast<std::uint32_t>(collider->shape), collider->halfExtents,
                               collider->radius, collider->height, collider->meshPath, newBodyType))
                continue;
            const Aabb bounds = computeBounds(world, entity);
            record.position = transform->position;
            record.rotation = transform->rotation;
            record.scale = transform->scale;
            record.shape = static_cast<std::uint32_t>(collider->shape);
            record.halfExtents = collider->halfExtents;
            record.radius = collider->radius;
            record.height = collider->height;
            record.meshPath = collider->meshPath;
            record.bodyType = newBodyType;
            bool& sideChanged =
                newBodyType == static_cast<std::uint32_t>(BodyType::Static) ? staticsChanged : dynamicsChanged;
            if (!detail::sameBounds(bounds, record.bounds))
            {
                record.bounds = bounds;
                acceleration.sceneEntries[found->second].bounds = bounds;
                sideChanged = true;
            }
            // 静/动分类变化即使包围盒不变，也要把实体在两棵树之间迁移。
            if (previousBodyType != record.bodyType)
            {
                staticsChanged = true;
                dynamicsChanged = true;
            }
            continue;
        }
        // 新出现的碰撞体：追加记录与条目，保持 records 与 sceneEntries 同下标对齐。
        detail::SceneSyncRecord record;
        record.entity = entity;
        record.lastSeen = stamp;
        record.bounds = computeBounds(world, entity);
        record.position = transform->position;
        record.rotation = transform->rotation;
        record.scale = transform->scale;
        record.shape = static_cast<std::uint32_t>(collider->shape);
        record.halfExtents = collider->halfExtents;
        record.radius = collider->radius;
        record.height = collider->height;
        record.meshPath = collider->meshPath;
        record.bodyType = static_cast<std::uint32_t>(body ? body->type : BodyType::Static);
        records.push_back(std::move(record));
        acceleration.sceneEntries.push_back({entity, records.back().bounds});
        index.emplace(entity, records.size() - 1);
        structural = true;
        (records.back().bodyType != static_cast<std::uint32_t>(BodyType::Static) ? dynamicsChanged
                                                                                : staticsChanged) = true;
    }

    // 本轮没有出现的记录 = 实体销毁或组件被移除：交换删除，保持两侧下标对齐。
    for (std::size_t i = records.size(); i-- > 0;)
    {
        if (records[i].lastSeen == stamp)
            continue;
        index.erase(records[i].entity);
        const std::size_t last = records.size() - 1;
        if (i != last)
        {
            records[i] = std::move(records[last]);
            acceleration.sceneEntries[i] = acceleration.sceneEntries[last];
            index[records[i].entity] = i;
        }
        records.pop_back();
        acceleration.sceneEntries.pop_back();
        structural = true;
    }
    acceleration.sceneStructureDirty = false;

    // 实体增删改变两棵树的成员集；其余情况按所属侧独立刷新。
    if (structural)
    {
        staticsChanged = true;
        dynamicsChanged = true;
    }
    // 快路径：自上次同步以来没有任何变化，树保持原样即可。
    if (!staticsChanged && !dynamicsChanged)
    {
        ++acceleration.statistics.sceneSyncSkips;
        return;
    }

    // 包围盒变化走 update 内部的增量 refit；实体增删交给 update 自行判断重建。
    if (staticsChanged)
    {
        std::vector<detail::BoundsEntry> statics;
        statics.reserve(records.size());
        for (const auto& record : records)
            if (record.bodyType == static_cast<std::uint32_t>(BodyType::Static))
                statics.push_back({record.entity, record.bounds});
        if (acceleration.staticTree.update(std::move(statics)))
            ++acceleration.statistics.staticTreeBuilds;
    }
    if (dynamicsChanged)
    {
        std::vector<detail::BoundsEntry> dynamics;
        dynamics.reserve(records.size());
        for (const auto& record : records)
            if (record.bodyType != static_cast<std::uint32_t>(BodyType::Static))
                dynamics.push_back({record.entity, record.bounds});
        if (acceleration.dynamicTree.update(std::move(dynamics)))
            ++acceleration.statistics.dynamicTreeBuilds;
    }
}

// 先积分动态刚体，再生成接触并修正穿透，最后与上一帧接触集合比较得到 Enter/Stay/Exit。
void PhysicsWorld::step(World& world, const float fixedDeltaTime)
{
    auto& acceleration = accelerationState();
    acceleration.beginOperation();
    m_events.clear();
    world.GetComponentManager().ForEach<TransformComponent, RigidBodyComponent>(
        [this, &world, fixedDeltaTime](const Entity entity, TransformComponent& transform, RigidBodyComponent& body)
        {
            if (body.type != BodyType::Dynamic)
                return;
            if (body.useGravity)
                body.velocity.y -= 9.81f * fixedDeltaTime;
            body.velocity = body.velocity * std::max(0.0f, 1.0f - body.linearDamping * fixedDeltaTime);
            body.angularVelocity = body.angularVelocity * std::max(0.0f, 1.0f - body.angularDamping * fixedDeltaTime);
            if (body.lockRotationX)
                body.angularVelocity.x = 0.0f;
            if (body.lockRotationY)
                body.angularVelocity.y = 0.0f;
            if (body.lockRotationZ)
                body.angularVelocity.z = 0.0f;
            transform.rotation += body.angularVelocity * fixedDeltaTime;
            const Vec3 displacement = body.velocity * fixedDeltaTime;
            const auto* collider = world.GetComponent<ColliderComponent>(entity);
            RaycastHit hit;
            const ShapeSweepGeometry geometry{collider ? collider->shape : ColliderShape::Box,
                                              collider ? collider->halfExtents : Vec3{0.5f, 0.5f, 0.5f},
                                              collider ? collider->radius : 0.5f, collider ? collider->height : 1.0f,
                                              collider ? collider->meshPath : std::string{}};
            if (body.continuous && collider &&
                shapeSweep(world, geometry, transform.position, displacement, hit, {collider->mask, false, entity}))
            {
                const float distance = length(displacement);
                const float travel = std::max(0.0f, hit.distance - 0.001f);
                transform.position += displacement / distance * travel;
                const float velocityAlongNormal = dot(body.velocity, hit.normal);
                if (velocityAlongNormal < 0.0f)
                    body.velocity -= hit.normal * ((1.0f + body.restitution) * velocityAlongNormal);
            }
            else
                transform.position += displacement;
        });

    struct BroadphaseEntry
    {
        Entity entity;
        Aabb bounds;
    };
    synchronizeScene(world);
    std::vector<BroadphaseEntry> colliders;
    colliders.reserve(acceleration.sceneEntries.size());
    std::unordered_map<Entity, std::size_t> ranks;
    for (std::size_t i = 0; i < acceleration.sceneEntries.size(); ++i)
        ranks.emplace(acceleration.sceneEntries[i].payload, i);
    // 保留旧 ColliderComponent 遍历和排序次序，包含 minimum.x 相同的物体。
    world.GetComponentManager().ForEach<ColliderComponent>(
        [&](Entity entity, ColliderComponent&)
        {
            const auto found = ranks.find(entity);
            if (found != ranks.end())
                colliders.push_back({entity, acceleration.sceneEntries[found->second].bounds});
        });
    std::sort(colliders.begin(), colliders.end(),
              [](const BroadphaseEntry& a, const BroadphaseEntry& b)
              { return a.bounds.minimum.x < b.bounds.minimum.x; });
    for (std::size_t i = 0; i < colliders.size(); ++i)
        ranks[colliders[i].entity] = i;
    std::vector<std::pair<Entity, Entity>> currentPairs;
    std::vector<std::size_t> candidates;
    for (std::size_t firstIndex = 0; firstIndex < colliders.size(); ++firstIndex)
    {
        candidates.clear();
        const auto intersects = [&](const Aabb& bounds) { return colliders[firstIndex].bounds.intersects(bounds); };
        const auto collect = [&](std::uint64_t entity)
        {
            const auto secondIndex = ranks.at(entity);
            if (secondIndex > firstIndex)
                candidates.push_back(secondIndex);
            return true;
        };
        acceleration.staticTree.visit(intersects, collect, acceleration.statistics.sceneBoundsTests);
        acceleration.dynamicTree.visit(intersects, collect, acceleration.statistics.sceneBoundsTests);
        // 与原扫描保持相同的接触响应顺序；树的遍历次序不参与求解结果。
        std::sort(candidates.begin(), candidates.end());
        for (const std::size_t secondIndex : candidates)
        {
            const Entity first = colliders[firstIndex].entity, second = colliders[secondIndex].entity;
            const auto* firstCollider = world.GetComponent<ColliderComponent>(first);
            const auto* secondCollider = world.GetComponent<ColliderComponent>(second);
            const auto* firstJoint = world.GetComponent<JointComponent>(first);
            const auto* secondJoint = world.GetComponent<JointComponent>(second);
            if ((firstJoint && firstJoint->connectedEntity == second && !firstJoint->collideConnected) ||
                (secondJoint && secondJoint->connectedEntity == first && !secondJoint->collideConnected))
                continue;
            if ((firstCollider->mask & secondCollider->layer) == 0 ||
                (secondCollider->mask & firstCollider->layer) == 0)
                continue;
            const Aabb firstBounds = colliders[firstIndex].bounds, secondBounds = colliders[secondIndex].bounds;
            if (!firstBounds.intersects(secondBounds))
                continue;
            if (firstCollider->shape == ColliderShape::TriangleMesh &&
                !meshTrianglesOverlapBounds(acceleration, world, first, secondBounds))
                continue;
            if (secondCollider->shape == ColliderShape::TriangleMesh &&
                !meshTrianglesOverlapBounds(acceleration, world, second, firstBounds))
                continue;
            Vec3 meshContactNormal{};
            float meshPenetration = 0.0f;
            if (firstCollider->shape == ColliderShape::TriangleMesh && secondCollider->shape == ColliderShape::Sphere &&
                !meshSphereContact(acceleration, world, first, second, meshContactNormal, meshPenetration))
                continue;
            if (secondCollider->shape == ColliderShape::TriangleMesh && firstCollider->shape == ColliderShape::Sphere &&
                !meshSphereContact(acceleration, world, second, first, meshContactNormal, meshPenetration))
                continue;
            if ((firstCollider->shape == ColliderShape::ConvexHull ||
                 secondCollider->shape == ColliderShape::ConvexHull) &&
                !convexOverlap(acceleration, world, first, second))
                continue;
            const auto pair = OrderedPair(first, second);
            currentPairs.push_back(pair);
            const bool existed = std::binary_search(m_previousPairs.begin(), m_previousPairs.end(), pair);
            m_events.push_back({first, second, existed ? CollisionEventType::Stay : CollisionEventType::Enter,
                                firstCollider->trigger || secondCollider->trigger});
            if (firstCollider->trigger || secondCollider->trigger)
                continue;

            auto* firstBody = world.GetComponent<RigidBodyComponent>(first);
            auto* secondBody = world.GetComponent<RigidBodyComponent>(second);
            auto* firstTransform = world.GetComponent<TransformComponent>(first);
            auto* secondTransform = world.GetComponent<TransformComponent>(second);
            const float firstInverseMass =
                firstBody && firstBody->type == BodyType::Dynamic ? 1.0f / std::max(0.001f, firstBody->mass) : 0.0f;
            const float secondInverseMass =
                secondBody && secondBody->type == BodyType::Dynamic ? 1.0f / std::max(0.001f, secondBody->mass) : 0.0f;
            const float inverseMassSum = firstInverseMass + secondInverseMass;
            if (inverseMassSum > 0.0f && firstTransform && secondTransform)
            {
                Vec3 normal = meshContactNormal;
                float penetration = meshPenetration;
                const bool detailedContact = (firstCollider->shape == ColliderShape::TriangleMesh &&
                                              secondCollider->shape == ColliderShape::Sphere) ||
                                             (secondCollider->shape == ColliderShape::TriangleMesh &&
                                              firstCollider->shape == ColliderShape::Sphere);
                if (secondCollider->shape == ColliderShape::TriangleMesh &&
                    firstCollider->shape == ColliderShape::Sphere)
                    normal = normal * -1.0f;
                if (!detailedContact)
                {
                    const float overlapX = std::min(firstBounds.maximum.x, secondBounds.maximum.x) -
                                           std::max(firstBounds.minimum.x, secondBounds.minimum.x);
                    const float overlapY = std::min(firstBounds.maximum.y, secondBounds.maximum.y) -
                                           std::max(firstBounds.minimum.y, secondBounds.minimum.y);
                    const float overlapZ = std::min(firstBounds.maximum.z, secondBounds.maximum.z) -
                                           std::max(firstBounds.minimum.z, secondBounds.minimum.z);
                    penetration = overlapX;
                    normal.x = firstTransform->position.x < secondTransform->position.x ? 1.0f : -1.0f;
                    if (overlapY < penetration)
                    {
                        penetration = overlapY;
                        normal = {0.0f, firstTransform->position.y < secondTransform->position.y ? 1.0f : -1.0f, 0.0f};
                    }
                    if (overlapZ < penetration)
                    {
                        penetration = overlapZ;
                        normal = {0.0f, 0.0f, firstTransform->position.z < secondTransform->position.z ? 1.0f : -1.0f};
                    }
                }
                firstTransform->position -= normal * (penetration * firstInverseMass / inverseMassSum);
                secondTransform->position += normal * (penetration * secondInverseMass / inverseMassSum);
                const Vec3 firstVelocity = firstBody ? firstBody->velocity : Vec3{},
                           secondVelocity = secondBody ? secondBody->velocity : Vec3{};
                const float velocityAlongNormal = dot(secondVelocity - firstVelocity, normal);
                if (velocityAlongNormal < 0.0f)
                {
                    const float restitution = std::max(firstBody ? firstBody->restitution : 0.0f,
                                                       secondBody ? secondBody->restitution : 0.0f);
                    const float impulse = -(1.0f + restitution) * velocityAlongNormal / inverseMassSum;
                    if (firstBody && firstInverseMass > 0.0f)
                        firstBody->velocity -= normal * (impulse * firstInverseMass);
                    if (secondBody && secondInverseMass > 0.0f)
                        secondBody->velocity += normal * (impulse * secondInverseMass);
                }
                const Vec3 relative =
                    (secondBody ? secondBody->velocity : Vec3{}) - (firstBody ? firstBody->velocity : Vec3{});
                Vec3 tangent = relative - normal * dot(relative, normal);
                const float tangentLength = length(tangent);
                if (tangentLength > 0.0001f)
                {
                    tangent = tangent / tangentLength;
                    float tangentImpulse = -dot(relative, tangent) / inverseMassSum;
                    const float friction = std::sqrt(std::max(0.0f, firstBody ? firstBody->friction : 0.6f) *
                                                     std::max(0.0f, secondBody ? secondBody->friction : 0.6f));
                    tangentImpulse = std::clamp(tangentImpulse, -friction * tangentLength, friction * tangentLength);
                    if (firstBody && firstInverseMass > 0.0f)
                        firstBody->velocity -= tangent * (tangentImpulse * firstInverseMass);
                    if (secondBody && secondInverseMass > 0.0f)
                        secondBody->velocity += tangent * (tangentImpulse * secondInverseMass);
                }
            }
        }
    }
    world.GetComponentManager().ForEach<JointComponent>(
        [&](const Entity entity, JointComponent& joint)
        {
            auto* transform = world.GetComponent<TransformComponent>(entity);
            auto* connected = world.GetComponent<TransformComponent>(joint.connectedEntity);
            if (!transform || !connected)
                return;
            auto* body = world.GetComponent<RigidBodyComponent>(entity);
            auto* connectedBody = world.GetComponent<RigidBodyComponent>(joint.connectedEntity);
            const float inverseMass =
                body && body->type == BodyType::Dynamic ? 1.0f / std::max(0.001f, body->mass) : 0.0f;
            const float connectedInverseMass = connectedBody && connectedBody->type == BodyType::Dynamic
                                                   ? 1.0f / std::max(0.001f, connectedBody->mass)
                                                   : 0.0f;
            const float sum = inverseMass + connectedInverseMass;
            if (sum <= 0.0f)
                return;
            const Vec3 target =
                connected->position +
                (joint.type == JointType::Fixed || joint.type == JointType::Hinge ? joint.fixedOffset : Vec3{});
            const Vec3 delta = transform->position - target;
            const float distance = length(delta);
            if (distance < 0.0001f)
                return;
            const float desired = joint.type == JointType::Distance || joint.type == JointType::Spring
                                      ? std::max(0.0f, joint.restLength)
                                      : 0.0f;
            const float strength = joint.type == JointType::Spring
                                       ? std::clamp(joint.stiffness * fixedDeltaTime * 10.0f, 0.0f, 1.0f)
                                       : std::clamp(joint.stiffness, 0.0f, 1.0f);
            const Vec3 correction = delta * ((distance - desired) / distance * strength);
            if (joint.breakForce > 0.0f && length(correction) / std::max(fixedDeltaTime, 0.0001f) > joint.breakForce)
            {
                joint.connectedEntity = kInvalidEntity;
                return;
            }
            transform->position -= correction * (inverseMass / sum);
            connected->position += correction * (connectedInverseMass / sum);
            if (body && connectedBody && joint.type == JointType::Spring)
            {
                const Vec3 relativeVelocity = body->velocity - connectedBody->velocity;
                const Vec3 dampingImpulse = relativeVelocity * std::clamp(joint.damping * fixedDeltaTime, 0.0f, 1.0f);
                body->velocity -= dampingImpulse * (inverseMass / sum);
                connectedBody->velocity += dampingImpulse * (connectedInverseMass / sum);
            }
        });
    std::sort(currentPairs.begin(), currentPairs.end());
    for (const auto& oldPair : m_previousPairs)
        if (!std::binary_search(currentPairs.begin(), currentPairs.end(), oldPair))
            m_events.push_back({oldPair.first, oldPair.second, CollisionEventType::Exit, false});
    m_previousPairs = std::move(currentPairs);
    // 调试包围盒只有调试渲染真的在轮询时才值得重建：大场景下这段每步上千次包围盒
    // 计算与哈希集构建不可忽略（Release 同样付费）。
    if (m_debugBoxesRequested)
    {
        m_debugBoxesRequested = false;
        m_debugBoxes.clear();
        std::unordered_set<Entity> collidingEntities;
        for (const auto& pair : m_previousPairs)
        {
            collidingEntities.insert(pair.first);
            collidingEntities.insert(pair.second);
        }
        for (const auto& collider : colliders)
        {
            const auto* component = world.GetComponent<ColliderComponent>(collider.entity);
            m_debugBoxes.push_back({collider.entity, computeBounds(world, collider.entity),
                                    collidingEntities.find(collider.entity) != collidingEntities.end(),
                                    component && component->trigger});
        }
    }
}

bool PhysicsWorld::raycast(const World& world, const Ray& ray, const float maxDistance, RaycastHit& hit,
                           const PhysicsQuery& query) const
{
    auto& acceleration = accelerationState();
    acceleration.beginOperation();
    synchronizeScene(world);
    bool found = false;
    float closest = maxDistance;
    const auto intersects = [&](const Aabb& bounds)
    {
        float distance;
        return rayAabb(ray, bounds, closest, distance);
    };
    const auto inspect = [&](std::uint64_t entity)
    {
        const auto* collider = world.GetComponent<ColliderComponent>(entity);
        if (entity == query.ignoredEntity || (!query.includeTriggers && collider->trigger) ||
            (collider->layer & query.layerMask) == 0)
            return true;
        float distance = closest;
        Vec3 normal;
        const bool meshShape =
            collider->shape == ColliderShape::TriangleMesh || collider->shape == ColliderShape::ConvexHull;
        const bool intersected = meshShape ? rayMesh(acceleration, world, entity, ray, closest, distance, normal)
                                           : rayAabb(ray, computeBounds(world, entity), closest, distance, &normal);
        if (intersected && distance < closest)
        {
            closest = distance;
            hit = {entity, ray.origin + ray.direction * distance, normal, distance};
            found = true;
        }
        return true;
    };
    acceleration.staticTree.visit(intersects, inspect, acceleration.statistics.sceneBoundsTests);
    acceleration.dynamicTree.visit(intersects, inspect, acceleration.statistics.sceneBoundsTests);
    return found;
}

// Sweep 返回沿位移最早的命中；零位移与退化形状都要在进入窄相位前处理。
bool PhysicsWorld::shapeSweep(const World& world, const ShapeSweepGeometry& geometry, const Vec3& origin,
                              const Vec3& displacement, RaycastHit& hit, const PhysicsQuery& query) const
{
    auto& acceleration = accelerationState();
    acceleration.beginOperation();
    const float distance = length(displacement);
    if (distance <= 0.000001f)
        return false;
    float radius = geometry.radius;
    if (geometry.shape == ColliderShape::Box)
        radius = length(geometry.halfExtents);
    else if (geometry.shape == ColliderShape::Capsule)
        radius = geometry.radius + geometry.height * 0.5f;
    else if ((geometry.shape == ColliderShape::ConvexHull || geometry.shape == ColliderShape::TriangleMesh) &&
             !geometry.meshPath.empty())
    {
        if (const auto mesh = acceleration.meshFor(geometry.meshPath))
            radius = length((mesh->bounds.maximum - mesh->bounds.minimum) * 0.5f);
    }
    return sphereCast(world, {origin, displacement / distance}, std::max(0.001f, radius), distance, hit, query);
}

bool PhysicsWorld::sphereCast(const World& world, const Ray& ray, const float radius, const float maxDistance,
                              RaycastHit& hit, const PhysicsQuery& query) const
{
    auto& acceleration = accelerationState();
    acceleration.beginOperation();
    synchronizeScene(world);
    bool found = false;
    float closest = maxDistance;
    const Vec3 extent{radius, radius, radius};
    const auto intersects = [&](const Aabb& bounds)
    {
        float distance;
        return rayAabb(ray, {bounds.minimum - extent, bounds.maximum + extent}, closest, distance);
    };
    const auto inspect = [&](std::uint64_t entity)
    {
        const auto* collider = world.GetComponent<ColliderComponent>(entity);
        if (entity == query.ignoredEntity || (!query.includeTriggers && collider->trigger) ||
            (collider->layer & query.layerMask) == 0)
            return true;
        Aabb bounds = computeBounds(world, entity);
        bounds.minimum -= extent;
        bounds.maximum += extent;
        float distance = 0.0f;
        Vec3 normal;
        if (rayAabb(ray, bounds, closest, distance, &normal) && distance < closest)
        {
            closest = distance;
            hit = {entity, ray.origin + ray.direction * distance, normal, distance};
            found = true;
        }
        return true;
    };
    acceleration.staticTree.visit(intersects, inspect, acceleration.statistics.sceneBoundsTests);
    acceleration.dynamicTree.visit(intersects, inspect, acceleration.statistics.sceneBoundsTests);
    return found;
}

std::vector<Entity> PhysicsWorld::Overlap(const World& world, const Aabb& bounds, const PhysicsQuery& query) const
{
    auto& acceleration = accelerationState();
    acceleration.beginOperation();
    synchronizeScene(world);
    std::vector<Entity> result;
    const auto inspect = [&](std::uint64_t entity)
    {
        const auto* collider = world.GetComponent<ColliderComponent>(entity);
        if (entity != query.ignoredEntity && (query.includeTriggers || !collider->trigger) &&
            (collider->layer & query.layerMask) != 0)
            result.push_back(entity);
        return true;
    };
    const auto intersects = [&](const Aabb& other) { return bounds.intersects(other); };
    acceleration.staticTree.visit(intersects, inspect, acceleration.statistics.sceneBoundsTests);
    acceleration.dynamicTree.visit(intersects, inspect, acceleration.statistics.sceneBoundsTests);
    return result;
}

Vec3 PhysicsWorld::moveCharacter(World& world, const Entity entity, const Vec3& displacement,
                                 const float skinWidth) const
{
    auto& acceleration = accelerationState();
    acceleration.beginOperation();
    auto* transform = world.GetComponent<TransformComponent>(entity);
    if (!transform || !world.HasComponent<ColliderComponent>(entity))
        return {};
    const Vec3 start = transform->position;
    const float values[3] = {displacement.x, displacement.y, displacement.z};
    for (int axis = 0; axis < 3; ++axis)
    {
        const float old = axis == 0 ? transform->position.x : axis == 1 ? transform->position.y : transform->position.z;
        if (axis == 0)
            transform->position.x += values[axis];
        else if (axis == 1)
            transform->position.y += values[axis];
        else
            transform->position.z += values[axis];
        Aabb bounds = computeBounds(world, entity);
        bounds.minimum += Vec3{skinWidth, skinWidth, skinWidth};
        bounds.maximum -= Vec3{skinWidth, skinWidth, skinWidth};
        if (!Overlap(world, bounds, {0xffffffffu, false, entity}).empty())
        {
            if (axis == 0)
                transform->position.x = old;
            else if (axis == 1)
                transform->position.y = old;
            else
                transform->position.z = old;
        }
    }
    return transform->position - start;
}

CharacterMoveResult PhysicsWorld::moveCharacterController(World& world, const Entity entity, const Vec3& displacement,
                                                          const float stepHeight, const float skinWidth,
                                                          const float maximumSlopeDegrees) const
{
    CharacterMoveResult result;
    auto* transform = world.GetComponent<TransformComponent>(entity);
    const auto* collider = world.GetComponent<ColliderComponent>(entity);
    if (!transform || !collider)
        return result;
    const Vec3 start = transform->position;
    const Vec3 horizontal{displacement.x, 0.0f, displacement.z};
    Vec3 horizontalApplied = moveCharacter(world, entity, horizontal, skinWidth);
    const bool blocked = std::abs(horizontalApplied.x - horizontal.x) > 0.0001f ||
                         std::abs(horizontalApplied.z - horizontal.z) > 0.0001f;
    if (blocked && stepHeight > 0.0f)
    {
        transform->position = start;
        const Vec3 raised = moveCharacter(world, entity, {0.0f, stepHeight, 0.0f}, skinWidth);
        const Vec3 steppedHorizontal = moveCharacter(world, entity, horizontal, skinWidth);
        if (raised.y > stepHeight * 0.9f && std::abs(steppedHorizontal.x - horizontal.x) < 0.0001f &&
            std::abs(steppedHorizontal.z - horizontal.z) < 0.0001f)
        {
            float remainingDrop = stepHeight;
            const float dropIncrement = std::max(0.01f, skinWidth);
            while (remainingDrop > 0.0001f)
            {
                const float drop = std::min(dropIncrement, remainingDrop);
                if (moveCharacter(world, entity, {0.0f, -drop, 0.0f}, skinWidth).y > -drop + 0.0001f)
                    break;
                remainingDrop -= drop;
            }
            horizontalApplied = transform->position - start;
            result.stepped = true;
        }
        else
        {
            transform->position = start;
            horizontalApplied = moveCharacter(world, entity, horizontal, skinWidth);
        }
    }
    const Vec3 verticalApplied = moveCharacter(world, entity, {0.0f, displacement.y, 0.0f}, skinWidth);
    result.hitCeiling = displacement.y > 0.0f && verticalApplied.y < displacement.y - 0.0001f;
    result.appliedDisplacement = transform->position - start;
    const float probeRadius = collider->shape == ColliderShape::Sphere || collider->shape == ColliderShape::Capsule
                                  ? collider->radius
                                  : std::max(0.05f, std::min(collider->halfExtents.x, collider->halfExtents.z));
    Vec3 probeOrigin = transform->position;
    if (collider->shape == ColliderShape::Capsule)
        probeOrigin.y -= collider->height * 0.5f;
    else if (collider->shape == ColliderShape::Box)
        probeOrigin.y -= std::max(0.0f, collider->halfExtents.y - probeRadius * 0.9f);
    RaycastHit groundHit;
    result.grounded = sphereCast(world, {probeOrigin, {0.0f, -1.0f, 0.0f}}, probeRadius * 0.9f,
                                 std::max(0.08f, skinWidth * 4.0f), groundHit, {0xffffffffu, false, entity});
    if (result.grounded)
    {
        result.groundNormal = groundHit.normal;
        const float minimumWalkableY = std::cos(degreesToRadians(std::clamp(maximumSlopeDegrees, 0.0f, 89.0f)));
        if (result.groundNormal.y < minimumWalkableY)
            result.grounded = false;
    }
    if (displacement.y < 0.0f && verticalApplied.y > displacement.y + 0.0001f)
        result.grounded = true;
    return result;
}

const std::vector<CollisionEvent>& PhysicsWorld::GetEvents() const
{
    return m_events;
}
const std::vector<PhysicsDebugBox>& PhysicsWorld::GetDebugBoxes() const
{
    m_debugBoxesRequested = true;
    return m_debugBoxes;
}
PhysicsStatistics PhysicsWorld::GetStatistics() const
{
    return m_acceleration ? m_acceleration->statistics : PhysicsStatistics{};
}
void PhysicsWorld::clear()
{
    m_acceleration.reset();
    m_events.clear();
    m_previousPairs.clear();
    m_debugBoxes.clear();
}
bool PhysicsWorld::setBackend(const PhysicsBackend backend)
{
    if (!isBackendAvailable(backend))
        return false;
    m_backend = backend;
    return true;
}
PhysicsBackend PhysicsWorld::getBackend() const
{
    return m_backend;
}
bool PhysicsWorld::isBackendAvailable(const PhysicsBackend backend)
{
    if (backend == PhysicsBackend::Builtin)
        return true;
#ifdef TINYENGINE_HAS_JOLT
    return true;
#else
    return false;
#endif
}
} // namespace tinyengine
