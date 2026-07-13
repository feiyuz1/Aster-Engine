#include "MiniEngine/Physics/PhysicsWorld.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace MiniEngine
{
    namespace
    {
        std::pair<Entity, Entity> OrderedPair(Entity a, Entity b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }
        bool RayAabb(const Ray& ray, const Aabb& bounds, const float maxDistance, float& distance)
        {
            float tMin = 0.0f, tMax = maxDistance;
            for (int axis = 0; axis < 3; ++axis)
            {
                const float origin = axis == 0 ? ray.origin.x : axis == 1 ? ray.origin.y : ray.origin.z;
                const float direction = axis == 0 ? ray.direction.x : axis == 1 ? ray.direction.y : ray.direction.z;
                const float minimum = axis == 0 ? bounds.minimum.x : axis == 1 ? bounds.minimum.y : bounds.minimum.z;
                const float maximum = axis == 0 ? bounds.maximum.x : axis == 1 ? bounds.maximum.y : bounds.maximum.z;
                if (std::abs(direction) < 1e-6f) { if (origin < minimum || origin > maximum) return false; continue; }
                float nearValue = (minimum - origin) / direction, farValue = (maximum - origin) / direction;
                if (nearValue > farValue) std::swap(nearValue, farValue);
                tMin = std::max(tMin, nearValue); tMax = std::min(tMax, farValue);
                if (tMin > tMax) return false;
            }
            distance = tMin; return true;
        }
    }

    Aabb PhysicsWorld::ComputeBounds(const World& world, const Entity entity)
    {
        const auto* transform = world.GetComponent<TransformComponent>(entity);
        const auto* collider = world.GetComponent<ColliderComponent>(entity);
        if (!transform || !collider) return {};
        Vec3 extent = collider->shape == ColliderShape::Box ? collider->halfExtents : Vec3{collider->radius, collider->shape == ColliderShape::Capsule ? collider->height * 0.5f + collider->radius : collider->radius, collider->radius};
        extent = {extent.x * std::abs(transform->scale.x), extent.y * std::abs(transform->scale.y), extent.z * std::abs(transform->scale.z)};
        return {transform->position - extent, transform->position + extent};
    }

    void PhysicsWorld::Step(World& world, const float fixedDeltaTime)
    {
        m_events.clear();
        world.GetComponentManager().ForEach<TransformComponent, RigidBodyComponent>([fixedDeltaTime](Entity, TransformComponent& transform, RigidBodyComponent& body)
        {
            if (body.type != BodyType::Dynamic) return;
            if (body.useGravity) body.velocity.y -= 9.81f * fixedDeltaTime;
            const int substeps = std::clamp(static_cast<int>(std::ceil(Length(body.velocity) * fixedDeltaTime / 0.25f)), 1, 8);
            for (int step = 0; step < substeps; ++step) transform.position += body.velocity * (fixedDeltaTime / static_cast<float>(substeps));
        });

        struct BroadphaseEntry { Entity entity; Aabb bounds; };
        std::vector<BroadphaseEntry> colliders;
        world.GetComponentManager().ForEach<ColliderComponent>([&](Entity entity, ColliderComponent&) { colliders.push_back({entity, ComputeBounds(world, entity)}); });
        std::sort(colliders.begin(), colliders.end(), [](const BroadphaseEntry& a, const BroadphaseEntry& b) { return a.bounds.minimum.x < b.bounds.minimum.x; });
        std::vector<std::pair<Entity, Entity>> currentPairs;
        for (std::size_t firstIndex = 0; firstIndex < colliders.size(); ++firstIndex)
        {
            for (std::size_t secondIndex = firstIndex + 1; secondIndex < colliders.size(); ++secondIndex)
            {
                if (colliders[secondIndex].bounds.minimum.x > colliders[firstIndex].bounds.maximum.x) break;
                const Entity first = colliders[firstIndex].entity, second = colliders[secondIndex].entity;
                const auto* firstCollider = world.GetComponent<ColliderComponent>(first);
                const auto* secondCollider = world.GetComponent<ColliderComponent>(second);
                if ((firstCollider->mask & secondCollider->layer) == 0 || (secondCollider->mask & firstCollider->layer) == 0) continue;
                const Aabb firstBounds = colliders[firstIndex].bounds, secondBounds = colliders[secondIndex].bounds;
                if (!firstBounds.Intersects(secondBounds)) continue;
                const auto pair = OrderedPair(first, second); currentPairs.push_back(pair);
                const bool existed = std::find(m_previousPairs.begin(), m_previousPairs.end(), pair) != m_previousPairs.end();
                m_events.push_back({first, second, existed ? CollisionEventType::Stay : CollisionEventType::Enter, firstCollider->trigger || secondCollider->trigger});
                if (firstCollider->trigger || secondCollider->trigger) continue;

                auto resolveDynamic = [&](Entity dynamicEntity, Entity staticEntity)
                {
                    auto* body = world.GetComponent<RigidBodyComponent>(dynamicEntity);
                    auto* transform = world.GetComponent<TransformComponent>(dynamicEntity);
                    const auto* staticBody = world.GetComponent<RigidBodyComponent>(staticEntity);
                    if (!body || !transform || body->type != BodyType::Dynamic || (staticBody && staticBody->type == BodyType::Dynamic)) return;
                    const Aabb dynamicBounds = ComputeBounds(world, dynamicEntity), staticBounds = ComputeBounds(world, staticEntity);
                    const float penetration = staticBounds.maximum.y - dynamicBounds.minimum.y;
                    if (penetration > 0.0f && transform->position.y >= (staticBounds.minimum.y + staticBounds.maximum.y) * 0.5f)
                    {
                        transform->position.y += penetration;
                        if (body->velocity.y < 0.0f) body->velocity.y = 0.0f;
                    }
                };
                resolveDynamic(first, second); resolveDynamic(second, first);
            }
        }
        for (const auto& oldPair : m_previousPairs)
            if (std::find(currentPairs.begin(), currentPairs.end(), oldPair) == currentPairs.end()) m_events.push_back({oldPair.first, oldPair.second, CollisionEventType::Exit, false});
        m_previousPairs = std::move(currentPairs);
    }

    bool PhysicsWorld::Raycast(const World& world, const Ray& ray, const float maxDistance, RaycastHit& hit, const PhysicsQuery& query) const
    {
        bool found = false; float closest = maxDistance;
        for (const Entity entity : world.GetEntityManager().GetAliveEntities())
        {
            const auto* collider = world.GetComponent<ColliderComponent>(entity);
            if (!collider || entity == query.ignoredEntity || (!query.includeTriggers && collider->trigger) || (collider->layer & query.layerMask) == 0) continue;
            const Aabb bounds = ComputeBounds(world, entity);
            float distance = 0.0f;
            if (RayAabb(ray, bounds, closest, distance) && distance < closest) { closest = distance; hit.entity = entity; hit.distance = distance; hit.point = ray.origin + ray.direction * distance; hit.normal = {}; found = true; }
        }
        return found;
    }

    bool PhysicsWorld::SphereCast(const World& world, const Ray& ray, const float radius, const float maxDistance, RaycastHit& hit, const PhysicsQuery& query) const
    {
        bool found = false; float closest = maxDistance;
        for (const Entity entity : world.GetEntityManager().GetAliveEntities())
        {
            const auto* collider = world.GetComponent<ColliderComponent>(entity);
            if (!collider || entity == query.ignoredEntity || (!query.includeTriggers && collider->trigger) || (collider->layer & query.layerMask) == 0) continue;
            Aabb bounds = ComputeBounds(world, entity); const Vec3 extent{radius, radius, radius}; bounds.minimum -= extent; bounds.maximum += extent;
            float distance = 0.0f;
            if (RayAabb(ray, bounds, closest, distance) && distance < closest) { closest = distance; hit = {entity, ray.origin + ray.direction * distance, {}, distance}; found = true; }
        }
        return found;
    }

    std::vector<Entity> PhysicsWorld::Overlap(const World& world, const Aabb& bounds, const PhysicsQuery& query) const
    {
        std::vector<Entity> result;
        for (const Entity entity : world.GetEntityManager().GetAliveEntities())
        {
            const auto* collider = world.GetComponent<ColliderComponent>(entity);
            if (!collider || entity == query.ignoredEntity || (!query.includeTriggers && collider->trigger) || (collider->layer & query.layerMask) == 0) continue;
            if (bounds.Intersects(ComputeBounds(world, entity))) result.push_back(entity);
        }
        return result;
    }

    Vec3 PhysicsWorld::MoveCharacter(World& world, const Entity entity, const Vec3& displacement, const float skinWidth) const
    {
        auto* transform = world.GetComponent<TransformComponent>(entity);
        if (!transform || !world.HasComponent<ColliderComponent>(entity)) return {};
        const Vec3 start = transform->position;
        const float values[3] = {displacement.x, displacement.y, displacement.z};
        for (int axis = 0; axis < 3; ++axis)
        {
            const float old = axis == 0 ? transform->position.x : axis == 1 ? transform->position.y : transform->position.z;
            if (axis == 0) transform->position.x += values[axis]; else if (axis == 1) transform->position.y += values[axis]; else transform->position.z += values[axis];
            Aabb bounds = ComputeBounds(world, entity); bounds.minimum += Vec3{skinWidth, skinWidth, skinWidth}; bounds.maximum -= Vec3{skinWidth, skinWidth, skinWidth};
            if (!Overlap(world, bounds, {0xffffffffu, false, entity}).empty())
            {
                if (axis == 0) transform->position.x = old; else if (axis == 1) transform->position.y = old; else transform->position.z = old;
            }
        }
        return transform->position - start;
    }

    const std::vector<CollisionEvent>& PhysicsWorld::GetEvents() const { return m_events; }
    void PhysicsWorld::Clear() { m_events.clear(); m_previousPairs.clear(); }
}
