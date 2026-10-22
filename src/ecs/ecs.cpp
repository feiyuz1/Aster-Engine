// 文件：src/ecs/ecs.cpp
// 作用：定义带代际句柄的实体、类型化组件存储、系统调度和 World 生命周期。
// 约定：实体句柄包含代际信息，禁止长期保存未校验的裸实体编号。
// 类型：实现文件。

#include "tinyengine/ecs/ecs.h"
#include "tinyengine/ecs/components.h"

#include <algorithm>
#include <unordered_set>

namespace tinyengine
{
// 优先复用空闲索引，但沿用该槽递增后的 generation，使旧 Entity 句柄保持失效。
Entity EntityManager::Create()
{
    std::uint32_t index = 0;
    if (!m_freeIndices.empty())
    {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
    }
    else
    {
        index = static_cast<std::uint32_t>(m_generations.size());
        m_generations.push_back(1);
    }
    const Entity entity = makeEntity(index, m_generations[index]);
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
    const std::uint32_t index = entityIndex(entity);
    ++m_generations[index];
    if (m_generations[index] == 0)
        m_generations[index] = 1;
    m_freeIndices.push_back(index);
}

bool EntityManager::isAlive(const Entity entity) const
{
    const std::uint32_t index = entityIndex(entity);
    return index > 0 && index < m_generations.size() && m_generations[index] == entityGeneration(entity) &&
           m_aliveEntities.find(entity) != m_aliveEntities.end();
}

const std::unordered_set<Entity>& EntityManager::GetAliveEntities() const
{
    return m_aliveEntities;
}

void EntityManager::reset()
{
    m_aliveEntities.clear();
    m_freeIndices.clear();
    // 保留已分配槽位的代数历史，防止替换场景后旧句柄重新生效。
    for (std::size_t index = m_generations.size(); index > 1; --index)
    {
        auto& generation = m_generations[index - 1];
        if (++generation == 0)
            generation = 1;
        m_freeIndices.push_back(static_cast<std::uint32_t>(index - 1));
    }
}

void ComponentManager::removeAll(const Entity entity)
{
    for (auto& pair : m_storages)
    {
        pair.second->Remove(entity);
    }
}

void ComponentManager::clear()
{
    for (auto& pair : m_storages)
        pair.second->clear();
}

Entity World::createEntity(const std::string& name)
{
    const Entity entity = m_entityManager.Create();
    m_componentManager.Add(entity, NameComponent{name});
    m_componentManager.Add(entity, TransformComponent{});
    m_componentManager.Add(entity, ActiveComponent{});
    m_componentManager.Add(entity, TagComponent{});
    m_componentManager.Add(entity, LayerComponent{});
    for (auto* listener : m_listeners)
        listener->onEntityCreated(*this, entity);
    return entity;
}

// 先通知监听器，再解除层级并删除组件；监听器因此仍可读取即将销毁实体的组件完成清理。
void World::destroyEntity(const Entity entity)
{
    if (!m_entityManager.isAlive(entity))
        return;
    std::vector<Entity> children;
    m_componentManager.ForEach<RelationshipComponent>(
        [entity, &children](const Entity child, RelationshipComponent& relationship)
        {
            if (relationship.parent == entity)
                children.push_back(child);
        });
    for (const Entity child : children)
        destroyEntity(child);
    for (auto* listener : m_listeners)
        listener->onEntityDestroying(*this, entity);
    m_componentManager.removeAll(entity);
    m_entityManager.Destroy(entity);
}

bool World::isAlive(const Entity entity) const
{
    return m_entityManager.isAlive(entity);
}

void World::clear()
{
    // Empty resets also begin a new world epoch for external handles.
    for (auto* listener : m_listeners)
        listener->onWorldCleared(*this);
    m_componentManager.clear();
    m_entityManager.reset();
}

void World::addListener(WorldListener* listener)
{
    if (listener && std::find(m_listeners.begin(), m_listeners.end(), listener) == m_listeners.end())
        m_listeners.push_back(listener);
}

void World::removeListener(WorldListener* listener)
{
    m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), listener), m_listeners.end());
}

// 沿父链检查环，禁止 child 成为自己的祖先，否则世界矩阵递归将无法终止。
bool World::setParent(const Entity child, const Entity parent)
{
    if (!isAlive(child) || (parent != kInvalidEntity && !isAlive(parent)) || child == parent)
        return false;
    Entity current = parent;
    while (current != kInvalidEntity)
    {
        if (current == child)
            return false;
        const auto* relationship = GetComponent<RelationshipComponent>(current);
        current = relationship ? relationship->parent : kInvalidEntity;
    }
    AddComponent(child, RelationshipComponent{parent});
    return true;
}

// 从当前实体向根收集局部变换后逆序相乘，避免依赖实体存储或创建顺序。
Mat4 World::getWorldMatrix(const Entity entity) const
{
    const auto* transform = GetComponent<TransformComponent>(entity);
    if (!transform)
        return Mat4::identity();
    Mat4 result = transformMatrix(transform->position, transform->rotation, transform->scale);
    std::unordered_set<Entity> visited{entity};
    const auto* relationship = GetComponent<RelationshipComponent>(entity);
    Entity parent = relationship ? relationship->parent : kInvalidEntity;
    while (parent != kInvalidEntity && isAlive(parent) && visited.insert(parent).second)
    {
        const auto* parentTransform = GetComponent<TransformComponent>(parent);
        if (parentTransform)
            result =
                transformMatrix(parentTransform->position, parentTransform->rotation, parentTransform->scale) * result;
        const auto* parentRelationship = GetComponent<RelationshipComponent>(parent);
        parent = parentRelationship ? parentRelationship->parent : kInvalidEntity;
    }
    return result;
}

void SystemManager::initializeAll(World& world)
{
    for (const auto& system : m_systems)
    {
        system->initialize(world);
    }
}

void SystemManager::updateAll(World& world, const float deltaTime)
{
    for (const auto& system : m_systems)
    {
        system->Update(world, deltaTime);
    }
}

void SystemManager::shutdownAll(World& world)
{
    for (const auto& system : m_systems)
    {
        system->shutdown(world);
    }
}

EntityManager& World::GetEntityManager()
{
    return m_entityManager;
}

ComponentManager& World::GetComponentManager()
{
    return m_componentManager;
}

SystemManager& World::GetSystemManager()
{
    return m_systemManager;
}

const EntityManager& World::GetEntityManager() const
{
    return m_entityManager;
}

const ComponentManager& World::GetComponentManager() const
{
    return m_componentManager;
}

const SystemManager& World::GetSystemManager() const
{
    return m_systemManager;
}

void World::initialize()
{
    if (m_initialized)
    {
        return;
    }

    m_systemManager.initializeAll(*this);
    m_initialized = true;
}

void World::Update(const float deltaTime)
{
    if (!m_initialized)
    {
        return;
    }

    m_systemManager.updateAll(*this, deltaTime);
}

void World::shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    m_systemManager.shutdownAll(*this);
    clear();
    m_initialized = false;
}
} // namespace tinyengine
