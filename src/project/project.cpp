// 文件：src/project/project.cpp
// 作用：定义项目设置、场景切换、Prefab 实例化以及编辑/运行态快照。
// 约定：编辑态和运行态场景相互隔离，运行结束后恢复进入前快照。
// 类型：实现文件。

#include "tinyengine/project/project.h"

#include "tinyengine/core/config.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/ecs/scene_serializer.h"

#include "tinyengine/platform/platform.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace tinyengine
{
bool ProjectSettings::load(const std::filesystem::path& path)
{
    Config config;
    if (!config.load(toUtf8Path(path)))
        return false;
    const auto nextProjectId = config.getString("project.id");
    const auto nextCodeModule = config.getString("code.module");
    const auto nextManifest = config.getString("code.manifest");
    const bool nextRequired = config.getBool("code.required", !nextCodeModule.empty());
    if ((!nextCodeModule.empty() && (nextProjectId.empty() || nextManifest.empty())) ||
        (nextCodeModule.empty() && (!nextManifest.empty() || nextRequired)))
        return false;
    projectId = nextProjectId;
    codeModule = nextCodeModule;
    codeManifest = fromUtf8Path(nextManifest);
    codeRequired = nextRequired;
    projectFile = std::filesystem::absolute(path).lexically_normal();
    name = config.getString("project.name", name);
    assetDirectory = fromUtf8Path(config.getString("project.assets", toUtf8Path(assetDirectory)));
    startupScene = fromUtf8Path(config.getString("project.startupScene", toUtf8Path(startupScene)));
    cookedDirectory = fromUtf8Path(config.getString("project.cooked", toUtf8Path(cookedDirectory)));
    packageArchive = fromUtf8Path(config.getString("project.package", ""));
    inputBindings = fromUtf8Path(config.getString("input.bindings", toUtf8Path(inputBindings)));
    environmentTexture = fromUtf8Path(config.getString("render.environment", toUtf8Path(environmentTexture)));
    fixedStep = std::max(0.001f, config.getFloat("physics.fixedStep", fixedStep));
    return true;
}

bool ProjectSettings::Save(const std::filesystem::path& path) const
{
    Config config;
    config.set("project.name", name);
    if (!projectId.empty())
        config.set("project.id", projectId);
    if (!codeModule.empty())
    {
        config.set("code.module", codeModule);
        config.set("code.required", codeRequired ? "true" : "false");
        config.set("code.manifest", toUtf8GenericPath(codeManifest));
    }
    config.set("project.assets", toUtf8GenericPath(assetDirectory));
    config.set("project.startupScene", toUtf8GenericPath(startupScene));
    config.set("project.cooked", toUtf8GenericPath(cookedDirectory));
    if (!packageArchive.empty())
        config.set("project.package", toUtf8GenericPath(packageArchive));
    if (!inputBindings.empty())
        config.set("input.bindings", toUtf8GenericPath(inputBindings));
    if (!environmentTexture.empty())
        config.set("render.environment", toUtf8GenericPath(environmentTexture));
    config.set("physics.fixedStep", std::to_string(fixedStep));
    return config.Save(toUtf8Path(path));
}

std::filesystem::path ProjectSettings::resolve(const std::filesystem::path& path) const
{
    if (path.is_absolute())
        return path.lexically_normal();
    return ((projectFile.empty() ? std::filesystem::current_path() : projectFile.parent_path()) / path)
        .lexically_normal();
}

SceneManager::SceneManager(World* world) : m_world(world) {}
void SceneManager::setWorld(World* world)
{
    m_world = world;
}
void SceneManager::newScene()
{
    if (m_world)
        m_world->clear();
    m_currentScene.clear();
}
// 反序列化器只有在完整解析成功后才替换 World，加载失败保持当前场景可继续编辑。
bool SceneManager::loadScene(const std::filesystem::path& path)
{
    if (!m_world || !SceneSerializer::load(*m_world, path))
        return false;
    m_currentScene = path;
    return true;
}
bool SceneManager::saveScene(const std::filesystem::path& path)
{
    if (!m_world)
        return false;
    const auto target = path.empty() ? m_currentScene : path;
    if (target.empty() || !SceneSerializer::Save(*m_world, target))
        return false;
    m_currentScene = target;
    return true;
}
std::string SceneManager::createSnapshot() const
{
    return m_world ? SceneSerializer::serialize(*m_world) : std::string{};
}
bool SceneManager::restoreSnapshot(const std::string& snapshot)
{
    return m_world && SceneSerializer::deserialize(*m_world, snapshot);
}
const std::filesystem::path& SceneManager::GetCurrentScene() const
{
    return m_currentScene;
}

namespace
{
template <typename T> void CopyComponent(const World& source, Entity from, World& destination, Entity to)
{
    if (const auto* component = source.GetComponent<T>(from))
        destination.AddComponent(to, *component);
}

Entity copyEntity(const World& source, Entity from, World& destination, std::unordered_map<Entity, Entity>& mapping)
{
    const auto* name = source.GetComponent<NameComponent>(from);
    const Entity to = destination.createEntity(name ? name->name : "Entity");
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
} // namespace

bool Prefab::Save(const World& world, const Entity root, const std::filesystem::path& path)
{
    if (!world.isAlive(root))
        return false;
    std::unordered_set<Entity> included{root};
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const Entity entity : world.GetEntityManager().GetAliveEntities())
            if (const auto* relation = world.GetComponent<RelationshipComponent>(entity);
                relation && included.count(relation->parent) && included.insert(entity).second)
                changed = true;
    }
    World prefabWorld;
    std::unordered_map<Entity, Entity> mapping;
    for (const Entity entity : included)
        copyEntity(world, entity, prefabWorld, mapping);
    for (const Entity entity : included)
        if (const auto* relation = world.GetComponent<RelationshipComponent>(entity);
            relation && included.count(relation->parent))
            prefabWorld.setParent(mapping[entity], mapping[relation->parent]);
    for (const auto& pair : mapping)
        if (auto* joint = prefabWorld.GetComponent<JointComponent>(pair.second))
            joint->connectedEntity =
                mapping.count(joint->connectedEntity) ? mapping[joint->connectedEntity] : kInvalidEntity;
    return SceneSerializer::Save(prefabWorld, path);
}

// 先创建全部实体并建立旧到新编号映射，再恢复父子和组件引用，避免引用尚未创建的目标。
std::vector<Entity> Prefab::Instantiate(World& world, const std::filesystem::path& path, const Entity parent)
{
    World prefabWorld;
    if (!SceneSerializer::load(prefabWorld, path) || (parent != kInvalidEntity && !world.isAlive(parent)))
        return {};
    std::unordered_map<Entity, Entity> mapping;
    std::vector<Entity> roots;
    for (const Entity entity : prefabWorld.GetEntityManager().GetAliveEntities())
        copyEntity(prefabWorld, entity, world, mapping);
    for (const auto& pair : mapping)
    {
        const auto* relation = prefabWorld.GetComponent<RelationshipComponent>(pair.first);
        if (relation && mapping.count(relation->parent))
            world.setParent(pair.second, mapping[relation->parent]);
        else
        {
            roots.push_back(pair.second);
            if (parent != kInvalidEntity)
                world.setParent(pair.second, parent);
        }
    }
    for (const auto& pair : mapping)
        if (auto* joint = world.GetComponent<JointComponent>(pair.second))
            joint->connectedEntity =
                mapping.count(joint->connectedEntity) ? mapping[joint->connectedEntity] : kInvalidEntity;
    return roots;
}

void SaveGame::set(std::string key, std::string value)
{
    m_data.set(std::move(key), std::move(value));
}
std::string SaveGame::getString(const std::string& key, const std::string& fallback) const
{
    return m_data.getString(key, fallback);
}
int SaveGame::getInt(const std::string& key, const int fallback) const
{
    return m_data.getInt(key, fallback);
}
float SaveGame::getFloat(const std::string& key, const float fallback) const
{
    return m_data.getFloat(key, fallback);
}
bool SaveGame::getBool(const std::string& key, const bool fallback) const
{
    return m_data.getBool(key, fallback);
}
bool SaveGame::Save(const std::filesystem::path& path) const
{
    return m_data.Save(path.string());
}
bool SaveGame::load(const std::filesystem::path& path)
{
    return m_data.load(path.string());
}
} // namespace tinyengine
