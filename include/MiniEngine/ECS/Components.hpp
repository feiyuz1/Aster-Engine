#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Types.hpp"

#include <string>
#include <vector>

namespace MiniEngine
{
struct NameComponent
{
    std::string name = "Entity";
};
struct ActiveComponent
{
    bool active = true;
};
struct TagComponent
{
    std::string tag = "Untagged";
};
struct LayerComponent
{
    std::uint32_t layer = 0;
};

struct TransformComponent
{
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct RelationshipComponent
{
    Entity parent = InvalidEntity;
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

struct LodComponent
{
    std::string level1MeshPath;
    std::string level2MeshPath;
    float level1Distance = 20.0f;
    float level2Distance = 50.0f;
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

struct PawnComponent
{
    bool playerControlled = true;
    std::uint32_t controllerIndex = 0;
};

struct CharacterControllerComponent
{
    float movementSpeed = 4.0f;
    float sprintSpeed = 8.0f;
    float jumpSpeed = 5.0f;
    float gravity = 18.0f;
    float airControl = 0.35f;
    bool grounded = false;
    Vec3 velocity{};
};

enum class BodyType
{
    Static,
    Dynamic,
    Kinematic
};
struct RigidBodyComponent
{
    BodyType type = BodyType::Dynamic;
    float mass = 1.0f;
    Vec3 velocity{};
    bool useGravity = true;
    float linearDamping = 0.05f;
    float restitution = 0.0f;
    bool continuous = false;
    Vec3 angularVelocity{};
    float angularDamping = 0.1f;
    float friction = 0.6f;
    Vec3 inertiaTensor{1.0f, 1.0f, 1.0f};
    bool lockRotationX = false;
    bool lockRotationY = false;
    bool lockRotationZ = false;
};

enum class ColliderShape
{
    Box,
    Sphere,
    Capsule,
    ConvexHull,
    TriangleMesh
};
struct ColliderComponent
{
    ColliderShape shape = ColliderShape::Box;
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
    float radius = 0.5f;
    float height = 1.0f;
    bool trigger = false;
    std::uint32_t layer = 1;
    std::uint32_t mask = 0xffffffffu;
    std::string meshPath;
};

enum class JointType
{
    Fixed,
    Distance,
    Hinge,
    Spring
};
struct JointComponent
{
    JointType type = JointType::Distance;
    Entity connectedEntity = InvalidEntity;
    float restLength = 1.0f;
    float stiffness = 1.0f;
    Vec3 fixedOffset{};
    bool collideConnected = false;
    Vec3 axis{0.0f, 1.0f, 0.0f};
    float damping = 0.1f;
    float minimumLimit = -180.0f;
    float maximumLimit = 180.0f;
    float breakForce = 0.0f;
};

struct AnimationComponent
{
    std::string clipPath;
    float time = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool playing = true;
    std::string nextClipPath;
    float blendDuration = 0.0f;
    float blendTime = 0.0f;
    bool rootMotion = false;
    std::string skeletonPath;
    std::string graphPath;
    std::string graphParameter = "Speed";
    float graphParameterValue = 0.0f;
    std::string graphState;
    std::vector<std::string> firedEvents;
    std::vector<Mat4> skinMatrices;
};

struct AudioSourceComponent
{
    std::string clipPath;
    float volume = 1.0f;
    bool loop = false;
    bool playOnStart = false;
    bool spatial = true;
    float minimumDistance = 1.0f;
    float maximumDistance = 25.0f;
    std::string bus = "SFX";
};
} // namespace MiniEngine
