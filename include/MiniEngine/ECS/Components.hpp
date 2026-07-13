#pragma once

#include <string>

namespace MiniEngine
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct TransformComponent
    {
        Vec3 position{};
        Vec3 rotation{};
        Vec3 scale{1.0f, 1.0f, 1.0f};
    };

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
    };

    struct LightComponent
    {
        Vec3 color{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
    };

    struct ScriptComponent
    {
        std::string scriptClassName;
    };
}
