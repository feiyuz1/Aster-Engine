// 文件：src/Physics/PhysicsWorld.cpp
// 作用：管理碰撞体、刚体仿真、射线/形状查询和角色控制器。
// 约定：仿真使用固定时间步；编辑器帧率不能直接改变物理积分步长。
// 类型：实现文件。

#include "MiniEngine/Physics/PhysicsWorld.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/Resource/Resource.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace MiniEngine
{
namespace
{
std::pair<Entity, Entity> OrderedPair(Entity a, Entity b)
{
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}
bool RayAabb(const Ray &ray, const Aabb &bounds, const float maxDistance, float &distance, Vec3 *hitNormal = nullptr)
{
    float tMin = 0.0f, tMax = maxDistance;
    Vec3 normal{};
    for (int axis = 0; axis < 3; ++axis)
    {
        const float origin = axis == 0 ? ray.origin.x : axis == 1 ? ray.origin.y : ray.origin.z;
        const float direction = axis == 0 ? ray.direction.x : axis == 1 ? ray.direction.y : ray.direction.z;
        const float minimum = axis == 0 ? bounds.minimum.x : axis == 1 ? bounds.minimum.y : bounds.minimum.z;
        const float maximum = axis == 0 ? bounds.maximum.x : axis == 1 ? bounds.maximum.y : bounds.maximum.z;
        if (std::abs(direction) < 1e-6f)
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
Vec3 TransformPoint(const Mat4 &matrix, const Vec3 &point)
{
    return {matrix.At(0, 0) * point.x + matrix.At(0, 1) * point.y + matrix.At(0, 2) * point.z + matrix.At(0, 3),
            matrix.At(1, 0) * point.x + matrix.At(1, 1) * point.y + matrix.At(1, 2) * point.z + matrix.At(1, 3),
            matrix.At(2, 0) * point.x + matrix.At(2, 1) * point.y + matrix.At(2, 2) * point.z + matrix.At(2, 3)};
}
bool RayTriangle(const Ray &ray, const Vec3 &a, const Vec3 &b, const Vec3 &c, const float maximum, float &distance, Vec3 &normal)
{
    const Vec3 edge1 = b - a, edge2 = c - a, p = Cross(ray.direction, edge2);
    const float determinant = Dot(edge1, p);
    if (std::abs(determinant) < 1e-7f)
        return false;
    const float inverse = 1.0f / determinant;
    const Vec3 t = ray.origin - a;
    const float u = Dot(t, p) * inverse;
    if (u < 0.0f || u > 1.0f)
        return false;
    const Vec3 q = Cross(t, edge1);
    const float v = Dot(ray.direction, q) * inverse;
    if (v < 0.0f || u + v > 1.0f)
        return false;
    distance = Dot(edge2, q) * inverse;
    if (distance < 0.0f || distance > maximum)
        return false;
    normal = Normalize(Cross(edge1, edge2));
    if (Dot(normal, ray.direction) > 0.0f)
        normal = normal * -1.0f;
    return true;
}

bool RayMesh(const World &world, const Entity entity, const Ray &ray, const float maximum, float &closest, Vec3 &hitNormal)
{
    const auto *collider = world.GetComponent<ColliderComponent>(entity);
    const auto *transform = world.GetComponent<TransformComponent>(entity);
    if (!collider || !transform || collider->meshPath.empty())
        return false;
    MeshResource mesh;
    if (!mesh.Load(collider->meshPath))
        return false;
    const Mat4 matrix = TransformMatrix(transform->position, transform->rotation, transform->scale);
    bool found = false;
    for (std::size_t index = 0; index + 2 < mesh.GetIndices().size(); index += 3)
    {
        const auto ia = mesh.GetIndices()[index], ib = mesh.GetIndices()[index + 1], ic = mesh.GetIndices()[index + 2];
        if (ia >= mesh.GetVertices().size() || ib >= mesh.GetVertices().size() || ic >= mesh.GetVertices().size())
            continue;
        float distance = 0.0f;
        Vec3 normal;
        if (RayTriangle(ray, TransformPoint(matrix, mesh.GetVertices()[ia].position),
                        TransformPoint(matrix, mesh.GetVertices()[ib].position), TransformPoint(matrix, mesh.GetVertices()[ic].position),
                        std::min(maximum, closest), distance, normal))
        {
            closest = distance;
            hitNormal = normal;
            found = true;
        }
    }
    return found;
}
bool MeshTrianglesOverlapBounds(const World &world, const Entity meshEntity, const Aabb &otherBounds)
{
    const auto *collider = world.GetComponent<ColliderComponent>(meshEntity);
    const auto *transform = world.GetComponent<TransformComponent>(meshEntity);
    if (!collider || !transform || collider->meshPath.empty())
        return false;
    MeshResource mesh;
    if (!mesh.Load(collider->meshPath))
        return false;
    const Mat4 matrix = TransformMatrix(transform->position, transform->rotation, transform->scale);
    for (std::size_t index = 0; index + 2 < mesh.GetIndices().size(); index += 3)
    {
        const auto ia = mesh.GetIndices()[index], ib = mesh.GetIndices()[index + 1], ic = mesh.GetIndices()[index + 2];
        if (ia >= mesh.GetVertices().size() || ib >= mesh.GetVertices().size() || ic >= mesh.GetVertices().size())
            continue;
        const Vec3 a = TransformPoint(matrix, mesh.GetVertices()[ia].position);
        const Vec3 b = TransformPoint(matrix, mesh.GetVertices()[ib].position);
        const Vec3 c = TransformPoint(matrix, mesh.GetVertices()[ic].position);
        const Aabb triangleBounds{{std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})},
                                  {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})}};
        if (triangleBounds.Intersects(otherBounds))
            return true;
    }
    return false;
}
Vec3 ShapeSupport(const World &world, const Entity entity, const Vec3 &direction)
{
    const auto *collider = world.GetComponent<ColliderComponent>(entity);
    const auto *transform = world.GetComponent<TransformComponent>(entity);
    if (!collider || !transform)
        return {};
    const Vec3 normalized = LengthSquared(direction) > 1e-10f ? Normalize(direction) : Vec3{1.0f, 0.0f, 0.0f};
    const Mat4 matrix = TransformMatrix(transform->position, transform->rotation, transform->scale);
    if (collider->shape == ColliderShape::Sphere)
    {
        const float scale = std::max({std::abs(transform->scale.x), std::abs(transform->scale.y), std::abs(transform->scale.z)});
        return transform->position + normalized * (collider->radius * scale);
    }
    if (collider->shape == ColliderShape::Capsule)
    {
        const Vec3 top = TransformPoint(matrix, {0.0f, collider->height * 0.5f, 0.0f});
        const Vec3 bottom = TransformPoint(matrix, {0.0f, -collider->height * 0.5f, 0.0f});
        const Vec3 end = Dot(top, direction) > Dot(bottom, direction) ? top : bottom;
        const float scale = std::max(std::abs(transform->scale.x), std::abs(transform->scale.z));
        return end + normalized * (collider->radius * scale);
    }
    std::vector<Vec3> points;
    if (collider->shape == ColliderShape::ConvexHull && !collider->meshPath.empty())
    {
        MeshResource mesh;
        if (mesh.Load(collider->meshPath))
        {
            points.reserve(mesh.GetVertices().size());
            for (const auto &vertex : mesh.GetVertices())
                points.push_back(TransformPoint(matrix, vertex.position));
        }
    }
    if (points.empty())
        for (int x = -1; x <= 1; x += 2)
            for (int y = -1; y <= 1; y += 2)
                for (int z = -1; z <= 1; z += 2)
                    points.push_back(
                        TransformPoint(matrix, {collider->halfExtents.x * x, collider->halfExtents.y * y, collider->halfExtents.z * z}));
    return *std::max_element(points.begin(), points.end(),
                             [&](const Vec3 &left, const Vec3 &right) { return Dot(left, direction) < Dot(right, direction); });
}

Vec3 MinkowskiSupport(const World &world, const Entity first, const Entity second, const Vec3 &direction)
{
    return ShapeSupport(world, first, direction) - ShapeSupport(world, second, direction * -1.0f);
}

Vec3 TripleCross(const Vec3 &a, const Vec3 &b, const Vec3 &c)
{
    return Cross(Cross(a, b), c);
}

bool UpdateSimplex(std::vector<Vec3> &simplex, Vec3 &direction)
{
    const Vec3 a = simplex.back(), ao = a * -1.0f;
    if (simplex.size() == 2)
    {
        const Vec3 b = simplex[0], ab = b - a;
        if (Dot(ab, ao) > 0.0f)
            direction = TripleCross(ab, ao, ab);
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
        const Vec3 abc = Cross(ab, ac);
        if (Dot(Cross(abc, ac), ao) > 0.0f)
        {
            if (Dot(ac, ao) > 0.0f)
            {
                simplex = {c, a};
                direction = TripleCross(ac, ao, ac);
            }
            else
            {
                simplex = {b, a};
                direction = Dot(ab, ao) > 0.0f ? TripleCross(ab, ao, ab) : ao;
                if (Dot(ab, ao) <= 0.0f)
                    simplex = {a};
            }
        }
        else if (Dot(Cross(ab, abc), ao) > 0.0f)
        {
            simplex = {b, a};
            direction = Dot(ab, ao) > 0.0f ? TripleCross(ab, ao, ab) : ao;
            if (Dot(ab, ao) <= 0.0f)
                simplex = {a};
        }
        else if (Dot(abc, ao) > 0.0f)
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
    const Vec3 abc = Cross(ab, ac), acd = Cross(ac, ad), adb = Cross(ad, ab);
    if (Dot(abc, ao) > 0.0f)
    {
        simplex = {c, b, a};
        direction = abc;
        return false;
    }
    if (Dot(acd, ao) > 0.0f)
    {
        simplex = {d, c, a};
        direction = acd;
        return false;
    }
    if (Dot(adb, ao) > 0.0f)
    {
        simplex = {b, d, a};
        direction = adb;
        return false;
    }
    return true;
}

bool ConvexOverlap(const World &world, const Entity first, const Entity second)
{
    const auto *firstTransform = world.GetComponent<TransformComponent>(first);
    const auto *secondTransform = world.GetComponent<TransformComponent>(second);
    Vec3 direction = firstTransform && secondTransform ? secondTransform->position - firstTransform->position : Vec3{1.0f, 0.0f, 0.0f};
    if (LengthSquared(direction) < 1e-8f)
        direction = {1.0f, 0.0f, 0.0f};
    std::vector<Vec3> simplex{MinkowskiSupport(world, first, second, direction)};
    direction = simplex.front() * -1.0f;
    for (int iteration = 0; iteration < 32; ++iteration)
    {
        const Vec3 point = MinkowskiSupport(world, first, second, direction);
        if (Dot(point, direction) < 0.0f)
            return false;
        simplex.push_back(point);
        if (UpdateSimplex(simplex, direction))
            return true;
        if (LengthSquared(direction) < 1e-10f)
            return true;
    }
    return false;
}
} // namespace

Aabb PhysicsWorld::ComputeBounds(const World &world, const Entity entity)
{
    const auto *transform = world.GetComponent<TransformComponent>(entity);
    const auto *collider = world.GetComponent<ColliderComponent>(entity);
    if (!transform || !collider)
        return {};
    Vec3 center = transform->position;
    Vec3 extent = collider->shape == ColliderShape::Box
                      ? collider->halfExtents
                      : Vec3{collider->radius,
                             collider->shape == ColliderShape::Capsule ? collider->height * 0.5f + collider->radius : collider->radius,
                             collider->radius};
    if ((collider->shape == ColliderShape::ConvexHull || collider->shape == ColliderShape::TriangleMesh) && !collider->meshPath.empty())
    {
        MeshResource mesh;
        if (mesh.Load(collider->meshPath))
        {
            const Aabb &bounds = mesh.GetBounds();
            const Vec3 localCenter = (bounds.minimum + bounds.maximum) * 0.5f;
            extent = (bounds.maximum - bounds.minimum) * 0.5f;
            center += Vec3{localCenter.x * transform->scale.x, localCenter.y * transform->scale.y, localCenter.z * transform->scale.z};
        }
    }
    extent = {extent.x * std::abs(transform->scale.x), extent.y * std::abs(transform->scale.y), extent.z * std::abs(transform->scale.z)};
    return {center - extent, center + extent};
}

// 先积分动态刚体，再生成接触并修正穿透，最后与上一帧接触集合比较得到 Enter/Stay/Exit。
void PhysicsWorld::Step(World &world, const float fixedDeltaTime)
{
    m_events.clear();
    m_debugBoxes.clear();
    world.GetComponentManager().ForEach<TransformComponent, RigidBodyComponent>(
        [fixedDeltaTime](Entity, TransformComponent &transform, RigidBodyComponent &body) {
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
            const int substeps =
                body.continuous ? std::clamp(static_cast<int>(std::ceil(Length(body.velocity) * fixedDeltaTime / 0.1f)), 1, 16) : 1;
            for (int step = 0; step < substeps; ++step)
                transform.position += body.velocity * (fixedDeltaTime / static_cast<float>(substeps));
        });

    struct BroadphaseEntry
    {
        Entity entity;
        Aabb bounds;
    };
    std::vector<BroadphaseEntry> colliders;
    world.GetComponentManager().ForEach<ColliderComponent>(
        [&](Entity entity, ColliderComponent &) { colliders.push_back({entity, ComputeBounds(world, entity)}); });
    std::sort(colliders.begin(), colliders.end(),
              [](const BroadphaseEntry &a, const BroadphaseEntry &b) { return a.bounds.minimum.x < b.bounds.minimum.x; });
    std::vector<std::pair<Entity, Entity>> currentPairs;
    for (std::size_t firstIndex = 0; firstIndex < colliders.size(); ++firstIndex)
    {
        for (std::size_t secondIndex = firstIndex + 1; secondIndex < colliders.size(); ++secondIndex)
        {
            if (colliders[secondIndex].bounds.minimum.x > colliders[firstIndex].bounds.maximum.x)
                break;
            const Entity first = colliders[firstIndex].entity, second = colliders[secondIndex].entity;
            const auto *firstCollider = world.GetComponent<ColliderComponent>(first);
            const auto *secondCollider = world.GetComponent<ColliderComponent>(second);
            const auto *firstJoint = world.GetComponent<JointComponent>(first);
            const auto *secondJoint = world.GetComponent<JointComponent>(second);
            if ((firstJoint && firstJoint->connectedEntity == second && !firstJoint->collideConnected) ||
                (secondJoint && secondJoint->connectedEntity == first && !secondJoint->collideConnected))
                continue;
            if ((firstCollider->mask & secondCollider->layer) == 0 || (secondCollider->mask & firstCollider->layer) == 0)
                continue;
            const Aabb firstBounds = colliders[firstIndex].bounds, secondBounds = colliders[secondIndex].bounds;
            if (!firstBounds.Intersects(secondBounds))
                continue;
            if (firstCollider->shape == ColliderShape::TriangleMesh && !MeshTrianglesOverlapBounds(world, first, secondBounds))
                continue;
            if (secondCollider->shape == ColliderShape::TriangleMesh && !MeshTrianglesOverlapBounds(world, second, firstBounds))
                continue;
            if ((firstCollider->shape == ColliderShape::ConvexHull || secondCollider->shape == ColliderShape::ConvexHull) &&
                !ConvexOverlap(world, first, second))
                continue;
            const auto pair = OrderedPair(first, second);
            currentPairs.push_back(pair);
            const bool existed = std::find(m_previousPairs.begin(), m_previousPairs.end(), pair) != m_previousPairs.end();
            m_events.push_back({first, second, existed ? CollisionEventType::Stay : CollisionEventType::Enter,
                                firstCollider->trigger || secondCollider->trigger});
            if (firstCollider->trigger || secondCollider->trigger)
                continue;

            auto *firstBody = world.GetComponent<RigidBodyComponent>(first);
            auto *secondBody = world.GetComponent<RigidBodyComponent>(second);
            auto *firstTransform = world.GetComponent<TransformComponent>(first);
            auto *secondTransform = world.GetComponent<TransformComponent>(second);
            const float firstInverseMass =
                firstBody && firstBody->type == BodyType::Dynamic ? 1.0f / std::max(0.001f, firstBody->mass) : 0.0f;
            const float secondInverseMass =
                secondBody && secondBody->type == BodyType::Dynamic ? 1.0f / std::max(0.001f, secondBody->mass) : 0.0f;
            const float inverseMassSum = firstInverseMass + secondInverseMass;
            if (inverseMassSum > 0.0f && firstTransform && secondTransform)
            {
                const float overlapX =
                    std::min(firstBounds.maximum.x, secondBounds.maximum.x) - std::max(firstBounds.minimum.x, secondBounds.minimum.x);
                const float overlapY =
                    std::min(firstBounds.maximum.y, secondBounds.maximum.y) - std::max(firstBounds.minimum.y, secondBounds.minimum.y);
                const float overlapZ =
                    std::min(firstBounds.maximum.z, secondBounds.maximum.z) - std::max(firstBounds.minimum.z, secondBounds.minimum.z);
                Vec3 normal{};
                float penetration = overlapX;
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
                firstTransform->position -= normal * (penetration * firstInverseMass / inverseMassSum);
                secondTransform->position += normal * (penetration * secondInverseMass / inverseMassSum);
                const Vec3 firstVelocity = firstBody ? firstBody->velocity : Vec3{},
                           secondVelocity = secondBody ? secondBody->velocity : Vec3{};
                const float velocityAlongNormal = Dot(secondVelocity - firstVelocity, normal);
                if (velocityAlongNormal < 0.0f)
                {
                    const float restitution =
                        std::max(firstBody ? firstBody->restitution : 0.0f, secondBody ? secondBody->restitution : 0.0f);
                    const float impulse = -(1.0f + restitution) * velocityAlongNormal / inverseMassSum;
                    if (firstBody && firstInverseMass > 0.0f)
                        firstBody->velocity -= normal * (impulse * firstInverseMass);
                    if (secondBody && secondInverseMass > 0.0f)
                        secondBody->velocity += normal * (impulse * secondInverseMass);
                }
                const Vec3 relative = (secondBody ? secondBody->velocity : Vec3{}) - (firstBody ? firstBody->velocity : Vec3{});
                Vec3 tangent = relative - normal * Dot(relative, normal);
                const float tangentLength = Length(tangent);
                if (tangentLength > 0.0001f)
                {
                    tangent = tangent / tangentLength;
                    float tangentImpulse = -Dot(relative, tangent) / inverseMassSum;
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
    world.GetComponentManager().ForEach<JointComponent>([&](const Entity entity, JointComponent &joint) {
        auto *transform = world.GetComponent<TransformComponent>(entity);
        auto *connected = world.GetComponent<TransformComponent>(joint.connectedEntity);
        if (!transform || !connected)
            return;
        auto *body = world.GetComponent<RigidBodyComponent>(entity);
        auto *connectedBody = world.GetComponent<RigidBodyComponent>(joint.connectedEntity);
        const float inverseMass = body && body->type == BodyType::Dynamic ? 1.0f / std::max(0.001f, body->mass) : 0.0f;
        const float connectedInverseMass =
            connectedBody && connectedBody->type == BodyType::Dynamic ? 1.0f / std::max(0.001f, connectedBody->mass) : 0.0f;
        const float sum = inverseMass + connectedInverseMass;
        if (sum <= 0.0f)
            return;
        const Vec3 target =
            connected->position + (joint.type == JointType::Fixed || joint.type == JointType::Hinge ? joint.fixedOffset : Vec3{});
        const Vec3 delta = transform->position - target;
        const float distance = Length(delta);
        if (distance < 0.0001f)
            return;
        const float desired =
            joint.type == JointType::Distance || joint.type == JointType::Spring ? std::max(0.0f, joint.restLength) : 0.0f;
        const float strength = joint.type == JointType::Spring ? std::clamp(joint.stiffness * fixedDeltaTime * 10.0f, 0.0f, 1.0f)
                                                               : std::clamp(joint.stiffness, 0.0f, 1.0f);
        const Vec3 correction = delta * ((distance - desired) / distance * strength);
        if (joint.breakForce > 0.0f && Length(correction) / std::max(fixedDeltaTime, 0.0001f) > joint.breakForce)
        {
            joint.connectedEntity = InvalidEntity;
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
    for (const auto &oldPair : m_previousPairs)
        if (std::find(currentPairs.begin(), currentPairs.end(), oldPair) == currentPairs.end())
            m_events.push_back({oldPair.first, oldPair.second, CollisionEventType::Exit, false});
    m_previousPairs = std::move(currentPairs);
    for (const auto &collider : colliders)
    {
        const bool colliding = std::any_of(m_previousPairs.begin(), m_previousPairs.end(), [&](const auto &pair) {
            return pair.first == collider.entity || pair.second == collider.entity;
        });
        const auto *component = world.GetComponent<ColliderComponent>(collider.entity);
        m_debugBoxes.push_back({collider.entity, ComputeBounds(world, collider.entity), colliding, component && component->trigger});
    }
}

bool PhysicsWorld::Raycast(const World &world, const Ray &ray, const float maxDistance, RaycastHit &hit, const PhysicsQuery &query) const
{
    bool found = false;
    float closest = maxDistance;
    for (const Entity entity : world.GetEntityManager().GetAliveEntities())
    {
        const auto *collider = world.GetComponent<ColliderComponent>(entity);
        if (!collider || entity == query.ignoredEntity || (!query.includeTriggers && collider->trigger) ||
            (collider->layer & query.layerMask) == 0)
            continue;
        const Aabb bounds = ComputeBounds(world, entity);
        float distance = closest;
        Vec3 normal;
        const bool meshShape = collider->shape == ColliderShape::TriangleMesh || collider->shape == ColliderShape::ConvexHull;
        const bool intersected =
            meshShape ? RayMesh(world, entity, ray, maxDistance, distance, normal) : RayAabb(ray, bounds, closest, distance, &normal);
        if (intersected && distance < closest)
        {
            closest = distance;
            hit.entity = entity;
            hit.distance = distance;
            hit.point = ray.origin + ray.direction * distance;
            hit.normal = normal;
            found = true;
        }
    }
    return found;
}

// Sweep 返回沿位移最早的命中；零位移与退化形状都要在进入窄相位前处理。
bool PhysicsWorld::ShapeSweep(const World &world, const ShapeSweepGeometry &geometry, const Vec3 &origin, const Vec3 &displacement,
                              RaycastHit &hit, const PhysicsQuery &query) const
{
    const float distance = Length(displacement);
    if (distance <= 0.000001f)
        return false;
    float radius = geometry.radius;
    if (geometry.shape == ColliderShape::Box)
        radius = Length(geometry.halfExtents);
    else if (geometry.shape == ColliderShape::Capsule)
        radius = geometry.radius + geometry.height * 0.5f;
    else if ((geometry.shape == ColliderShape::ConvexHull || geometry.shape == ColliderShape::TriangleMesh) && !geometry.meshPath.empty())
    {
        MeshResource mesh;
        if (mesh.Load(geometry.meshPath))
            radius = Length((mesh.GetBounds().maximum - mesh.GetBounds().minimum) * 0.5f);
    }
    return SphereCast(world, {origin, displacement / distance}, std::max(0.001f, radius), distance, hit, query);
}

bool PhysicsWorld::SphereCast(const World &world, const Ray &ray, const float radius, const float maxDistance, RaycastHit &hit,
                              const PhysicsQuery &query) const
{
    bool found = false;
    float closest = maxDistance;
    for (const Entity entity : world.GetEntityManager().GetAliveEntities())
    {
        const auto *collider = world.GetComponent<ColliderComponent>(entity);
        if (!collider || entity == query.ignoredEntity || (!query.includeTriggers && collider->trigger) ||
            (collider->layer & query.layerMask) == 0)
            continue;
        Aabb bounds = ComputeBounds(world, entity);
        const Vec3 extent{radius, radius, radius};
        bounds.minimum -= extent;
        bounds.maximum += extent;
        float distance = 0.0f;
        Vec3 normal;
        if (RayAabb(ray, bounds, closest, distance, &normal) && distance < closest)
        {
            closest = distance;
            hit = {entity, ray.origin + ray.direction * distance, normal, distance};
            found = true;
        }
    }
    return found;
}

std::vector<Entity> PhysicsWorld::Overlap(const World &world, const Aabb &bounds, const PhysicsQuery &query) const
{
    std::vector<Entity> result;
    for (const Entity entity : world.GetEntityManager().GetAliveEntities())
    {
        const auto *collider = world.GetComponent<ColliderComponent>(entity);
        if (!collider || entity == query.ignoredEntity || (!query.includeTriggers && collider->trigger) ||
            (collider->layer & query.layerMask) == 0)
            continue;
        if (bounds.Intersects(ComputeBounds(world, entity)))
            result.push_back(entity);
    }
    return result;
}

Vec3 PhysicsWorld::MoveCharacter(World &world, const Entity entity, const Vec3 &displacement, const float skinWidth) const
{
    auto *transform = world.GetComponent<TransformComponent>(entity);
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
        Aabb bounds = ComputeBounds(world, entity);
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

CharacterMoveResult PhysicsWorld::MoveCharacterController(World &world, const Entity entity, const Vec3 &displacement,
                                                          const float stepHeight, const float skinWidth,
                                                          const float maximumSlopeDegrees) const
{
    CharacterMoveResult result;
    auto *transform = world.GetComponent<TransformComponent>(entity);
    const auto *collider = world.GetComponent<ColliderComponent>(entity);
    if (!transform || !collider)
        return result;
    const Vec3 start = transform->position;
    const Vec3 horizontal{displacement.x, 0.0f, displacement.z};
    Vec3 horizontalApplied = MoveCharacter(world, entity, horizontal, skinWidth);
    const bool blocked = std::abs(horizontalApplied.x - horizontal.x) > 0.0001f || std::abs(horizontalApplied.z - horizontal.z) > 0.0001f;
    if (blocked && stepHeight > 0.0f)
    {
        transform->position = start;
        const Vec3 raised = MoveCharacter(world, entity, {0.0f, stepHeight, 0.0f}, skinWidth);
        const Vec3 steppedHorizontal = MoveCharacter(world, entity, horizontal, skinWidth);
        if (raised.y > stepHeight * 0.9f && std::abs(steppedHorizontal.x - horizontal.x) < 0.0001f &&
            std::abs(steppedHorizontal.z - horizontal.z) < 0.0001f)
        {
            float remainingDrop = stepHeight;
            const float dropIncrement = std::max(0.01f, skinWidth);
            while (remainingDrop > 0.0001f)
            {
                const float drop = std::min(dropIncrement, remainingDrop);
                if (MoveCharacter(world, entity, {0.0f, -drop, 0.0f}, skinWidth).y > -drop + 0.0001f)
                    break;
                remainingDrop -= drop;
            }
            horizontalApplied = transform->position - start;
            result.stepped = true;
        }
        else
        {
            transform->position = start;
            horizontalApplied = MoveCharacter(world, entity, horizontal, skinWidth);
        }
    }
    const Vec3 verticalApplied = MoveCharacter(world, entity, {0.0f, displacement.y, 0.0f}, skinWidth);
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
    result.grounded = SphereCast(world, {probeOrigin, {0.0f, -1.0f, 0.0f}}, probeRadius * 0.9f, std::max(0.08f, skinWidth * 4.0f),
                                 groundHit, {0xffffffffu, false, entity});
    if (result.grounded)
    {
        result.groundNormal = groundHit.normal;
        const float minimumWalkableY = std::cos(DegreesToRadians(std::clamp(maximumSlopeDegrees, 0.0f, 89.0f)));
        if (result.groundNormal.y < minimumWalkableY)
            result.grounded = false;
    }
    if (displacement.y < 0.0f && verticalApplied.y > displacement.y + 0.0001f)
        result.grounded = true;
    return result;
}

const std::vector<CollisionEvent> &PhysicsWorld::GetEvents() const
{
    return m_events;
}
const std::vector<PhysicsDebugBox> &PhysicsWorld::GetDebugBoxes() const
{
    return m_debugBoxes;
}
void PhysicsWorld::Clear()
{
    m_events.clear();
    m_previousPairs.clear();
    m_debugBoxes.clear();
}
bool PhysicsWorld::SetBackend(const PhysicsBackend backend)
{
    if (!IsBackendAvailable(backend))
        return false;
    m_backend = backend;
    return true;
}
PhysicsBackend PhysicsWorld::GetBackend() const
{
    return m_backend;
}
bool PhysicsWorld::IsBackendAvailable(const PhysicsBackend backend)
{
    if (backend == PhysicsBackend::Builtin)
        return true;
#ifdef TINYENGINE_HAS_JOLT
    return true;
#else
    return false;
#endif
}
} // namespace MiniEngine
