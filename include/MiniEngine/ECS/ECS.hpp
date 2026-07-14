#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Types.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace MiniEngine
{
class EntityManager
{
  public:
    Entity Create();
    void Destroy(Entity entity);
    bool IsAlive(Entity entity) const;
    const std::unordered_set<Entity> &GetAliveEntities() const;
    void Reset();

  private:
    std::vector<u32> m_generations{0};
    std::vector<u32> m_freeIndices;
    std::unordered_set<Entity> m_aliveEntities;
};

class IComponentStorage
{
  public:
    virtual ~IComponentStorage() = default;
    virtual void Remove(Entity entity) = 0;
    virtual bool Has(Entity entity) const = 0;
    virtual void Clear() = 0;
};

template <typename T> class ComponentStorage final : public IComponentStorage
{
  public:
    void Add(Entity entity, const T &component)
    {
        m_data[entity] = component;
    }

    void Remove(Entity entity) override
    {
        m_data.erase(entity);
    }

    bool Has(Entity entity) const override
    {
        return m_data.find(entity) != m_data.end();
    }

    void Clear() override
    {
        m_data.clear();
    }

    T *Get(Entity entity)
    {
        const auto found = m_data.find(entity);
        return found != m_data.end() ? &found->second : nullptr;
    }

    const T *Get(Entity entity) const
    {
        const auto found = m_data.find(entity);
        return found != m_data.end() ? &found->second : nullptr;
    }

    std::unordered_map<Entity, T> &Data()
    {
        return m_data;
    }

    const std::unordered_map<Entity, T> &Data() const
    {
        return m_data;
    }

  private:
    std::unordered_map<Entity, T> m_data;
};

class ComponentManager
{
  public:
    template <typename T> void Add(Entity entity, const T &component)
    {
        GetOrCreateStorage<T>()->Add(entity, component);
    }

    template <typename T> void Remove(Entity entity)
    {
        auto *storage = FindStorage<T>();
        if (storage != nullptr)
        {
            storage->Remove(entity);
        }
    }

    template <typename T> T *Get(Entity entity)
    {
        auto *storage = FindStorage<T>();
        return storage != nullptr ? storage->Get(entity) : nullptr;
    }

    template <typename T> const T *Get(Entity entity) const
    {
        const auto *storage = FindStorage<T>();
        return storage != nullptr ? storage->Get(entity) : nullptr;
    }

    template <typename T> bool Has(Entity entity) const
    {
        const auto *storage = FindStorage<T>();
        return storage != nullptr && storage->Has(entity);
    }

    template <typename T, typename Func> void ForEach(Func &&func)
    {
        auto *storage = FindStorage<T>();
        if (storage == nullptr)
        {
            return;
        }

        for (auto &pair : storage->Data())
        {
            func(pair.first, pair.second);
        }
    }

    template <typename TFirst, typename TSecond, typename Func> void ForEach(Func &&func)
    {
        auto *first = FindStorage<TFirst>();
        auto *second = FindStorage<TSecond>();
        if (first == nullptr || second == nullptr)
            return;
        if (first->Data().size() <= second->Data().size())
        {
            for (auto &pair : first->Data())
                if (auto *value = second->Get(pair.first))
                    func(pair.first, pair.second, *value);
        }
        else
        {
            for (auto &pair : second->Data())
                if (auto *value = first->Get(pair.first))
                    func(pair.first, *value, pair.second);
        }
    }

    void RemoveAll(Entity entity);
    void Clear();

  private:
    template <typename T> ComponentStorage<T> *GetOrCreateStorage()
    {
        const std::type_index key = typeid(T);
        const auto found = m_storages.find(key);
        if (found != m_storages.end())
        {
            return static_cast<ComponentStorage<T> *>(found->second.get());
        }

        auto storage = std::make_unique<ComponentStorage<T>>();
        ComponentStorage<T> *rawStorage = storage.get();
        m_storages.emplace(key, std::move(storage));
        return rawStorage;
    }

    template <typename T> ComponentStorage<T> *FindStorage()
    {
        const auto found = m_storages.find(typeid(T));
        if (found == m_storages.end())
        {
            return nullptr;
        }

        return static_cast<ComponentStorage<T> *>(found->second.get());
    }

    template <typename T> const ComponentStorage<T> *FindStorage() const
    {
        const auto found = m_storages.find(typeid(T));
        if (found == m_storages.end())
        {
            return nullptr;
        }

        return static_cast<const ComponentStorage<T> *>(found->second.get());
    }

  private:
    std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> m_storages;
};

class World;

class WorldListener
{
  public:
    virtual ~WorldListener() = default;
    virtual void OnEntityCreated(World &, Entity)
    {
    }
    virtual void OnEntityDestroying(World &, Entity)
    {
    }
    virtual void OnWorldCleared(World &)
    {
    }
};

class System
{
  public:
    virtual ~System() = default;
    virtual void Initialize(World &world)
    {
        (void)world;
    }
    virtual void Update(World &world, float deltaTime) = 0;
    virtual void Shutdown(World &world)
    {
        (void)world;
    }
};

class SystemManager
{
  public:
    template <typename TSystem, typename... Args> TSystem *RegisterSystem(Args &&...args)
    {
        static_assert(std::is_base_of<System, TSystem>::value, "TSystem must derive from System");

        auto system = std::make_unique<TSystem>(std::forward<Args>(args)...);
        TSystem *raw = system.get();
        m_systems.push_back(std::move(system));
        return raw;
    }

    void InitializeAll(World &world);
    void UpdateAll(World &world, float deltaTime);
    void ShutdownAll(World &world);

  private:
    std::vector<std::unique_ptr<System>> m_systems;
};

class World
{
  public:
    Entity CreateEntity(const std::string &name = "Entity");
    void DestroyEntity(Entity entity);
    bool IsAlive(Entity entity) const;
    void Clear();
    bool SetParent(Entity child, Entity parent);
    Mat4 GetWorldMatrix(Entity entity) const;
    void AddListener(WorldListener *listener);
    void RemoveListener(WorldListener *listener);

    EntityManager &GetEntityManager();
    ComponentManager &GetComponentManager();
    SystemManager &GetSystemManager();

    const EntityManager &GetEntityManager() const;
    const ComponentManager &GetComponentManager() const;
    const SystemManager &GetSystemManager() const;

    template <typename T> void AddComponent(Entity entity, const T &component)
    {
        if (m_entityManager.IsAlive(entity))
            m_componentManager.Add<T>(entity, component);
    }

    template <typename T> void RemoveComponent(Entity entity)
    {
        m_componentManager.Remove<T>(entity);
    }

    template <typename T> T *GetComponent(Entity entity)
    {
        return m_componentManager.Get<T>(entity);
    }

    template <typename T> const T *GetComponent(Entity entity) const
    {
        return m_componentManager.Get<T>(entity);
    }

    template <typename T> bool HasComponent(Entity entity) const
    {
        return m_componentManager.Has<T>(entity);
    }

    void Initialize();
    void Update(float deltaTime);
    void Shutdown();

  private:
    EntityManager m_entityManager;
    ComponentManager m_componentManager;
    SystemManager m_systemManager;
    bool m_initialized = false;
    std::vector<WorldListener *> m_listeners;
};
} // namespace MiniEngine
