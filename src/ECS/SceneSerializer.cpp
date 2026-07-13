#include "MiniEngine/ECS/SceneSerializer.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace MiniEngine
{
    std::string SceneSerializer::Serialize(const World& world)
    {
        std::vector<Entity> entities(world.GetEntityManager().GetAliveEntities().begin(), world.GetEntityManager().GetAliveEntities().end());
        std::sort(entities.begin(), entities.end());
        std::unordered_map<Entity, std::size_t> ids;
        for (std::size_t index = 0; index < entities.size(); ++index) ids[entities[index]] = index + 1;
        std::ostringstream out;
        out << "TINYSCENE 2\n";
        for (const Entity entity : entities)
        {
            const auto* name = world.GetComponent<NameComponent>(entity);
            const auto* relationship = world.GetComponent<RelationshipComponent>(entity);
            const std::size_t parent = relationship && ids.count(relationship->parent) ? ids[relationship->parent] : 0;
            out << "entity " << ids[entity] << ' ' << parent << ' ' << std::quoted(name ? name->name : "Entity") << '\n';
            if (const auto* v = world.GetComponent<ActiveComponent>(entity)) out << "active " << v->active << '\n';
            if (const auto* v = world.GetComponent<TagComponent>(entity)) out << "tag " << std::quoted(v->tag) << '\n';
            if (const auto* v = world.GetComponent<LayerComponent>(entity)) out << "layer " << v->layer << '\n';
            if (const auto* v = world.GetComponent<TransformComponent>(entity))
                out << "transform " << v->position.x << ' ' << v->position.y << ' ' << v->position.z << ' ' << v->rotation.x << ' ' << v->rotation.y << ' ' << v->rotation.z << ' ' << v->scale.x << ' ' << v->scale.y << ' ' << v->scale.z << '\n';
            if (const auto* v = world.GetComponent<MeshComponent>(entity)) out << "mesh " << std::quoted(v->meshPath) << ' ' << std::quoted(v->materialPath) << '\n';
            if (const auto* v = world.GetComponent<CameraComponent>(entity)) out << "camera " << v->fov << ' ' << v->nearPlane << ' ' << v->farPlane << ' ' << v->primary << '\n';
            if (const auto* v = world.GetComponent<LightComponent>(entity)) out << "light " << v->color.x << ' ' << v->color.y << ' ' << v->color.z << ' ' << v->intensity << ' ' << v->direction.x << ' ' << v->direction.y << ' ' << v->direction.z << '\n';
            if (const auto* v = world.GetComponent<RigidBodyComponent>(entity)) out << "rigidbody " << static_cast<int>(v->type) << ' ' << v->mass << ' ' << v->velocity.x << ' ' << v->velocity.y << ' ' << v->velocity.z << ' ' << v->useGravity << '\n';
            if (const auto* v = world.GetComponent<ColliderComponent>(entity)) out << "collider " << static_cast<int>(v->shape) << ' ' << v->halfExtents.x << ' ' << v->halfExtents.y << ' ' << v->halfExtents.z << ' ' << v->radius << ' ' << v->trigger << ' ' << v->layer << ' ' << v->mask << '\n';
            if (const auto* v = world.GetComponent<ScriptComponent>(entity)) out << "script " << std::quoted(v->scriptClassName) << '\n';
            if (const auto* v = world.GetComponent<AnimationComponent>(entity)) out << "animation " << std::quoted(v->clipPath) << ' ' << v->time << ' ' << v->speed << ' ' << v->loop << ' ' << v->playing << '\n';
            if (const auto* v = world.GetComponent<AudioSourceComponent>(entity)) out << "audio " << std::quoted(v->clipPath) << ' ' << v->volume << ' ' << v->loop << ' ' << v->playOnStart << '\n';
            out << "end\n";
        }
        return out.str();
    }

    bool SceneSerializer::Save(const World& world, const std::filesystem::path& path)
    {
        return PlatformFile::WriteAllText(path, Serialize(world));
    }

    bool SceneSerializer::Deserialize(World& world, const std::string& text)
    {
        std::istringstream in(text);
        std::string token;
        int version = 0;
        if (!(in >> token >> version) || token != "TINYSCENE" || (version != 1 && version != 2)) return false;
        world.Clear();
        std::unordered_map<std::size_t, Entity> ids;
        std::vector<std::pair<Entity, std::size_t>> parents;
        Entity current = InvalidEntity;
        while (in >> token)
        {
            if (token == "entity")
            {
                std::size_t id = 0, parent = 0; std::string name;
                if (!(in >> id >> parent >> std::quoted(name))) return false;
                current = world.CreateEntity(name); ids[id] = current; parents.emplace_back(current, parent);
            }
            else if (current == InvalidEntity) return false;
            else if (token == "active") { auto* v = world.GetComponent<ActiveComponent>(current); if (!v || !(in >> v->active)) return false; }
            else if (token == "tag") { auto* v = world.GetComponent<TagComponent>(current); if (!v || !(in >> std::quoted(v->tag))) return false; }
            else if (token == "layer") { auto* v = world.GetComponent<LayerComponent>(current); if (!v || !(in >> v->layer)) return false; }
            else if (token == "transform")
            {
                auto* v = world.GetComponent<TransformComponent>(current);
                if (!v || !(in >> v->position.x >> v->position.y >> v->position.z >> v->rotation.x >> v->rotation.y >> v->rotation.z >> v->scale.x >> v->scale.y >> v->scale.z)) return false;
            }
            else if (token == "mesh") { MeshComponent v; in >> std::quoted(v.meshPath) >> std::quoted(v.materialPath); world.AddComponent(current, v); }
            else if (token == "camera") { CameraComponent v; in >> v.fov >> v.nearPlane >> v.farPlane >> v.primary; world.AddComponent(current, v); }
            else if (token == "light") { LightComponent v; in >> v.color.x >> v.color.y >> v.color.z >> v.intensity >> v.direction.x >> v.direction.y >> v.direction.z; world.AddComponent(current, v); }
            else if (token == "rigidbody") { RigidBodyComponent v; int type; in >> type >> v.mass >> v.velocity.x >> v.velocity.y >> v.velocity.z >> v.useGravity; v.type = static_cast<BodyType>(type); world.AddComponent(current, v); }
            else if (token == "collider") { ColliderComponent v; int shape; in >> shape >> v.halfExtents.x >> v.halfExtents.y >> v.halfExtents.z >> v.radius >> v.trigger >> v.layer >> v.mask; v.shape = static_cast<ColliderShape>(shape); world.AddComponent(current, v); }
            else if (token == "script") { ScriptComponent v; in >> std::quoted(v.scriptClassName); world.AddComponent(current, v); }
            else if (token == "animation") { AnimationComponent v; in >> std::quoted(v.clipPath) >> v.time >> v.speed >> v.loop >> v.playing; world.AddComponent(current, v); }
            else if (token == "audio") { AudioSourceComponent v; in >> std::quoted(v.clipPath) >> v.volume >> v.loop >> v.playOnStart; world.AddComponent(current, v); }
            else if (token == "end") current = InvalidEntity;
            else return false;
            if (!in) return false;
        }
        for (const auto& pair : parents)
            if (pair.second != 0 && ids.count(pair.second)) world.SetParent(pair.first, ids[pair.second]);
        return true;
    }

    bool SceneSerializer::Load(World& world, const std::filesystem::path& path)
    {
        const std::string text = PlatformFile::ReadAllText(path);
        return !text.empty() && Deserialize(world, text);
    }
}
