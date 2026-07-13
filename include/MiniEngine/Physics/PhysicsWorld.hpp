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

    class PhysicsWorld
    {
    public:
        void Step(World& world, float fixedDeltaTime);
        bool Raycast(const World& world, const Ray& ray, float maxDistance, RaycastHit& hit) const;
        const std::vector<CollisionEvent>& GetEvents() const;
        void Clear();
    private:
        static Aabb ComputeBounds(const World& world, Entity entity);
        std::vector<CollisionEvent> m_events;
        std::vector<std::pair<Entity, Entity>> m_previousPairs;
    };
}
