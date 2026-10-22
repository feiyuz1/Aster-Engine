// 文件：include/tinyengine/script/script_engine.h
// 作用：提供脚本组件生命周期、宿主 API 和可替换脚本后端接口。
// 约定：脚本异常被隔离到当前实例，不能越过帧边界破坏主循环。
// 类型：公开声明。

#pragma once

#include "tinyengine/platform/platform.h"
#include "tinyengine/types.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tinyengine
{
class Logger;
class World;

class ScriptInstance
{
  public:
    /// @brief 析构 `ScriptInstance` 对象并释放其持有资源。
    virtual ~ScriptInstance() = default;
    /// @brief 响应 `Create`。
    virtual void onCreate() {}
    /// @brief 响应 `Update`。
    virtual void onUpdate(float) {}
    /// @brief 响应 `Destroy`。
    virtual void onDestroy() {}
    /// @brief 获取 `Entity`。
    /// @return 返回实体句柄；失败时返回 `InvalidEntity`。
    Entity getEntity() const
    {
        return m_entity;
    }
    /// @brief 获取 `World`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    World* getWorld() const
    {
        return m_world;
    }

  private:
    friend class ScriptEngine;
    Entity m_entity = kInvalidEntity;
    World* m_world = nullptr;
};

class ScriptBinding
{
  public:
    /// @brief 注册 `EngineAPI`。
    void registerEngineApi();
    /// @brief 判断是否满足 `Registered` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isRegistered() const
    {
        return m_registered;
    }

  private:
    bool m_registered = false;
};

class ScriptEngine
{
  public:
    using Factory = std::function<std::unique_ptr<ScriptInstance>()>;
    /// @brief 初始化对象及其依赖资源。
    /// @param logger 用于输出诊断信息的日志服务。
    void initialize(Logger* logger = nullptr);
    /// @brief 停止服务并释放对象持有的资源。
    void shutdown();
    /// @brief 创建 `Instance`。
    /// @param className 脚本类或类型名称。
    /// @param entity 目标实体句柄。
    /// @param world 要读取或修改的 World。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    ScriptInstance* createInstance(const std::string& className, Entity entity = kInvalidEntity,
                                   World* world = nullptr);
    /// @brief 销毁 `Instance`。
    /// @param instance 接口所需的输入参数。
    void destroyInstance(ScriptInstance* instance);
    /// @brief 销毁 `Entity`。
    /// @param entity 目标实体句柄。
    void destroyEntity(Entity entity);
    /// @brief 清空 `Instances`。
    void clearInstances();
    /// @brief 重新构建 `WorldInstances`。
    /// @param world 要读取或修改的 World。
    void rebuildWorldInstances(World& world);
    /// @brief 更新 `All`。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void updateAll(float deltaTime);
    /// @brief 重新加载 `Script`。
    /// @param className 脚本类或类型名称。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool reloadScript(const std::string& className);
    /// @brief 注册 `Factory`。
    /// @param className 脚本类或类型名称。
    /// @param factory 满足条件时调用的回调函数。
    void registerFactory(const std::string& className, Factory factory);
    /// @brief 获取 `InstanceCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getInstanceCount() const;

  private:
    struct Record
    {
        std::string className;
        Entity entity = kInvalidEntity;
        World* world = nullptr;
        std::unique_ptr<ScriptInstance> instance;
        bool pendingDestroy = false;
        bool pendingReload = false;
        bool reloading = false;
        bool destroyNotified = false;
    };
    /// @brief 实例化资产并重建内部实体引用。
    /// @param className 脚本类或类型名称。
    /// @param entity 目标实体句柄。
    /// @param world 要读取或修改的 World。
    /// @return 返回接口计算或查询得到的结果。
    std::unique_ptr<ScriptInstance> Instantiate(const std::string& className, Entity entity, World* world) const;
    void invokeCallback(const std::function<void()>& callback);
    void drainDeferred();
    Logger* m_logger = nullptr;
    ScriptBinding m_binding;
    std::unordered_map<std::string, Factory> m_factories;
    // 回调增删脚本期间使用快照，避免遍历中的记录失效。
    std::vector<std::shared_ptr<Record>> m_instances;
    std::size_t m_callbackDepth = 0;
    bool m_draining = false;
    bool m_clearing = false;
};

class HotReloadManager
{
  public:
    /// @brief 监听脚本源文件并在变更后触发重载。
    /// @param className 脚本类或类型名称。
    /// @param path 文件、资源或目录路径。
    void watch(std::string className, std::string path);
    /// @brief 检查 `ForChanges`。
    void checkForChanges();
    /// @brief 重新加载 `ChangedScripts`。
    /// @param engine 由接口读取并可能修改的输入/输出参数。
    void reloadChangedScripts(ScriptEngine& engine);

  private:
    struct Watched
    {
        std::string className;
        std::string path;
        FileWatcher watcher;
        bool changed = false;
    };
    std::vector<Watched> m_watched;
};
} // namespace tinyengine
