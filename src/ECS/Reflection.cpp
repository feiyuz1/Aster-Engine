#include "MiniEngine/ECS/Reflection.hpp"
#include "MiniEngine/ECS/Components.hpp"

#include <utility>

namespace MiniEngine
{
    namespace
    {
        template<typename Component>
        ReflectedComponent MakeComponent(std::string name, const bool removable = true)
        {
            ReflectedComponent result;
            result.name = std::move(name);
            result.removable = removable;
            result.has = [](const World& world, const Entity entity) { return world.HasComponent<Component>(entity); };
            result.add = [](World& world, const Entity entity) { world.AddComponent(entity, Component{}); };
            result.remove = [](World& world, const Entity entity) { world.RemoveComponent<Component>(entity); };
            return result;
        }

        template<typename Component, typename Field>
        ReflectedProperty MakeProperty(std::string name, const ReflectedPropertyType type, Field Component::* member)
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

        template<typename Component, typename Enum>
        ReflectedProperty MakeEnumProperty(std::string name, Enum Component::* member, std::vector<std::string> names)
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
                if (auto* component = world.GetComponent<Component>(entity)) component->*member = static_cast<Enum>(value);
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
    }

    ReflectionRegistry::ReflectionRegistry()
    {
        auto name = MakeComponent<NameComponent>("Name", false);
        name.properties.push_back(MakeProperty("Name", ReflectedPropertyType::String, &NameComponent::name));
        m_components.push_back(std::move(name));

        auto active = MakeComponent<ActiveComponent>("Active", false);
        active.properties.push_back(MakeProperty("Active", ReflectedPropertyType::Bool, &ActiveComponent::active));
        m_components.push_back(std::move(active));

        auto transform = MakeComponent<TransformComponent>("Transform", false);
        transform.properties.push_back(MakeProperty("Position", ReflectedPropertyType::Vec3, &TransformComponent::position));
        transform.properties.back().speed = 0.05f;
        transform.properties.push_back(MakeProperty("Rotation", ReflectedPropertyType::Vec3, &TransformComponent::rotation));
        transform.properties.back().speed = 0.25f;
        transform.properties.push_back(MakeProperty("Scale", ReflectedPropertyType::Vec3, &TransformComponent::scale));
        Range(transform.properties.back(), 0.001f, 1000.0f, 0.02f);
        m_components.push_back(std::move(transform));

        auto tag = MakeComponent<TagComponent>("Tag", false);
        tag.properties.push_back(MakeProperty("Tag", ReflectedPropertyType::String, &TagComponent::tag));
        m_components.push_back(std::move(tag));

        auto layer = MakeComponent<LayerComponent>("Layer", false);
        layer.properties.push_back(MakeProperty("Layer", ReflectedPropertyType::UInt, &LayerComponent::layer));
        m_components.push_back(std::move(layer));

        auto velocity = MakeComponent<VelocityComponent>("Velocity");
        velocity.properties.push_back(MakeProperty("Linear", ReflectedPropertyType::Vec3, &VelocityComponent::linear));
        m_components.push_back(std::move(velocity));

        auto mesh = MakeComponent<MeshComponent>("Mesh Renderer");
        mesh.properties.push_back(MakeProperty("Mesh", ReflectedPropertyType::String, &MeshComponent::meshPath));
        mesh.properties.back().assetType = "Mesh";
        mesh.properties.push_back(MakeProperty("Material", ReflectedPropertyType::String, &MeshComponent::materialPath));
        mesh.properties.back().assetType = "Material";
        m_components.push_back(std::move(mesh));

        auto camera = MakeComponent<CameraComponent>("Camera");
        camera.properties.push_back(MakeProperty("FOV", ReflectedPropertyType::Float, &CameraComponent::fov));
        Range(camera.properties.back(), 1.0f, 179.0f, 0.25f);
        camera.properties.push_back(MakeProperty("Near Plane", ReflectedPropertyType::Float, &CameraComponent::nearPlane));
        Range(camera.properties.back(), 0.001f, 100.0f, 0.01f);
        camera.properties.push_back(MakeProperty("Far Plane", ReflectedPropertyType::Float, &CameraComponent::farPlane));
        Range(camera.properties.back(), 0.01f, 100000.0f, 1.0f);
        camera.properties.push_back(MakeProperty("Primary", ReflectedPropertyType::Bool, &CameraComponent::primary));
        m_components.push_back(std::move(camera));

        auto light = MakeComponent<LightComponent>("Directional Light");
        light.properties.push_back(MakeProperty("Color", ReflectedPropertyType::Vec3, &LightComponent::color));
        light.properties.back().color = true;
        light.properties.push_back(MakeProperty("Intensity", ReflectedPropertyType::Float, &LightComponent::intensity));
        Range(light.properties.back(), 0.0f, 1000.0f, 0.05f);
        light.properties.push_back(MakeProperty("Direction", ReflectedPropertyType::Vec3, &LightComponent::direction));
        m_components.push_back(std::move(light));

        auto script = MakeComponent<ScriptComponent>("Script");
        script.properties.push_back(MakeProperty("Class", ReflectedPropertyType::String, &ScriptComponent::scriptClassName));
        m_components.push_back(std::move(script));

        auto rigidBody = MakeComponent<RigidBodyComponent>("Rigid Body");
        rigidBody.properties.push_back(MakeEnumProperty("Type", &RigidBodyComponent::type, {"Static", "Dynamic", "Kinematic"}));
        rigidBody.properties.push_back(MakeProperty("Mass", ReflectedPropertyType::Float, &RigidBodyComponent::mass));
        Range(rigidBody.properties.back(), 0.0f, 100000.0f, 0.1f);
        rigidBody.properties.push_back(MakeProperty("Velocity", ReflectedPropertyType::Vec3, &RigidBodyComponent::velocity));
        rigidBody.properties.push_back(MakeProperty("Use Gravity", ReflectedPropertyType::Bool, &RigidBodyComponent::useGravity));
        rigidBody.properties.push_back(MakeProperty("Linear Damping", ReflectedPropertyType::Float, &RigidBodyComponent::linearDamping));
        Range(rigidBody.properties.back(), 0.0f, 100.0f, 0.01f);
        rigidBody.properties.push_back(MakeProperty("Restitution", ReflectedPropertyType::Float, &RigidBodyComponent::restitution));
        Range(rigidBody.properties.back(), 0.0f, 1.0f, 0.01f);
        rigidBody.properties.push_back(MakeProperty("Continuous", ReflectedPropertyType::Bool, &RigidBodyComponent::continuous));
        m_components.push_back(std::move(rigidBody));

        auto collider = MakeComponent<ColliderComponent>("Collider");
        collider.properties.push_back(MakeEnumProperty("Shape", &ColliderComponent::shape, {"Box", "Sphere", "Capsule", "Convex Hull", "Triangle Mesh"}));
        collider.properties.push_back(MakeProperty("Half Extents", ReflectedPropertyType::Vec3, &ColliderComponent::halfExtents));
        Range(collider.properties.back(), 0.001f, 100000.0f, 0.02f);
        collider.properties.push_back(MakeProperty("Radius", ReflectedPropertyType::Float, &ColliderComponent::radius));
        Range(collider.properties.back(), 0.001f, 100000.0f, 0.02f);
        collider.properties.push_back(MakeProperty("Height", ReflectedPropertyType::Float, &ColliderComponent::height));
        Range(collider.properties.back(), 0.001f, 100000.0f, 0.02f);
        collider.properties.push_back(MakeProperty("Trigger", ReflectedPropertyType::Bool, &ColliderComponent::trigger));
        collider.properties.push_back(MakeProperty("Layer", ReflectedPropertyType::UInt, &ColliderComponent::layer));
        collider.properties.push_back(MakeProperty("Mask", ReflectedPropertyType::UInt, &ColliderComponent::mask));
        collider.properties.push_back(MakeProperty("Mesh", ReflectedPropertyType::String, &ColliderComponent::meshPath));
        collider.properties.back().assetType = "Mesh";
        m_components.push_back(std::move(collider));

        auto joint = MakeComponent<JointComponent>("Joint");
        joint.properties.push_back(MakeEnumProperty("Type", &JointComponent::type, {"Fixed", "Distance"}));
        joint.properties.push_back(MakeProperty("Connected Entity", ReflectedPropertyType::Entity, &JointComponent::connectedEntity));
        joint.properties.push_back(MakeProperty("Rest Length", ReflectedPropertyType::Float, &JointComponent::restLength));
        Range(joint.properties.back(), 0.0f, 100000.0f, 0.05f);
        joint.properties.push_back(MakeProperty("Stiffness", ReflectedPropertyType::Float, &JointComponent::stiffness));
        Range(joint.properties.back(), 0.0f, 1.0f, 0.01f);
        joint.properties.push_back(MakeProperty("Fixed Offset", ReflectedPropertyType::Vec3, &JointComponent::fixedOffset));
        joint.properties.push_back(MakeProperty("Collide Connected", ReflectedPropertyType::Bool, &JointComponent::collideConnected));
        m_components.push_back(std::move(joint));

        auto animation = MakeComponent<AnimationComponent>("Animation");
        animation.properties.push_back(MakeProperty("Clip", ReflectedPropertyType::String, &AnimationComponent::clipPath));
        animation.properties.back().assetType = "Animation";
        animation.properties.push_back(MakeProperty("Time", ReflectedPropertyType::Float, &AnimationComponent::time));
        Range(animation.properties.back(), 0.0f, 100000.0f, 0.01f);
        animation.properties.push_back(MakeProperty("Speed", ReflectedPropertyType::Float, &AnimationComponent::speed));
        Range(animation.properties.back(), -100.0f, 100.0f, 0.05f);
        animation.properties.push_back(MakeProperty("Loop", ReflectedPropertyType::Bool, &AnimationComponent::loop));
        animation.properties.push_back(MakeProperty("Playing", ReflectedPropertyType::Bool, &AnimationComponent::playing));
        animation.properties.push_back(MakeProperty("Next Clip", ReflectedPropertyType::String, &AnimationComponent::nextClipPath));
        animation.properties.back().assetType = "Animation";
        animation.properties.push_back(MakeProperty("Blend Duration", ReflectedPropertyType::Float, &AnimationComponent::blendDuration));
        Range(animation.properties.back(), 0.0f, 60.0f, 0.05f);
        animation.properties.push_back(MakeProperty("Root Motion", ReflectedPropertyType::Bool, &AnimationComponent::rootMotion));
        m_components.push_back(std::move(animation));

        auto audio = MakeComponent<AudioSourceComponent>("Audio Source");
        audio.properties.push_back(MakeProperty("Clip", ReflectedPropertyType::String, &AudioSourceComponent::clipPath));
        audio.properties.back().assetType = "Audio";
        audio.properties.push_back(MakeProperty("Volume", ReflectedPropertyType::Float, &AudioSourceComponent::volume));
        Range(audio.properties.back(), 0.0f, 1.0f, 0.01f);
        audio.properties.push_back(MakeProperty("Loop", ReflectedPropertyType::Bool, &AudioSourceComponent::loop));
        audio.properties.push_back(MakeProperty("Play On Start", ReflectedPropertyType::Bool, &AudioSourceComponent::playOnStart));
        audio.properties.push_back(MakeProperty("Spatial", ReflectedPropertyType::Bool, &AudioSourceComponent::spatial));
        audio.properties.push_back(MakeProperty("Minimum Distance", ReflectedPropertyType::Float, &AudioSourceComponent::minimumDistance));
        Range(audio.properties.back(), 0.0f, 100000.0f, 0.1f);
        audio.properties.push_back(MakeProperty("Maximum Distance", ReflectedPropertyType::Float, &AudioSourceComponent::maximumDistance));
        Range(audio.properties.back(), 0.01f, 100000.0f, 0.1f);
        audio.properties.push_back(MakeProperty("Bus", ReflectedPropertyType::String, &AudioSourceComponent::bus));
        m_components.push_back(std::move(audio));
    }

    const ReflectionRegistry& ReflectionRegistry::Get()
    {
        static const ReflectionRegistry registry;
        return registry;
    }

    const std::vector<ReflectedComponent>& ReflectionRegistry::GetComponents() const { return m_components; }

    const ReflectedComponent* ReflectionRegistry::FindComponent(const std::string& name) const
    {
        for (const auto& component : m_components) if (component.name == name) return &component;
        return nullptr;
    }
}
