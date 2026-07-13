#include "MiniEngine/ECS/ECS.hpp"

namespace MiniEngine
{
    Entity EntityManager::Create()
    {
        Entity entity = InvalidEntity;
        if (!m_freeEntities.empty())
        {
            entity = m_freeEntities.back();
            m_freeEntities.pop_back();
        }
        else
        {
            entity = m_nextEntity++;
        }

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
        m_freeEntities.push_back(entity);
    }

    bool EntityManager::IsAlive(const Entity entity) const
    {
        return m_aliveEntities.find(entity) != m_aliveEntities.end();
    }

    const std::unordered_set<Entity>& EntityManager::GetAliveEntities() const
    {
        return m_aliveEntities;
    }

    void ComponentManager::RemoveAll(const Entity entity)
    {
        for (auto& pair : m_storages)
        {
            pair.second->Remove(entity);
        }
    }

    void SystemManager::InitializeAll(World& world)
    {
        for (const auto& system : m_systems)
        {
            system->Initialize(world);
        }
    }

    void SystemManager::UpdateAll(World& world, const float deltaTime)
    {
        for (const auto& system : m_systems)
        {
            system->Update(world, deltaTime);
        }
    }

    void SystemManager::ShutdownAll(World& world)
    {
        for (const auto& system : m_systems)
        {
            system->Shutdown(world);
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
        m_initialized = false;
    }
}
