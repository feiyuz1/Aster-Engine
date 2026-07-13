#include "MiniEngine/Project/Project.hpp"

#include "MiniEngine/Core/Config.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace MiniEngine
{
    bool ProjectSettings::Load(const std::filesystem::path& path)
    {
        Config config;
        if (!config.Load(path.string())) return false;
        projectFile = std::filesystem::absolute(path).lexically_normal();
        name = config.GetString("project.name", name);
        assetDirectory = config.GetString("project.assets", assetDirectory.string());
        startupScene = config.GetString("project.startupScene", startupScene.string());
        cookedDirectory = config.GetString("project.cooked", cookedDirectory.string());
        fixedStep = std::max(0.001f, config.GetFloat("physics.fixedStep", fixedStep));
        return true;
    }

    bool ProjectSettings::Save(const std::filesystem::path& path) const
    {
        Config config;
        config.Set("project.name", name);
        config.Set("project.assets", assetDirectory.generic_string());
        config.Set("project.startupScene", startupScene.generic_string());
        config.Set("project.cooked", cookedDirectory.generic_string());
        config.Set("physics.fixedStep", std::to_string(fixedStep));
        return config.Save(path.string());
    }

    std::filesystem::path ProjectSettings::Resolve(const std::filesystem::path& path) const
    {
        if (path.is_absolute()) return path.lexically_normal();
        return ((projectFile.empty() ? std::filesystem::current_path() : projectFile.parent_path()) / path).lexically_normal();
    }

    SceneManager::SceneManager(World* world) : m_world(world) {}
    void SceneManager::SetWorld(World* world) { m_world = world; }
    void SceneManager::NewScene() { if (m_world) m_world->Clear(); m_currentScene.clear(); }
    bool SceneManager::LoadScene(const std::filesystem::path& path)
    {
        if (!m_world || !SceneSerializer::Load(*m_world, path)) return false;
        m_currentScene = path; return true;
    }
    bool SceneManager::SaveScene(const std::filesystem::path& path)
    {
        if (!m_world) return false;
        const auto target = path.empty() ? m_currentScene : path;
        if (target.empty() || !SceneSerializer::Save(*m_world, target)) return false;
        m_currentScene = target; return true;
    }
    std::string SceneManager::CreateSnapshot() const { return m_world ? SceneSerializer::Serialize(*m_world) : std::string{}; }
    bool SceneManager::RestoreSnapshot(const std::string& snapshot) { return m_world && SceneSerializer::Deserialize(*m_world, snapshot); }
    const std::filesystem::path& SceneManager::GetCurrentScene() const { return m_currentScene; }

    namespace
    {
        template<typename T> void CopyComponent(const World& source, Entity from, World& destination, Entity to)
        {
            if (const auto* component = source.GetComponent<T>(from)) destination.AddComponent(to, *component);
        }

        Entity CopyEntity(const World& source, Entity from, World& destination, std::unordered_map<Entity, Entity>& mapping)
        {
            const auto* name = source.GetComponent<NameComponent>(from);
            const Entity to = destination.CreateEntity(name ? name->name : "Entity");
            mapping[from] = to;
            CopyComponent<TransformComponent>(source, from, destination, to);
            CopyComponent<ActiveComponent>(source, from, destination, to);
            CopyComponent<TagComponent>(source, from, destination, to);
            CopyComponent<LayerComponent>(source, from, destination, to);
            CopyComponent<VelocityComponent>(source, from, destination, to);
            CopyComponent<MeshComponent>(source, from, destination, to);
            CopyComponent<CameraComponent>(source, from, destination, to);
            CopyComponent<LightComponent>(source, from, destination, to);
            CopyComponent<ScriptComponent>(source, from, destination, to);
            CopyComponent<RigidBodyComponent>(source, from, destination, to);
            CopyComponent<ColliderComponent>(source, from, destination, to);
            CopyComponent<AnimationComponent>(source, from, destination, to);
            CopyComponent<AudioSourceComponent>(source, from, destination, to);
            return to;
        }
    }

    bool Prefab::Save(const World& world, const Entity root, const std::filesystem::path& path)
    {
        if (!world.IsAlive(root)) return false;
        std::unordered_set<Entity> included{root};
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (const Entity entity : world.GetEntityManager().GetAliveEntities())
                if (const auto* relation = world.GetComponent<RelationshipComponent>(entity); relation && included.count(relation->parent) && included.insert(entity).second) changed = true;
        }
        World prefabWorld;
        std::unordered_map<Entity, Entity> mapping;
        for (const Entity entity : included) CopyEntity(world, entity, prefabWorld, mapping);
        for (const Entity entity : included)
            if (const auto* relation = world.GetComponent<RelationshipComponent>(entity); relation && included.count(relation->parent)) prefabWorld.SetParent(mapping[entity], mapping[relation->parent]);
        return SceneSerializer::Save(prefabWorld, path);
    }

    std::vector<Entity> Prefab::Instantiate(World& world, const std::filesystem::path& path, const Entity parent)
    {
        World prefabWorld;
        if (!SceneSerializer::Load(prefabWorld, path) || (parent != InvalidEntity && !world.IsAlive(parent))) return {};
        std::unordered_map<Entity, Entity> mapping;
        std::vector<Entity> roots;
        for (const Entity entity : prefabWorld.GetEntityManager().GetAliveEntities()) CopyEntity(prefabWorld, entity, world, mapping);
        for (const auto& pair : mapping)
        {
            const auto* relation = prefabWorld.GetComponent<RelationshipComponent>(pair.first);
            if (relation && mapping.count(relation->parent)) world.SetParent(pair.second, mapping[relation->parent]);
            else { roots.push_back(pair.second); if (parent != InvalidEntity) world.SetParent(pair.second, parent); }
        }
        return roots;
    }
}
