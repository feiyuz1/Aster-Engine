#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Types.hpp"

#include <vector>

namespace MiniEngine
{
    class World;
    enum class CollisionEventType { Enter, Stay, Exit };
    struct CollisionEvent { Entity first = InvalidEntity; Entity second = InvalidEntity; CollisionEventType type = CollisionEventType::Enter; bool trigger = false; };
    struct RaycastHit { Entity entity = InvalidEntity; Vec3 point; Vec3 normal; float distance = 0.0f; };
    struct PhysicsQuery
    {
        std::uint32_t layerMask = 0xffffffffu;
        bool includeTriggers = true;
        Entity ignoredEntity = InvalidEntity;
    };

    class PhysicsWorld
    {
    public:
        void Step(World& world, float fixedDeltaTime);
        bool Raycast(const World& world, const Ray& ray, float maxDistance, RaycastHit& hit, const PhysicsQuery& query = {}) const;
        bool SphereCast(const World& world, const Ray& ray, float radius, float maxDistance, RaycastHit& hit, const PhysicsQuery& query = {}) const;
        std::vector<Entity> Overlap(const World& world, const Aabb& bounds, const PhysicsQuery& query = {}) const;
        Vec3 MoveCharacter(World& world, Entity entity, const Vec3& displacement, float skinWidth = 0.02f) const;
        const std::vector<CollisionEvent>& GetEvents() const;
        void Clear();
    private:
        static Aabb ComputeBounds(const World& world, Entity entity);
        std::vector<CollisionEvent> m_events;
        std::vector<std::pair<Entity, Entity>> m_previousPairs;
    };
}
