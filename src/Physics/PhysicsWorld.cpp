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
    }

    Aabb PhysicsWorld::ComputeBounds(const World& world, const Entity entity)
    {
        const auto* transform = world.GetComponent<TransformComponent>(entity);
        const auto* collider = world.GetComponent<ColliderComponent>(entity);
        if (!transform || !collider) return {};
        Vec3 extent = collider->shape == ColliderShape::Sphere ? Vec3{collider->radius, collider->radius, collider->radius} : collider->halfExtents;
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
            transform.position += body.velocity * fixedDeltaTime;
        });

        std::vector<Entity> colliders;
        world.GetComponentManager().ForEach<ColliderComponent>([&colliders](Entity entity, ColliderComponent&) { colliders.push_back(entity); });
        std::vector<std::pair<Entity, Entity>> currentPairs;
        for (std::size_t firstIndex = 0; firstIndex < colliders.size(); ++firstIndex)
        {
            for (std::size_t secondIndex = firstIndex + 1; secondIndex < colliders.size(); ++secondIndex)
            {
                const Entity first = colliders[firstIndex], second = colliders[secondIndex];
                const auto* firstCollider = world.GetComponent<ColliderComponent>(first);
                const auto* secondCollider = world.GetComponent<ColliderComponent>(second);
                if ((firstCollider->mask & secondCollider->layer) == 0 || (secondCollider->mask & firstCollider->layer) == 0) continue;
                const Aabb firstBounds = ComputeBounds(world, first), secondBounds = ComputeBounds(world, second);
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

    bool PhysicsWorld::Raycast(const World& world, const Ray& ray, const float maxDistance, RaycastHit& hit) const
    {
        bool found = false; float closest = maxDistance;
        for (const Entity entity : world.GetEntityManager().GetAliveEntities())
        {
            if (!world.HasComponent<ColliderComponent>(entity)) continue;
            const Aabb bounds = ComputeBounds(world, entity);
            float tMin = 0.0f, tMax = closest;
            for (int axis = 0; axis < 3; ++axis)
            {
                const float origin = axis == 0 ? ray.origin.x : axis == 1 ? ray.origin.y : ray.origin.z;
                const float direction = axis == 0 ? ray.direction.x : axis == 1 ? ray.direction.y : ray.direction.z;
                const float minimum = axis == 0 ? bounds.minimum.x : axis == 1 ? bounds.minimum.y : bounds.minimum.z;
                const float maximum = axis == 0 ? bounds.maximum.x : axis == 1 ? bounds.maximum.y : bounds.maximum.z;
                if (std::abs(direction) < 1e-6f) { if (origin < minimum || origin > maximum) { tMin = maxDistance + 1.0f; break; } continue; }
                float nearValue = (minimum - origin) / direction, farValue = (maximum - origin) / direction;
                if (nearValue > farValue) std::swap(nearValue, farValue);
                tMin = std::max(tMin, nearValue); tMax = std::min(tMax, farValue);
                if (tMin > tMax) break;
            }
            if (tMin <= tMax && tMin < closest) { closest = tMin; hit.entity = entity; hit.distance = tMin; hit.point = ray.origin + ray.direction * tMin; hit.normal = {}; found = true; }
        }
        return found;
    }

    const std::vector<CollisionEvent>& PhysicsWorld::GetEvents() const { return m_events; }
    void PhysicsWorld::Clear() { m_events.clear(); m_previousPairs.clear(); }
}
