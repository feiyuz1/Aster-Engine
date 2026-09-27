// 文件：src/Script/ScriptEngine.cpp
// 作用：提供脚本组件生命周期、宿主 API 和可替换脚本后端接口。
// 约定：脚本异常被隔离到当前实例，不能越过帧边界破坏主循环。
// 类型：实现文件。

#include "MiniEngine/Script/ScriptEngine.hpp"

#include "MiniEngine/Core/Logger.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"

#include <algorithm>
#include <utility>

namespace MiniEngine
{
namespace
{
class OrbitScript final : public ScriptInstance
{
  public:
    void OnUpdate(const float deltaTime) override
    {
        if (GetWorld() && GetWorld()->IsAlive(GetEntity()))
            if (auto *transform = GetWorld()->GetComponent<TransformComponent>(GetEntity()))
                transform->rotation.y += 30.0f * deltaTime;
    }
};
} // 匿名命名空间

void ScriptBinding::RegisterEngineAPI()
{
    m_registered = true;
}

void ScriptEngine::Initialize(Logger *logger)
{
    m_logger = logger;
    m_binding.RegisterEngineAPI();
    RegisterFactory("OrbitScript", []() { return std::make_unique<OrbitScript>(); });
}

void ScriptEngine::Shutdown()
{
    ClearInstances();
    m_factories.clear();
    m_logger = nullptr;
}

std::unique_ptr<ScriptInstance> ScriptEngine::Instantiate(const std::string &className, const Entity entity, World *world) const
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

void ScriptEngine::InvokeCallback(const std::function<void()> &callback)
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
    DrainDeferred();
}

void ScriptEngine::DrainDeferred()
{
    if (m_callbackDepth != 0 || m_draining)
        return;
    m_draining = true;
    try
    {
        for (;;)
        {
            const auto destroying = std::find_if(m_instances.begin(), m_instances.end(),
                [](const auto &record) { return record->pendingDestroy; });
            if (destroying != m_instances.end())
            {
                const auto record = *destroying;
                // 调用 OnDestroy 前先解除绑定，避免递归移除时重复通知。
                m_instances.erase(destroying);
                if (!record->destroyNotified)
                {
                    record->destroyNotified = true;
                    InvokeCallback([&] { record->instance->OnDestroy(); });
                }
                record->instance.reset();
                continue;
            }
            const auto reloading = std::find_if(m_instances.begin(), m_instances.end(),
                [](const auto &record) { return record->pendingReload; });
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
            InvokeCallback([&] { record->instance->OnDestroy(); });
            if (record->world && record->entity != InvalidEntity && !record->world->IsAlive(record->entity))
                record->pendingDestroy = true;
            if (record->pendingDestroy)
                continue;
            record->instance = std::move(replacement);
            record->destroyNotified = false;
            InvokeCallback([&] { record->instance->OnCreate(); });
        }
    }
    catch (...)
    {
        for (const auto &record : m_instances)
            record->reloading = false;
        m_draining = false;
        throw;
    }
    for (const auto &record : m_instances)
        record->reloading = false;
    m_draining = false;
    m_clearing = false;
}

ScriptInstance *ScriptEngine::CreateInstance(const std::string &className, const Entity entity, World *world)
{
    if (m_clearing || (world && entity != InvalidEntity && !world->IsAlive(entity)))
        return nullptr;
    auto instance = Instantiate(className, entity, world);
    if (!instance)
    {
        if (m_logger)
            m_logger->Error("Unknown script class: " + className);
        return nullptr;
    }
    auto record = std::make_shared<Record>();
    record->className = className;
    record->entity = entity;
    record->world = world;
    record->instance = std::move(instance);
    m_instances.push_back(record);
    InvokeCallback([&] { record->instance->OnCreate(); });
    if (world && entity != InvalidEntity && !world->IsAlive(entity))
    {
        record->pendingDestroy = true;
        DrainDeferred();
    }
    return record->pendingDestroy ? nullptr : record->instance.get();
}

void ScriptEngine::DestroyInstance(ScriptInstance *instance)
{
    for (const auto &record : m_instances)
        if (record->instance.get() == instance)
            record->pendingDestroy = true;
    DrainDeferred();
}

void ScriptEngine::DestroyEntity(const Entity entity)
{
    for (const auto &record : m_instances)
        if (record->entity == entity)
            record->pendingDestroy = true;
    DrainDeferred();
}

void ScriptEngine::ClearInstances()
{
    m_clearing = true;
    for (const auto &record : m_instances)
        record->pendingDestroy = true;
    DrainDeferred();
}

void ScriptEngine::RebuildWorldInstances(World &world)
{
    ClearInstances();
    // OnCreate 可能删除组件或实体，因此不能直接遍历仍会变化的组件集合。
    std::vector<std::pair<Entity, std::string>> scripts;
    world.GetComponentManager().ForEach<ScriptComponent>([&](const Entity entity, ScriptComponent &component) {
        if (!component.scriptClassName.empty())
            scripts.emplace_back(entity, component.scriptClassName);
    });
    for (const auto &script : scripts)
        if (world.IsAlive(script.first))
            CreateInstance(script.second, script.first, &world);
}

void ScriptEngine::UpdateAll(const float deltaTime)
{
    const auto snapshot = m_instances;
    for (const auto &record : snapshot)
    {
        if (record->pendingDestroy)
            continue;
        if (record->world && record->entity != InvalidEntity && !record->world->IsAlive(record->entity))
        {
            record->pendingDestroy = true;
            DrainDeferred();
        }
        else
            InvokeCallback([&] { record->instance->OnUpdate(deltaTime); });
    }
}

bool ScriptEngine::ReloadScript(const std::string &className)
{
    bool reloaded = false;
    for (const auto &record : m_instances)
    {
        if (record->className == className && !record->pendingDestroy && !record->reloading && m_factories.count(className))
        {
            record->pendingReload = true;
            reloaded = true;
        }
    }
    DrainDeferred();
    if (m_logger)
        m_logger->Info(std::string(reloaded ? "Reloaded script: " : "No script instances to reload: ") + className);
    return reloaded;
}
void ScriptEngine::RegisterFactory(const std::string &className, Factory factory)
{
    m_factories[className] = std::move(factory);
}
std::size_t ScriptEngine::GetInstanceCount() const
{
    return static_cast<std::size_t>(std::count_if(m_instances.begin(), m_instances.end(),
        [](const auto &record) { return !record->pendingDestroy; }));
}

void HotReloadManager::Watch(std::string className, std::string path)
{
    m_watched.push_back({std::move(className), std::move(path), {}, false});
}
void HotReloadManager::CheckForChanges()
{
    for (auto &item : m_watched)
        item.changed = item.watcher.HasChanged(item.path);
}
void HotReloadManager::ReloadChangedScripts(ScriptEngine &engine)
{
    for (auto &item : m_watched)
        if (item.changed)
        {
            engine.ReloadScript(item.className);
            item.changed = false;
        }
}
} // 结束 MiniEngine 命名空间
