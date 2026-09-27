// 文件：include/MiniEngine/ECS/ECS.hpp
// 作用：定义带代际句柄的实体、类型化组件存储、系统调度和 World 生命周期。
// 约定：实体句柄包含代际信息，禁止长期保存未校验的裸实体编号。
// 类型：公开声明。

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
// 分配实体索引并维护代际号。Destroy 后旧句柄即使索引被复用也不会重新变为有效。
class EntityManager
{
  public:
    /// @brief 创建一个新对象或句柄。
    /// @return 返回实体句柄；失败时返回 `InvalidEntity`。
    Entity Create();
    /// @brief 销毁指定对象。
    /// @param entity 目标实体句柄。
    void Destroy(Entity entity);
    /// @brief 判断是否满足 `Alive` 条件。
    /// @param entity 目标实体句柄。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsAlive(Entity entity) const;
    /// @brief 获取 `AliveEntities`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::unordered_set<Entity> &GetAliveEntities() const;
    /// @brief 将对象恢复为初始状态。
    void Reset();

  private:
    std::vector<u32> m_generations{0};
    std::vector<u32> m_freeIndices;
    std::unordered_set<Entity> m_aliveEntities;
};

class IComponentStorage
{
  public:
    /// @brief 析构 `IComponentStorage` 对象并释放其持有资源。
    virtual ~IComponentStorage() = default;
    /// @brief 移除指定元素。
    /// @param entity 目标实体句柄。
    virtual void Remove(Entity entity) = 0;
    /// @brief 判断目标元素是否存在。
    /// @param entity 目标实体句柄。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    virtual bool Has(Entity entity) const = 0;
    /// @brief 清空对象保存的状态和数据。
    virtual void Clear() = 0;
};

// 每种组件拥有一个稀疏哈希存储；组件地址可能在插入时失效，不应跨结构修改长期保存指针。
template <typename T> class ComponentStorage final : public IComponentStorage
{
  public:
    /// @brief 添加一个元素。
    /// @param entity 目标实体句柄。
    /// @param component 要添加或更新的组件值。
    void Add(Entity entity, const T &component)
    {
        m_data[entity] = component;
    }

    /// @brief 移除指定元素。
    /// @param entity 目标实体句柄。
    void Remove(Entity entity) override
    {
        m_data.erase(entity);
    }

    /// @brief 判断目标元素是否存在。
    /// @param entity 目标实体句柄。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool Has(Entity entity) const override
    {
        return m_data.find(entity) != m_data.end();
    }

    /// @brief 清空对象保存的状态和数据。
    void Clear() override
    {
        m_data.clear();
    }

    /// @brief 获取目标对象或值。
    /// @param entity 目标实体句柄。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    T *Get(Entity entity)
    {
        const auto found = m_data.find(entity);
        return found != m_data.end() ? &found->second : nullptr;
    }

    /// @brief 获取目标对象或值。
    /// @param entity 目标实体句柄。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const T *Get(Entity entity) const
    {
        const auto found = m_data.find(entity);
        return found != m_data.end() ? &found->second : nullptr;
    }

    /// @brief 访问对象保存的底层数据。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    std::unordered_map<Entity, T> &Data()
    {
        return m_data;
    }

    /// @brief 访问对象保存的底层数据。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::unordered_map<Entity, T> &Data() const
    {
        return m_data;
    }

  private:
    std::unordered_map<Entity, T> m_data;
};

// 使用 type_index 擦除组件类型，使 World 能以统一方式删除实体的全部组件。
class ComponentManager
{
  public:
    /// @brief 添加一个元素。
    /// @param entity 目标实体句柄。
    /// @param component 要添加或更新的组件值。
    template <typename T> void Add(Entity entity, const T &component)
    {
        GetOrCreateStorage<T>()->Add(entity, component);
    }

    /// @brief 移除指定元素。
    /// @param entity 目标实体句柄。
    template <typename T> void Remove(Entity entity)
    {
        auto *storage = FindStorage<T>();
        if (storage != nullptr)
        {
            storage->Remove(entity);
        }
    }

    /// @brief 获取目标对象或值。
    /// @param entity 目标实体句柄。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    template <typename T> T *Get(Entity entity)
    {
        auto *storage = FindStorage<T>();
        return storage != nullptr ? storage->Get(entity) : nullptr;
    }

    /// @brief 获取目标对象或值。
    /// @param entity 目标实体句柄。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    template <typename T> const T *Get(Entity entity) const
    {
        const auto *storage = FindStorage<T>();
        return storage != nullptr ? storage->Get(entity) : nullptr;
    }

    /// @brief 判断目标元素是否存在。
    /// @param entity 目标实体句柄。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    template <typename T> bool Has(Entity entity) const
    {
        const auto *storage = FindStorage<T>();
        return storage != nullptr && storage->Has(entity);
    }

    /// @brief 遍历匹配组件组合的全部实体。
    /// @param func 工作线程或遍历过程调用的函数。
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

    /// @brief 遍历匹配组件组合的全部实体。
    /// @param func 工作线程或遍历过程调用的函数。
    template <typename TFirst, typename TSecond, typename Func> void ForEach(Func &&func)
    {
        auto *first = FindStorage<TFirst>();
        auto *second = FindStorage<TSecond>();
        if (first == nullptr || second == nullptr)
            return;
        // 从较小的组件集合开始遍历，避免为简单双组件查询建立临时结果集。
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

    /// @brief 移除 `All`。
    /// @param entity 目标实体句柄。
    void RemoveAll(Entity entity);
    /// @brief 清空对象保存的状态和数据。
    void Clear();

  private:
    /// @brief 获取 `OrCreateStorage`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
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

    /// @brief 查找 `Storage`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    template <typename T> ComponentStorage<T> *FindStorage()
    {
        const auto found = m_storages.find(typeid(T));
        if (found == m_storages.end())
        {
            return nullptr;
        }

        return static_cast<ComponentStorage<T> *>(found->second.get());
    }

    /// @brief 查找 `Storage`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
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

// 生命周期监听器在组件被清理前收到 Destroying，适合释放系统侧缓存和外部句柄。
class WorldListener
{
  public:
    /// @brief 析构 `WorldListener` 对象并释放其持有资源。
    virtual ~WorldListener() = default;
    /// @brief 响应 `EntityCreated`。
    virtual void OnEntityCreated(World &, Entity)
    {
    }
    /// @brief 响应 `EntityDestroying`。
    virtual void OnEntityDestroying(World &, Entity)
    {
    }
    /// @brief 响应 `WorldCleared`。
    virtual void OnWorldCleared(World &)
    {
    }
};

class System
{
  public:
    /// @brief 析构 `System` 对象并释放其持有资源。
    virtual ~System() = default;
    /// @brief 初始化对象及其依赖资源。
    /// @param world 要读取或修改的 World。
    virtual void Initialize(World &world)
    {
        (void)world;
    }
    /// @brief 更新对象当前状态。
    /// @param world 要读取或修改的 World。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    virtual void Update(World &world, float deltaTime) = 0;
    /// @brief 停止服务并释放对象持有的资源。
    /// @param world 要读取或修改的 World。
    virtual void Shutdown(World &world)
    {
        (void)world;
    }
};

// 系统严格按注册顺序初始化和更新，并按反序关闭；注册顺序即隐式的数据依赖顺序。
class SystemManager
{
  public:
    /// @brief 注册 `System`。
    /// @param args 由接口读取并可能修改的输入/输出参数。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    template <typename TSystem, typename... Args> TSystem *RegisterSystem(Args &&...args)
    {
        static_assert(std::is_base_of<System, TSystem>::value, "TSystem must derive from System");

        auto system = std::make_unique<TSystem>(std::forward<Args>(args)...);
        TSystem *raw = system.get();
        m_systems.push_back(std::move(system));
        return raw;
    }

    /// @brief 执行 `InitializeAll` 所表示的操作。
    /// @param world 要读取或修改的 World。
    void InitializeAll(World &world);
    /// @brief 更新 `All`。
    /// @param world 要读取或修改的 World。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void UpdateAll(World &world, float deltaTime);
    /// @brief 执行 `ShutdownAll` 所表示的操作。
    /// @param world 要读取或修改的 World。
    void ShutdownAll(World &world);

  private:
    std::vector<std::unique_ptr<System>> m_systems;
};

// World 是实体、组件和系统的聚合根，同时维护父子层级与生命周期通知。
class World
{
  public:
    /// @brief 创建 `Entity`。
    /// @param name 对象名称或注册键。
    /// @return 返回实体句柄；失败时返回 `InvalidEntity`。
    Entity CreateEntity(const std::string &name = "Entity");
    /// @brief 销毁 `Entity`。
    /// @param entity 目标实体句柄。
    void DestroyEntity(Entity entity);
    /// @brief 判断是否满足 `Alive` 条件。
    /// @param entity 目标实体句柄。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsAlive(Entity entity) const;
    /// @brief 清空对象保存的状态和数据。
    void Clear();
    /// @brief 设置 `Parent`。
    /// @param child 子实体或子控件标识。
    /// @param parent 相关实体句柄。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SetParent(Entity child, Entity parent);
    /// @brief 获取 `WorldMatrix`。
    /// @param entity 目标实体句柄。
    /// @return 返回接口计算或查询得到的结果。
    Mat4 GetWorldMatrix(Entity entity) const;
    /// @brief 添加 `Listener`。
    /// @param listener 监听器对象或音频听者位置。
    void AddListener(WorldListener *listener);
    /// @brief 移除 `Listener`。
    /// @param listener 监听器对象或音频听者位置。
    void RemoveListener(WorldListener *listener);

    /// @brief 获取 `EntityManager`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    EntityManager &GetEntityManager();
    /// @brief 获取 `ComponentManager`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    ComponentManager &GetComponentManager();
    /// @brief 获取 `SystemManager`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    SystemManager &GetSystemManager();

    /// @brief 获取 `EntityManager`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const EntityManager &GetEntityManager() const;
    /// @brief 获取 `ComponentManager`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const ComponentManager &GetComponentManager() const;
    /// @brief 获取 `SystemManager`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const SystemManager &GetSystemManager() const;

    /// @brief 添加 `Component`。
    /// @param entity 目标实体句柄。
    /// @param component 要添加或更新的组件值。
    template <typename T> void AddComponent(Entity entity, const T &component)
    {
        if (m_entityManager.IsAlive(entity))
            m_componentManager.Add<T>(entity, component);
    }

    /// @brief 移除 `Component`。
    /// @param entity 目标实体句柄。
    template <typename T> void RemoveComponent(Entity entity)
    {
        m_componentManager.Remove<T>(entity);
    }

    /// @brief 获取 `Component`。
    /// @param entity 目标实体句柄。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    template <typename T> T *GetComponent(Entity entity)
    {
        return m_componentManager.Get<T>(entity);
    }

    /// @brief 获取 `Component`。
    /// @param entity 目标实体句柄。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    template <typename T> const T *GetComponent(Entity entity) const
    {
        return m_componentManager.Get<T>(entity);
    }

    /// @brief 判断是否包含 `Component` 条件。
    /// @param entity 目标实体句柄。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    template <typename T> bool HasComponent(Entity entity) const
    {
        return m_componentManager.Has<T>(entity);
    }

    /// @brief 初始化对象及其依赖资源。
    void Initialize();
    /// @brief 更新对象当前状态。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void Update(float deltaTime);
    /// @brief 停止服务并释放对象持有的资源。
    void Shutdown();

  private:
    EntityManager m_entityManager;
    ComponentManager m_componentManager;
    SystemManager m_systemManager;
    bool m_initialized = false;
    std::vector<WorldListener *> m_listeners;
};
} // 结束 MiniEngine 命名空间
