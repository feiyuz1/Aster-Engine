// 文件：src/ecs/reflection.cpp
// 作用：登记组件字段元数据，供序列化器和 Inspector 共用。
// 约定：实体句柄包含代际信息，禁止长期保存未校验的裸实体编号。
// 类型：实现文件。

#include "tinyengine/ecs/reflection.h"
#include "tinyengine/ecs/components.h"

#include <utility>

namespace tinyengine
{
namespace
{
template <typename Component> ReflectedComponent MakeComponent(std::string name, const bool removable = true)
{
    ReflectedComponent result;
    result.name = std::move(name);
    result.removable = removable;
    result.has = [](const World& world, const Entity entity) { return world.HasComponent<Component>(entity); };
    result.add = [](World& world, const Entity entity) { world.AddComponent(entity, Component{}); };
    result.remove = [](World& world, const Entity entity) { world.RemoveComponent<Component>(entity); };
    return result;
}

template <typename Component, typename Field>
ReflectedProperty makeProperty(std::string name, const ReflectedPropertyType type, Field Component::*member)
{
    ReflectedProperty result;
    result.name = std::move(name);
    result.type = type;
    result.access = [member](World& world, const Entity entity) -> void*
    {
        auto* component = world.GetComponent<Component>(entity);
        return component ? static_cast<void*>(&(component->*member)) : nullptr;
    };
    return result;
}

template <typename Component, typename Enum>
ReflectedProperty makeEnumProperty(std::string name, Enum Component::*member, std::vector<std::string> names)
{
    ReflectedProperty result;
    result.name = std::move(name);
    result.type = ReflectedPropertyType::Enum;
    result.enumNames = std::move(names);
    result.getEnum = [member](const World& world, const Entity entity)
    {
        const auto* component = world.GetComponent<Component>(entity);
        return component ? static_cast<int>(component->*member) : 0;
    };
    result.setEnum = [member](World& world, const Entity entity, const int value)
    {
        if (auto* component = world.GetComponent<Component>(entity))
            component->*member = static_cast<Enum>(value);
    };
    return result;
}

ReflectedProperty& Range(ReflectedProperty& property, const float minimum, const float maximum, const float speed)
{
    property.hasRange = true;
    property.minimum = minimum;
    property.maximum = maximum;
    property.speed = speed;
    return property;
}
} // namespace

ReflectionRegistry::ReflectionRegistry()
{
    auto name = MakeComponent<NameComponent>("Name", false);
    name.properties.push_back(makeProperty("Name", ReflectedPropertyType::String, &NameComponent::name));
    m_components.push_back(std::move(name));

    auto active = MakeComponent<ActiveComponent>("Active", false);
    active.properties.push_back(makeProperty("Active", ReflectedPropertyType::Bool, &ActiveComponent::active));
    m_components.push_back(std::move(active));

    auto transform = MakeComponent<TransformComponent>("Transform", false);
    transform.properties.push_back(
        makeProperty("Position", ReflectedPropertyType::Vec3, &TransformComponent::position));
    transform.properties.back().speed = 0.05f;
    transform.properties.push_back(
        makeProperty("Rotation", ReflectedPropertyType::Vec3, &TransformComponent::rotation));
    transform.properties.back().speed = 0.25f;
    transform.properties.push_back(makeProperty("Scale", ReflectedPropertyType::Vec3, &TransformComponent::scale));
    Range(transform.properties.back(), 0.001f, 1000.0f, 0.02f);
    m_components.push_back(std::move(transform));

    auto tag = MakeComponent<TagComponent>("Tag", false);
    tag.properties.push_back(makeProperty("Tag", ReflectedPropertyType::String, &TagComponent::tag));
    m_components.push_back(std::move(tag));

    auto layer = MakeComponent<LayerComponent>("Layer", false);
    layer.properties.push_back(makeProperty("Layer", ReflectedPropertyType::UInt, &LayerComponent::layer));
    m_components.push_back(std::move(layer));

    auto velocity = MakeComponent<VelocityComponent>("Velocity");
    velocity.properties.push_back(makeProperty("Linear", ReflectedPropertyType::Vec3, &VelocityComponent::linear));
    m_components.push_back(std::move(velocity));

    auto mesh = MakeComponent<MeshComponent>("Mesh Renderer");
    mesh.properties.push_back(makeProperty("Mesh", ReflectedPropertyType::String, &MeshComponent::meshPath));
    mesh.properties.back().assetType = "Mesh";
    mesh.properties.push_back(makeProperty("Material", ReflectedPropertyType::String, &MeshComponent::materialPath));
    mesh.properties.back().assetType = "Material";
    m_components.push_back(std::move(mesh));

    auto lod = MakeComponent<LodComponent>("Level Of Detail");
    lod.properties.push_back(
        makeProperty("Level 1 Mesh", ReflectedPropertyType::String, &LodComponent::level1MeshPath));
    lod.properties.back().assetType = "Mesh";
    lod.properties.push_back(
        makeProperty("Level 2 Mesh", ReflectedPropertyType::String, &LodComponent::level2MeshPath));
    lod.properties.back().assetType = "Mesh";
    lod.properties.push_back(
        makeProperty("Level 1 Distance", ReflectedPropertyType::Float, &LodComponent::level1Distance));
    Range(lod.properties.back(), 0.0f, 100000.0f, 0.5f);
    lod.properties.push_back(
        makeProperty("Level 2 Distance", ReflectedPropertyType::Float, &LodComponent::level2Distance));
    Range(lod.properties.back(), 0.0f, 100000.0f, 0.5f);
    m_components.push_back(std::move(lod));

    auto camera = MakeComponent<CameraComponent>("Camera");
    camera.properties.push_back(makeProperty("FOV", ReflectedPropertyType::Float, &CameraComponent::fov));
    Range(camera.properties.back(), 1.0f, 179.0f, 0.25f);
    camera.properties.push_back(makeProperty("Near Plane", ReflectedPropertyType::Float, &CameraComponent::nearPlane));
    Range(camera.properties.back(), 0.001f, 100.0f, 0.01f);
    camera.properties.push_back(makeProperty("Far Plane", ReflectedPropertyType::Float, &CameraComponent::farPlane));
    Range(camera.properties.back(), 0.01f, 100000.0f, 1.0f);
    camera.properties.push_back(makeProperty("Primary", ReflectedPropertyType::Bool, &CameraComponent::primary));
    m_components.push_back(std::move(camera));

    auto light = MakeComponent<LightComponent>("Light");
    light.properties.push_back(makeEnumProperty("Type", &LightComponent::type, {"Directional", "Point", "Spot"}));
    light.properties.push_back(makeProperty("Color", ReflectedPropertyType::Vec3, &LightComponent::color));
    light.properties.back().color = true;
    light.properties.push_back(makeProperty("Intensity", ReflectedPropertyType::Float, &LightComponent::intensity));
    Range(light.properties.back(), 0.0f, 1000.0f, 0.05f);
    light.properties.push_back(makeProperty("Direction", ReflectedPropertyType::Vec3, &LightComponent::direction));
    light.properties.push_back(makeProperty("Range", ReflectedPropertyType::Float, &LightComponent::range));
    Range(light.properties.back(), 0.01f, 100000.0f, 0.1f);
    light.properties.push_back(
        makeProperty("Inner Cone", ReflectedPropertyType::Float, &LightComponent::innerConeAngle));
    Range(light.properties.back(), 0.0f, 89.0f, 0.25f);
    light.properties.push_back(
        makeProperty("Outer Cone", ReflectedPropertyType::Float, &LightComponent::outerConeAngle));
    Range(light.properties.back(), 0.0f, 89.0f, 0.25f);
    m_components.push_back(std::move(light));

    auto script = MakeComponent<ScriptComponent>("Script");
    script.properties.push_back(
        makeProperty("Class", ReflectedPropertyType::String, &ScriptComponent::scriptClassName));
    m_components.push_back(std::move(script));

    auto pawn = MakeComponent<PawnComponent>("Pawn");
    pawn.properties.push_back(
        makeProperty("Player Controlled", ReflectedPropertyType::Bool, &PawnComponent::playerControlled));
    pawn.properties.push_back(
        makeProperty("Controller Index", ReflectedPropertyType::UInt, &PawnComponent::controllerIndex));
    m_components.push_back(std::move(pawn));

    auto character = MakeComponent<CharacterControllerComponent>("Character Controller");
    character.properties.push_back(
        makeProperty("Movement Speed", ReflectedPropertyType::Float, &CharacterControllerComponent::movementSpeed));
    Range(character.properties.back(), 0.0f, 1000.0f, 0.1f);
    character.properties.push_back(
        makeProperty("Sprint Speed", ReflectedPropertyType::Float, &CharacterControllerComponent::sprintSpeed));
    Range(character.properties.back(), 0.0f, 1000.0f, 0.1f);
    character.properties.push_back(
        makeProperty("Jump Speed", ReflectedPropertyType::Float, &CharacterControllerComponent::jumpSpeed));
    Range(character.properties.back(), 0.0f, 1000.0f, 0.1f);
    character.properties.push_back(
        makeProperty("Gravity", ReflectedPropertyType::Float, &CharacterControllerComponent::gravity));
    Range(character.properties.back(), 0.0f, 1000.0f, 0.1f);
    character.properties.push_back(
        makeProperty("Air Control", ReflectedPropertyType::Float, &CharacterControllerComponent::airControl));
    Range(character.properties.back(), 0.0f, 1.0f, 0.01f);
    character.properties.push_back(
        makeProperty("Grounded", ReflectedPropertyType::Bool, &CharacterControllerComponent::grounded));
    character.properties.push_back(
        makeProperty("Velocity", ReflectedPropertyType::Vec3, &CharacterControllerComponent::velocity));
    m_components.push_back(std::move(character));

    auto rigidBody = MakeComponent<RigidBodyComponent>("Rigid Body");
    rigidBody.properties.push_back(
        makeEnumProperty("Type", &RigidBodyComponent::type, {"Static", "Dynamic", "Kinematic"}));
    rigidBody.properties.push_back(makeProperty("Mass", ReflectedPropertyType::Float, &RigidBodyComponent::mass));
    Range(rigidBody.properties.back(), 0.0f, 100000.0f, 0.1f);
    rigidBody.properties.push_back(
        makeProperty("Velocity", ReflectedPropertyType::Vec3, &RigidBodyComponent::velocity));
    rigidBody.properties.push_back(
        makeProperty("Use Gravity", ReflectedPropertyType::Bool, &RigidBodyComponent::useGravity));
    rigidBody.properties.push_back(
        makeProperty("Linear Damping", ReflectedPropertyType::Float, &RigidBodyComponent::linearDamping));
    Range(rigidBody.properties.back(), 0.0f, 100.0f, 0.01f);
    rigidBody.properties.push_back(
        makeProperty("Restitution", ReflectedPropertyType::Float, &RigidBodyComponent::restitution));
    Range(rigidBody.properties.back(), 0.0f, 1.0f, 0.01f);
    rigidBody.properties.push_back(
        makeProperty("Continuous", ReflectedPropertyType::Bool, &RigidBodyComponent::continuous));
    rigidBody.properties.push_back(
        makeProperty("Angular Velocity", ReflectedPropertyType::Vec3, &RigidBodyComponent::angularVelocity));
    rigidBody.properties.push_back(
        makeProperty("Angular Damping", ReflectedPropertyType::Float, &RigidBodyComponent::angularDamping));
    Range(rigidBody.properties.back(), 0.0f, 100.0f, 0.01f);
    rigidBody.properties.push_back(
        makeProperty("Friction", ReflectedPropertyType::Float, &RigidBodyComponent::friction));
    Range(rigidBody.properties.back(), 0.0f, 4.0f, 0.01f);
    rigidBody.properties.push_back(
        makeProperty("Inertia Tensor", ReflectedPropertyType::Vec3, &RigidBodyComponent::inertiaTensor));
    rigidBody.properties.push_back(
        makeProperty("Lock Rotation X", ReflectedPropertyType::Bool, &RigidBodyComponent::lockRotationX));
    rigidBody.properties.push_back(
        makeProperty("Lock Rotation Y", ReflectedPropertyType::Bool, &RigidBodyComponent::lockRotationY));
    rigidBody.properties.push_back(
        makeProperty("Lock Rotation Z", ReflectedPropertyType::Bool, &RigidBodyComponent::lockRotationZ));
    m_components.push_back(std::move(rigidBody));

    auto collider = MakeComponent<ColliderComponent>("Collider");
    collider.properties.push_back(makeEnumProperty("Shape", &ColliderComponent::shape,
                                                   {"Box", "Sphere", "Capsule", "Convex Hull", "Triangle Mesh"}));
    collider.properties.push_back(
        makeProperty("Half Extents", ReflectedPropertyType::Vec3, &ColliderComponent::halfExtents));
    Range(collider.properties.back(), 0.001f, 100000.0f, 0.02f);
    collider.properties.push_back(makeProperty("Radius", ReflectedPropertyType::Float, &ColliderComponent::radius));
    Range(collider.properties.back(), 0.001f, 100000.0f, 0.02f);
    collider.properties.push_back(makeProperty("Height", ReflectedPropertyType::Float, &ColliderComponent::height));
    Range(collider.properties.back(), 0.001f, 100000.0f, 0.02f);
    collider.properties.push_back(makeProperty("Trigger", ReflectedPropertyType::Bool, &ColliderComponent::trigger));
    collider.properties.push_back(makeProperty("Layer", ReflectedPropertyType::UInt, &ColliderComponent::layer));
    collider.properties.push_back(makeProperty("Mask", ReflectedPropertyType::UInt, &ColliderComponent::mask));
    collider.properties.push_back(makeProperty("Mesh", ReflectedPropertyType::String, &ColliderComponent::meshPath));
    collider.properties.back().assetType = "Mesh";
    m_components.push_back(std::move(collider));

    auto joint = MakeComponent<JointComponent>("Joint");
    joint.properties.push_back(
        makeEnumProperty("Type", &JointComponent::type, {"Fixed", "Distance", "Hinge", "Spring"}));
    joint.properties.push_back(
        makeProperty("Connected Entity", ReflectedPropertyType::Entity, &JointComponent::connectedEntity));
    joint.properties.push_back(makeProperty("Rest Length", ReflectedPropertyType::Float, &JointComponent::restLength));
    Range(joint.properties.back(), 0.0f, 100000.0f, 0.05f);
    joint.properties.push_back(makeProperty("Stiffness", ReflectedPropertyType::Float, &JointComponent::stiffness));
    Range(joint.properties.back(), 0.0f, 1.0f, 0.01f);
    joint.properties.push_back(makeProperty("Fixed Offset", ReflectedPropertyType::Vec3, &JointComponent::fixedOffset));
    joint.properties.push_back(
        makeProperty("Collide Connected", ReflectedPropertyType::Bool, &JointComponent::collideConnected));
    joint.properties.push_back(makeProperty("Axis", ReflectedPropertyType::Vec3, &JointComponent::axis));
    joint.properties.push_back(makeProperty("Damping", ReflectedPropertyType::Float, &JointComponent::damping));
    joint.properties.push_back(
        makeProperty("Minimum Limit", ReflectedPropertyType::Float, &JointComponent::minimumLimit));
    joint.properties.push_back(
        makeProperty("Maximum Limit", ReflectedPropertyType::Float, &JointComponent::maximumLimit));
    joint.properties.push_back(makeProperty("Break Force", ReflectedPropertyType::Float, &JointComponent::breakForce));
    m_components.push_back(std::move(joint));

    auto animation = MakeComponent<AnimationComponent>("Animation");
    animation.properties.push_back(makeProperty("Clip", ReflectedPropertyType::String, &AnimationComponent::clipPath));
    animation.properties.back().assetType = "Animation";
    animation.properties.push_back(
        makeProperty("Skeleton", ReflectedPropertyType::String, &AnimationComponent::skeletonPath));
    animation.properties.back().assetType = "Skeleton";
    animation.properties.push_back(makeProperty("Time", ReflectedPropertyType::Float, &AnimationComponent::time));
    Range(animation.properties.back(), 0.0f, 100000.0f, 0.01f);
    animation.properties.push_back(makeProperty("Speed", ReflectedPropertyType::Float, &AnimationComponent::speed));
    Range(animation.properties.back(), -100.0f, 100.0f, 0.05f);
    animation.properties.push_back(makeProperty("Loop", ReflectedPropertyType::Bool, &AnimationComponent::loop));
    animation.properties.push_back(makeProperty("Playing", ReflectedPropertyType::Bool, &AnimationComponent::playing));
    animation.properties.push_back(
        makeProperty("Next Clip", ReflectedPropertyType::String, &AnimationComponent::nextClipPath));
    animation.properties.back().assetType = "Animation";
    animation.properties.push_back(
        makeProperty("Blend Duration", ReflectedPropertyType::Float, &AnimationComponent::blendDuration));
    Range(animation.properties.back(), 0.0f, 60.0f, 0.05f);
    animation.properties.push_back(
        makeProperty("Root Motion", ReflectedPropertyType::Bool, &AnimationComponent::rootMotion));
    animation.properties.push_back(
        makeProperty("Animation Graph", ReflectedPropertyType::String, &AnimationComponent::graphPath));
    animation.properties.back().assetType = "AnimationGraph";
    animation.properties.push_back(
        makeProperty("Graph Parameter", ReflectedPropertyType::String, &AnimationComponent::graphParameter));
    animation.properties.push_back(
        makeProperty("Graph Value", ReflectedPropertyType::Float, &AnimationComponent::graphParameterValue));
    m_components.push_back(std::move(animation));

    auto audio = MakeComponent<AudioSourceComponent>("Audio Source");
    audio.properties.push_back(makeProperty("Clip", ReflectedPropertyType::String, &AudioSourceComponent::clipPath));
    audio.properties.back().assetType = "Audio";
    audio.properties.push_back(makeProperty("Volume", ReflectedPropertyType::Float, &AudioSourceComponent::volume));
    Range(audio.properties.back(), 0.0f, 1.0f, 0.01f);
    audio.properties.push_back(makeProperty("Loop", ReflectedPropertyType::Bool, &AudioSourceComponent::loop));
    audio.properties.push_back(
        makeProperty("Play On Start", ReflectedPropertyType::Bool, &AudioSourceComponent::playOnStart));
    audio.properties.push_back(makeProperty("Spatial", ReflectedPropertyType::Bool, &AudioSourceComponent::spatial));
    audio.properties.push_back(
        makeProperty("Minimum Distance", ReflectedPropertyType::Float, &AudioSourceComponent::minimumDistance));
    Range(audio.properties.back(), 0.0f, 100000.0f, 0.1f);
    audio.properties.push_back(
        makeProperty("Maximum Distance", ReflectedPropertyType::Float, &AudioSourceComponent::maximumDistance));
    Range(audio.properties.back(), 0.01f, 100000.0f, 0.1f);
    audio.properties.push_back(makeProperty("Bus", ReflectedPropertyType::String, &AudioSourceComponent::bus));
    m_components.push_back(std::move(audio));
}

const ReflectionRegistry& ReflectionRegistry::Get()
{
    static const ReflectionRegistry registry;
    return registry;
}

const std::vector<ReflectedComponent>& ReflectionRegistry::GetComponents() const
{
    return m_components;
}

const ReflectedComponent* ReflectionRegistry::findComponent(const std::string& name) const
{
    for (const auto& component : m_components)
        if (component.name == name)
            return &component;
    return nullptr;
}
} // namespace tinyengine
