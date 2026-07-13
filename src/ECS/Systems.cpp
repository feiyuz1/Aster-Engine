#include "MiniEngine/ECS/Systems.hpp"

#include "MiniEngine/ECS/Components.hpp"

namespace MiniEngine
{
    void MovementSystem::Update(World& world, const float deltaTime)
    {
        auto& components = world.GetComponentManager();
        components.ForEach<VelocityComponent>([&components, deltaTime](const Entity entity, VelocityComponent& velocity)
        {
            TransformComponent* transform = components.Get<TransformComponent>(entity);
            if (transform == nullptr)
            {
                return;
            }

            transform->position.x += velocity.linear.x * deltaTime;
            transform->position.y += velocity.linear.y * deltaTime;
            transform->position.z += velocity.linear.z * deltaTime;
        });
    }
}
