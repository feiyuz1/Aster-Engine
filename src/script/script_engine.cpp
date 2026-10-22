// 文件：src/script/script_engine.cpp
// 作用：提供脚本组件生命周期、宿主 API 和可替换脚本后端接口。
// 约定：脚本异常被隔离到当前实例，不能越过帧边界破坏主循环。
// 类型：实现文件。

#include "tinyengine/script/script_engine.h"

#include "tinyengine/core/logger.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"

#include <algorithm>
#include <utility>

namespace tinyengine
{
namespace
{
class OrbitScript final : public ScriptInstance
{
  public:
    void onUpdate(const float deltaTime) override
    {
        if (getWorld() && getWorld()->isAlive(getEntity()))
            if (auto* transform = getWorld()->GetComponent<TransformComponent>(getEntity()))
                transform->rotation.y += 30.0f * deltaTime;
    }
};
} // namespace

void ScriptBinding::registerEngineApi()
{
    m_registered = true;
}

void ScriptEngine::initialize(Logger* logger)
{
    m_logger = logger;
    m_binding.registerEngineApi();
    registerFactory("OrbitScript", []() { return std::make_unique<OrbitScript>(); });
}

void ScriptEngine::shutdown()
{
    clearInstances();
    m_factories.clear();
    m_logger = nullptr;
}

std::unique_ptr<ScriptInstance> ScriptEngine::Instantiate(const std::string& className, const Entity entity,
                                                          World* world) const
{
    const auto found = m_factories.find(className);
    if (found == m_factories.end())
        return nullptr;
    // 工厂回调可能注册其他工厂，导致映射表重新散列。
    const auto factory = found->second;
    auto instance = factory();
    if (instance)
    {
        instance->m_entity = entity;
        instance->m_world = world;
    }
    return instance;
}

void ScriptEngine::invokeCallback(const std::function<void()>& callback)
{
    ++m_callbackDepth;
    try
    {
        callback();
    }
    catch (...)
    {
        --m_callbackDepth;
        throw;
    }
    --m_callbackDepth;
    drainDeferred();
}

void ScriptEngine::drainDeferred()
{
    if (m_callbackDepth != 0 || m_draining)
        return;
    m_draining = true;
    try
    {
        for (;;)
        {
            const auto destroying = std::find_if(m_instances.begin(), m_instances.end(),
                                                 [](const auto& record) { return record->pendingDestroy; });
            if (destroying != m_instances.end())
            {
                const auto record = *destroying;
                // 调用 OnDestroy 前先解除绑定，避免递归移除时重复通知。
                m_instances.erase(destroying);
                if (!record->destroyNotified)
                {
                    record->destroyNotified = true;
                    invokeCallback([&] { record->instance->onDestroy(); });
                }
                record->instance.reset();
                continue;
            }
            const auto reloading = std::find_if(m_instances.begin(), m_instances.end(),
                                                [](const auto& record) { return record->pendingReload; });
            if (reloading == m_instances.end())
                break;
            const auto record = *reloading;
            record->pendingReload = false;
            // 合并重载回调提出的请求，包括跨类循环触发的请求。
            record->reloading = true;
            auto replacement = Instantiate(record->className, record->entity, record->world);
            if (!replacement)
                continue;
            record->destroyNotified = true;
            invokeCallback([&] { record->instance->onDestroy(); });
            if (record->world && record->entity != kInvalidEntity && !record->world->isAlive(record->entity))
                record->pendingDestroy = true;
            if (record->pendingDestroy)
                continue;
            record->instance = std::move(replacement);
            record->destroyNotified = false;
            invokeCallback([&] { record->instance->onCreate(); });
        }
    }
    catch (...)
    {
        for (const auto& record : m_instances)
            record->reloading = false;
        m_draining = false;
        throw;
    }
    for (const auto& record : m_instances)
        record->reloading = false;
    m_draining = false;
    m_clearing = false;
}

ScriptInstance* ScriptEngine::createInstance(const std::string& className, const Entity entity, World* world)
{
    if (m_clearing || (world && entity != kInvalidEntity && !world->isAlive(entity)))
        return nullptr;
    auto instance = Instantiate(className, entity, world);
    if (!instance)
    {
        if (m_logger)
            m_logger->error("Unknown script class: " + className);
        return nullptr;
    }
    auto record = std::make_shared<Record>();
    record->className = className;
    record->entity = entity;
    record->world = world;
    record->instance = std::move(instance);
    m_instances.push_back(record);
    invokeCallback([&] { record->instance->onCreate(); });
    if (world && entity != kInvalidEntity && !world->isAlive(entity))
    {
        record->pendingDestroy = true;
        drainDeferred();
    }
    return record->pendingDestroy ? nullptr : record->instance.get();
}

void ScriptEngine::destroyInstance(ScriptInstance* instance)
{
    for (const auto& record : m_instances)
        if (record->instance.get() == instance)
            record->pendingDestroy = true;
    drainDeferred();
}

void ScriptEngine::destroyEntity(const Entity entity)
{
    for (const auto& record : m_instances)
        if (record->entity == entity)
            record->pendingDestroy = true;
    drainDeferred();
}

void ScriptEngine::clearInstances()
{
    m_clearing = true;
    for (const auto& record : m_instances)
        record->pendingDestroy = true;
    drainDeferred();
}

void ScriptEngine::rebuildWorldInstances(World& world)
{
    clearInstances();
    // OnCreate 可能删除组件或实体，因此不能直接遍历仍会变化的组件集合。
    std::vector<std::pair<Entity, std::string>> scripts;
    world.GetComponentManager().ForEach<ScriptComponent>(
        [&](const Entity entity, ScriptComponent& component)
        {
            if (!component.scriptClassName.empty())
                scripts.emplace_back(entity, component.scriptClassName);
        });
    for (const auto& script : scripts)
        if (world.isAlive(script.first))
            createInstance(script.second, script.first, &world);
}

void ScriptEngine::updateAll(const float deltaTime)
{
    const auto snapshot = m_instances;
    for (const auto& record : snapshot)
    {
        if (record->pendingDestroy)
            continue;
        if (record->world && record->entity != kInvalidEntity && !record->world->isAlive(record->entity))
        {
            record->pendingDestroy = true;
            drainDeferred();
        }
        else
            invokeCallback([&] { record->instance->onUpdate(deltaTime); });
    }
}

bool ScriptEngine::reloadScript(const std::string& className)
{
    bool reloaded = false;
    for (const auto& record : m_instances)
    {
        if (record->className == className && !record->pendingDestroy && !record->reloading &&
            m_factories.count(className))
        {
            record->pendingReload = true;
            reloaded = true;
        }
    }
    drainDeferred();
    if (m_logger)
        m_logger->info(std::string(reloaded ? "Reloaded script: " : "No script instances to reload: ") + className);
    return reloaded;
}
void ScriptEngine::registerFactory(const std::string& className, Factory factory)
{
    m_factories[className] = std::move(factory);
}
std::size_t ScriptEngine::getInstanceCount() const
{
    return static_cast<std::size_t>(std::count_if(m_instances.begin(), m_instances.end(),
                                                  [](const auto& record) { return !record->pendingDestroy; }));
}

void HotReloadManager::watch(std::string className, std::string path)
{
    m_watched.push_back({std::move(className), std::move(path), {}, false});
}
void HotReloadManager::checkForChanges()
{
    for (auto& item : m_watched)
        item.changed = item.watcher.hasChanged(item.path);
}
void HotReloadManager::reloadChangedScripts(ScriptEngine& engine)
{
    for (auto& item : m_watched)
        if (item.changed)
        {
            engine.reloadScript(item.className);
            item.changed = false;
        }
}
} // namespace tinyengine
