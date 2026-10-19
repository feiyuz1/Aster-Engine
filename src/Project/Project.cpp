// 文件：src/Project/Project.cpp
// 作用：定义项目设置、场景切换、Prefab 实例化以及编辑/运行态快照。
// 约定：编辑态和运行态场景相互隔离，运行结束后恢复进入前快照。
// 类型：实现文件。

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
bool ProjectSettings::Load(const std::filesystem::path &path)
{
    Config config;
    if (!config.Load(path.u8string()))
        return false;
    const auto nextProjectId = config.GetString("project.id");
    const auto nextCodeModule = config.GetString("code.module");
    const auto nextManifest = config.GetString("code.manifest");
    const bool nextRequired = config.GetBool("code.required", !nextCodeModule.empty());
    if ((!nextCodeModule.empty() && (nextProjectId.empty() || nextManifest.empty())) ||
        (nextCodeModule.empty() && (!nextManifest.empty() || nextRequired)))
        return false;
    projectId = nextProjectId;
    codeModule = nextCodeModule;
    codeManifest = std::filesystem::u8path(nextManifest);
    codeRequired = nextRequired;
    projectFile = std::filesystem::absolute(path).lexically_normal();
    name = config.GetString("project.name", name);
    assetDirectory = std::filesystem::u8path(config.GetString("project.assets", assetDirectory.u8string()));
    startupScene = std::filesystem::u8path(config.GetString("project.startupScene", startupScene.u8string()));
    cookedDirectory = std::filesystem::u8path(config.GetString("project.cooked", cookedDirectory.u8string()));
    packageArchive = std::filesystem::u8path(config.GetString("project.package", ""));
    inputBindings = std::filesystem::u8path(config.GetString("input.bindings", inputBindings.u8string()));
    environmentTexture = std::filesystem::u8path(config.GetString("render.environment", environmentTexture.u8string()));
    fixedStep = std::max(0.001f, config.GetFloat("physics.fixedStep", fixedStep));
    return true;
}

bool ProjectSettings::Save(const std::filesystem::path &path) const
{
    Config config;
    config.Set("project.name", name);
    if (!projectId.empty()) config.Set("project.id", projectId);
    if (!codeModule.empty())
    {
        config.Set("code.module", codeModule);
        config.Set("code.required", codeRequired ? "true" : "false");
        config.Set("code.manifest", codeManifest.generic_u8string());
    }
    config.Set("project.assets", assetDirectory.generic_u8string());
    config.Set("project.startupScene", startupScene.generic_u8string());
    config.Set("project.cooked", cookedDirectory.generic_u8string());
    if (!packageArchive.empty())
        config.Set("project.package", packageArchive.generic_u8string());
    if (!inputBindings.empty())
        config.Set("input.bindings", inputBindings.generic_u8string());
    if (!environmentTexture.empty())
        config.Set("render.environment", environmentTexture.generic_u8string());
    config.Set("physics.fixedStep", std::to_string(fixedStep));
    return config.Save(path.u8string());
}

std::filesystem::path ProjectSettings::Resolve(const std::filesystem::path &path) const
{
    if (path.is_absolute())
        return path.lexically_normal();
    return ((projectFile.empty() ? std::filesystem::current_path() : projectFile.parent_path()) / path).lexically_normal();
}

SceneManager::SceneManager(World *world) : m_world(world)
{
}
void SceneManager::SetWorld(World *world)
{
    m_world = world;
}
void SceneManager::NewScene()
{
    if (m_world)
        m_world->Clear();
    m_currentScene.clear();
}
// 反序列化器只有在完整解析成功后才替换 World，加载失败保持当前场景可继续编辑。
bool SceneManager::LoadScene(const std::filesystem::path &path)
{
    if (!m_world || !SceneSerializer::Load(*m_world, path))
        return false;
    m_currentScene = path;
    return true;
}
bool SceneManager::SaveScene(const std::filesystem::path &path)
{
    if (!m_world)
        return false;
    const auto target = path.empty() ? m_currentScene : path;
    if (target.empty() || !SceneSerializer::Save(*m_world, target))
        return false;
    m_currentScene = target;
    return true;
}
std::string SceneManager::CreateSnapshot() const
{
    return m_world ? SceneSerializer::Serialize(*m_world) : std::string{};
}
bool SceneManager::RestoreSnapshot(const std::string &snapshot)
{
    return m_world && SceneSerializer::Deserialize(*m_world, snapshot);
}
const std::filesystem::path &SceneManager::GetCurrentScene() const
{
    return m_currentScene;
}

namespace
{
template <typename T> void CopyComponent(const World &source, Entity from, World &destination, Entity to)
{
    if (const auto *component = source.GetComponent<T>(from))
        destination.AddComponent(to, *component);
}

Entity CopyEntity(const World &source, Entity from, World &destination, std::unordered_map<Entity, Entity> &mapping)
{
    const auto *name = source.GetComponent<NameComponent>(from);
    const Entity to = destination.CreateEntity(name ? name->name : "Entity");
    mapping[from] = to;
    CopyComponent<TransformComponent>(source, from, destination, to);
    CopyComponent<ActiveComponent>(source, from, destination, to);
    CopyComponent<TagComponent>(source, from, destination, to);
    CopyComponent<LayerComponent>(source, from, destination, to);
    CopyComponent<VelocityComponent>(source, from, destination, to);
    CopyComponent<MeshComponent>(source, from, destination, to);
    CopyComponent<LodComponent>(source, from, destination, to);
    CopyComponent<CameraComponent>(source, from, destination, to);
    CopyComponent<LightComponent>(source, from, destination, to);
    CopyComponent<ScriptComponent>(source, from, destination, to);
    CopyComponent<PawnComponent>(source, from, destination, to);
    CopyComponent<CharacterControllerComponent>(source, from, destination, to);
    CopyComponent<RigidBodyComponent>(source, from, destination, to);
    CopyComponent<ColliderComponent>(source, from, destination, to);
    CopyComponent<JointComponent>(source, from, destination, to);
    CopyComponent<AnimationComponent>(source, from, destination, to);
    CopyComponent<AudioSourceComponent>(source, from, destination, to);
    return to;
}
} // 匿名命名空间

bool Prefab::Save(const World &world, const Entity root, const std::filesystem::path &path)
{
    if (!world.IsAlive(root))
        return false;
    std::unordered_set<Entity> included{root};
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const Entity entity : world.GetEntityManager().GetAliveEntities())
            if (const auto *relation = world.GetComponent<RelationshipComponent>(entity);
                relation && included.count(relation->parent) && included.insert(entity).second)
                changed = true;
    }
    World prefabWorld;
    std::unordered_map<Entity, Entity> mapping;
    for (const Entity entity : included)
        CopyEntity(world, entity, prefabWorld, mapping);
    for (const Entity entity : included)
        if (const auto *relation = world.GetComponent<RelationshipComponent>(entity); relation && included.count(relation->parent))
            prefabWorld.SetParent(mapping[entity], mapping[relation->parent]);
    for (const auto &pair : mapping)
        if (auto *joint = prefabWorld.GetComponent<JointComponent>(pair.second))
            joint->connectedEntity = mapping.count(joint->connectedEntity) ? mapping[joint->connectedEntity] : InvalidEntity;
    return SceneSerializer::Save(prefabWorld, path);
}

// 先创建全部实体并建立旧到新编号映射，再恢复父子和组件引用，避免引用尚未创建的目标。
std::vector<Entity> Prefab::Instantiate(World &world, const std::filesystem::path &path, const Entity parent)
{
    World prefabWorld;
    if (!SceneSerializer::Load(prefabWorld, path) || (parent != InvalidEntity && !world.IsAlive(parent)))
        return {};
    std::unordered_map<Entity, Entity> mapping;
    std::vector<Entity> roots;
    for (const Entity entity : prefabWorld.GetEntityManager().GetAliveEntities())
        CopyEntity(prefabWorld, entity, world, mapping);
    for (const auto &pair : mapping)
    {
        const auto *relation = prefabWorld.GetComponent<RelationshipComponent>(pair.first);
        if (relation && mapping.count(relation->parent))
            world.SetParent(pair.second, mapping[relation->parent]);
        else
        {
            roots.push_back(pair.second);
            if (parent != InvalidEntity)
                world.SetParent(pair.second, parent);
        }
    }
    for (const auto &pair : mapping)
        if (auto *joint = world.GetComponent<JointComponent>(pair.second))
            joint->connectedEntity = mapping.count(joint->connectedEntity) ? mapping[joint->connectedEntity] : InvalidEntity;
    return roots;
}

void SaveGame::Set(std::string key, std::string value)
{
    m_data.Set(std::move(key), std::move(value));
}
std::string SaveGame::GetString(const std::string &key, const std::string &fallback) const
{
    return m_data.GetString(key, fallback);
}
int SaveGame::GetInt(const std::string &key, const int fallback) const
{
    return m_data.GetInt(key, fallback);
}
float SaveGame::GetFloat(const std::string &key, const float fallback) const
{
    return m_data.GetFloat(key, fallback);
}
bool SaveGame::GetBool(const std::string &key, const bool fallback) const
{
    return m_data.GetBool(key, fallback);
}
bool SaveGame::Save(const std::filesystem::path &path) const
{
    return m_data.Save(path.string());
}
bool SaveGame::Load(const std::filesystem::path &path)
{
    return m_data.Load(path.string());
}
} // 结束 MiniEngine 命名空间
