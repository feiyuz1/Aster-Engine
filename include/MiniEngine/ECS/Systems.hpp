#pragma once

#include "MiniEngine/ECS/ECS.hpp"

namespace MiniEngine
{
    class MovementSystem final : public System
    {
    public:
        void Update(World& world, float deltaTime) override;
    };
}
