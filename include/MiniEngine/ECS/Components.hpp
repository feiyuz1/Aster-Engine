#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Types.hpp"

#include <string>

namespace MiniEngine
{
    struct NameComponent { std::string name = "Entity"; };
    struct ActiveComponent { bool active = true; };
    struct TagComponent { std::string tag = "Untagged"; };
    struct LayerComponent { std::uint32_t layer = 0; };

    struct TransformComponent
    {
        Vec3 position{};
        Vec3 rotation{};
        Vec3 scale{1.0f, 1.0f, 1.0f};
    };

    struct RelationshipComponent { Entity parent = InvalidEntity; };

    struct VelocityComponent
    {
        Vec3 linear{};
    };

    struct MeshComponent
    {
        std::string meshPath;
        std::string materialPath;
    };

    struct CameraComponent
    {
        float fov = 60.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        bool primary = true;
    };

    struct LightComponent
    {
        Vec3 color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        Vec3 direction{-0.4f, -1.0f, -0.3f};
    };

    struct ScriptComponent
    {
        std::string scriptClassName;
    };

    enum class BodyType { Static, Dynamic, Kinematic };
    struct RigidBodyComponent
    {
        BodyType type = BodyType::Dynamic;
        float mass = 1.0f;
        Vec3 velocity{};
        bool useGravity = true;
    };

    enum class ColliderShape { Box, Sphere, Capsule };
    struct ColliderComponent
    {
        ColliderShape shape = ColliderShape::Box;
        Vec3 halfExtents{0.5f, 0.5f, 0.5f};
        float radius = 0.5f;
        float height = 1.0f;
        bool trigger = false;
        std::uint32_t layer = 1;
        std::uint32_t mask = 0xffffffffu;
    };

    struct AnimationComponent
    {
        std::string clipPath;
        float time = 0.0f;
        float speed = 1.0f;
        bool loop = true;
        bool playing = true;
    };

    struct AudioSourceComponent
    {
        std::string clipPath;
        float volume = 1.0f;
        bool loop = false;
        bool playOnStart = false;
    };
}
