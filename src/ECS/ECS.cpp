// 文件：src/ECS/ECS.cpp
// 作用：定义带代际句柄的实体、类型化组件存储、系统调度和 World 生命周期。
// 约定：实体句柄包含代际信息，禁止长期保存未校验的裸实体编号。
// 类型：实现文件。

#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/Components.hpp"

#include <algorithm>
#include <unordered_set>

namespace MiniEngine
{
// 优先复用空闲索引，但沿用该槽递增后的 generation，使旧 Entity 句柄保持失效。
Entity EntityManager::Create()
{
    u32 index = 0;
    if (!m_freeIndices.empty())
    {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
    }
    else
    {
        index = static_cast<u32>(m_generations.size());
        m_generations.push_back(1);
    }
    const Entity entity = MakeEntity(index, m_generations[index]);
    m_aliveEntities.insert(entity);
    return entity;
}

void EntityManager::Destroy(const Entity entity)
{
    const auto found = m_aliveEntities.find(entity);
    if (found == m_aliveEntities.end())
    {
        return;
    }

    m_aliveEntities.erase(found);
    const u32 index = EntityIndex(entity);
    ++m_generations[index];
    if (m_generations[index] == 0)
        m_generations[index] = 1;
    m_freeIndices.push_back(index);
}

bool EntityManager::IsAlive(const Entity entity) const
{
    const u32 index = EntityIndex(entity);
    return index > 0 && index < m_generations.size() && m_generations[index] == EntityGeneration(entity) &&
           m_aliveEntities.find(entity) != m_aliveEntities.end();
}

const std::unordered_set<Entity> &EntityManager::GetAliveEntities() const
{
    return m_aliveEntities;
}

void EntityManager::Reset()
{
    m_aliveEntities.clear();
    m_freeIndices.clear();
    m_generations.assign(1, 0);
}

void ComponentManager::RemoveAll(const Entity entity)
{
    for (auto &pair : m_storages)
    {
        pair.second->Remove(entity);
    }
}

void ComponentManager::Clear()
{
    for (auto &pair : m_storages)
        pair.second->Clear();
}

Entity World::CreateEntity(const std::string &name)
{
    const Entity entity = m_entityManager.Create();
    m_componentManager.Add(entity, NameComponent{name});
    m_componentManager.Add(entity, TransformComponent{});
    m_componentManager.Add(entity, ActiveComponent{});
    m_componentManager.Add(entity, TagComponent{});
    m_componentManager.Add(entity, LayerComponent{});
    for (auto *listener : m_listeners)
        listener->OnEntityCreated(*this, entity);
    return entity;
}

// 先通知监听器，再解除层级并删除组件；监听器因此仍可读取即将销毁实体的组件完成清理。
void World::DestroyEntity(const Entity entity)
{
    if (!m_entityManager.IsAlive(entity))
        return;
    std::vector<Entity> children;
    m_componentManager.ForEach<RelationshipComponent>([entity, &children](const Entity child, RelationshipComponent &relationship) {
        if (relationship.parent == entity)
            children.push_back(child);
    });
    for (const Entity child : children)
        DestroyEntity(child);
    for (auto *listener : m_listeners)
        listener->OnEntityDestroying(*this, entity);
    m_componentManager.RemoveAll(entity);
    m_entityManager.Destroy(entity);
}

bool World::IsAlive(const Entity entity) const
{
    return m_entityManager.IsAlive(entity);
}

void World::Clear()
{
    if (!m_entityManager.GetAliveEntities().empty())
        for (auto *listener : m_listeners)
            listener->OnWorldCleared(*this);
    m_componentManager.Clear();
    m_entityManager.Reset();
}

void World::AddListener(WorldListener *listener)
{
    if (listener && std::find(m_listeners.begin(), m_listeners.end(), listener) == m_listeners.end())
        m_listeners.push_back(listener);
}

void World::RemoveListener(WorldListener *listener)
{
    m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), listener), m_listeners.end());
}

// 沿父链检查环，禁止 child 成为自己的祖先，否则世界矩阵递归将无法终止。
bool World::SetParent(const Entity child, const Entity parent)
{
    if (!IsAlive(child) || (parent != InvalidEntity && !IsAlive(parent)) || child == parent)
        return false;
    Entity current = parent;
    while (current != InvalidEntity)
    {
        if (current == child)
            return false;
        const auto *relationship = GetComponent<RelationshipComponent>(current);
        current = relationship ? relationship->parent : InvalidEntity;
    }
    AddComponent(child, RelationshipComponent{parent});
    return true;
}

// 从当前实体向根收集局部变换后逆序相乘，避免依赖实体存储或创建顺序。
Mat4 World::GetWorldMatrix(const Entity entity) const
{
    const auto *transform = GetComponent<TransformComponent>(entity);
    if (!transform)
        return Mat4::Identity();
    Mat4 result = TransformMatrix(transform->position, transform->rotation, transform->scale);
    std::unordered_set<Entity> visited{entity};
    const auto *relationship = GetComponent<RelationshipComponent>(entity);
    Entity parent = relationship ? relationship->parent : InvalidEntity;
    while (parent != InvalidEntity && IsAlive(parent) && visited.insert(parent).second)
    {
        const auto *parentTransform = GetComponent<TransformComponent>(parent);
        if (parentTransform)
            result = TransformMatrix(parentTransform->position, parentTransform->rotation, parentTransform->scale) * result;
        const auto *parentRelationship = GetComponent<RelationshipComponent>(parent);
        parent = parentRelationship ? parentRelationship->parent : InvalidEntity;
    }
    return result;
}

void SystemManager::InitializeAll(World &world)
{
    for (const auto &system : m_systems)
    {
        system->Initialize(world);
    }
}

void SystemManager::UpdateAll(World &world, const float deltaTime)
{
    for (const auto &system : m_systems)
    {
        system->Update(world, deltaTime);
    }
}

void SystemManager::ShutdownAll(World &world)
{
    for (const auto &system : m_systems)
    {
        system->Shutdown(world);
    }
}

EntityManager &World::GetEntityManager()
{
    return m_entityManager;
}

ComponentManager &World::GetComponentManager()
{
    return m_componentManager;
}

SystemManager &World::GetSystemManager()
{
    return m_systemManager;
}

const EntityManager &World::GetEntityManager() const
{
    return m_entityManager;
}

const ComponentManager &World::GetComponentManager() const
{
    return m_componentManager;
}

const SystemManager &World::GetSystemManager() const
{
    return m_systemManager;
}

void World::Initialize()
{
    if (m_initialized)
    {
        return;
    }

    m_systemManager.InitializeAll(*this);
    m_initialized = true;
}

void World::Update(const float deltaTime)
{
    if (!m_initialized)
    {
        return;
    }

    m_systemManager.UpdateAll(*this, deltaTime);
}

void World::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    m_systemManager.ShutdownAll(*this);
    Clear();
    m_initialized = false;
}
} // namespace MiniEngine
