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
        const Vec3 target = connected->position + (joint.type == JointType::Fixed ? joint.fixedOffset : Vec3{});
        const Vec3 delta = transform->position - target;
        const float distance = Length(delta);
        if (distance < 0.0001f)
            return;
        const float desired = joint.type == JointType::Distance ? std::max(0.0f, joint.restLength) : 0.0f;
        const Vec3 correction = delta * ((distance - desired) / distance * std::clamp(joint.stiffness, 0.0f, 1.0f));
        transform->position -= correction * (inverseMass / sum);
        connected->position += correction * (connectedInverseMass / sum);
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
        float distance = 0.0f;
        Vec3 normal;
        if (RayAabb(ray, bounds, closest, distance, &normal) && distance < closest)
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
} // namespace MiniEngine
