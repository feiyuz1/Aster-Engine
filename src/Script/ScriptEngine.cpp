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
                    if (auto* transform = GetWorld()->GetComponent<TransformComponent>(GetEntity())) transform->rotation.y += 30.0f * deltaTime;
            }
        };
    }

    void ScriptBinding::RegisterEngineAPI() { m_registered = true; }

    void ScriptEngine::Initialize(Logger* logger)
    {
        m_logger = logger; m_binding.RegisterEngineAPI();
        RegisterFactory("OrbitScript", []() { return std::make_unique<OrbitScript>(); });
    }

    void ScriptEngine::Shutdown()
    {
        for (auto& record : m_instances) record.instance->OnDestroy();
        m_instances.clear(); m_factories.clear(); m_logger = nullptr;
    }

    std::unique_ptr<ScriptInstance> ScriptEngine::Instantiate(const std::string& className, const Entity entity, World* world) const
    {
        const auto found = m_factories.find(className);
        if (found == m_factories.end()) return nullptr;
        auto instance = found->second();
        instance->m_entity = entity; instance->m_world = world;
        return instance;
    }

    ScriptInstance* ScriptEngine::CreateInstance(const std::string& className, const Entity entity, World* world)
    {
        auto instance = Instantiate(className, entity, world);
        if (!instance) { if (m_logger) m_logger->Error("Unknown script class: " + className); return nullptr; }
        ScriptInstance* result = instance.get();
        m_instances.push_back({className, entity, world, std::move(instance)});
        result->OnCreate(); return result;
    }

    void ScriptEngine::DestroyInstance(ScriptInstance* instance)
    {
        const auto found = std::find_if(m_instances.begin(), m_instances.end(), [instance](const Record& record) { return record.instance.get() == instance; });
        if (found == m_instances.end()) return;
        found->instance->OnDestroy(); m_instances.erase(found);
    }

    void ScriptEngine::DestroyEntity(const Entity entity)
    {
        for (auto it = m_instances.begin(); it != m_instances.end();)
        {
            if (it->entity == entity) { it->instance->OnDestroy(); it = m_instances.erase(it); }
            else ++it;
        }
    }

    void ScriptEngine::ClearInstances()
    {
        for (auto& record : m_instances) record.instance->OnDestroy();
        m_instances.clear();
    }

    void ScriptEngine::RebuildWorldInstances(World& world)
    {
        ClearInstances();
        world.GetComponentManager().ForEach<ScriptComponent>([this, &world](const Entity entity, ScriptComponent& component)
        {
            if (!component.scriptClassName.empty()) CreateInstance(component.scriptClassName, entity, &world);
        });
    }

    void ScriptEngine::UpdateAll(const float deltaTime)
    {
        for (auto it = m_instances.begin(); it != m_instances.end();)
        {
            if (it->world && it->entity != InvalidEntity && !it->world->IsAlive(it->entity)) { it->instance->OnDestroy(); it = m_instances.erase(it); }
            else { it->instance->OnUpdate(deltaTime); ++it; }
        }
    }

    bool ScriptEngine::ReloadScript(const std::string& className)
    {
        bool reloaded = false;
        for (auto& record : m_instances)
        {
            if (record.className != className) continue;
            auto replacement = Instantiate(className, record.entity, record.world);
            if (!replacement) continue;
            record.instance->OnDestroy(); record.instance = std::move(replacement); record.instance->OnCreate(); reloaded = true;
        }
        if (m_logger) m_logger->Info(std::string(reloaded ? "Reloaded script: " : "No script instances to reload: ") + className);
        return reloaded;
    }

    void ScriptEngine::RegisterFactory(const std::string& className, Factory factory) { m_factories[className] = std::move(factory); }
    std::size_t ScriptEngine::GetInstanceCount() const { return m_instances.size(); }

    void HotReloadManager::Watch(std::string className, std::string path) { m_watched.push_back({std::move(className), std::move(path), {}, false}); }
    void HotReloadManager::CheckForChanges() { for (auto& item : m_watched) item.changed = item.watcher.HasChanged(item.path); }
    void HotReloadManager::ReloadChangedScripts(ScriptEngine& engine) { for (auto& item : m_watched) if (item.changed) { engine.ReloadScript(item.className); item.changed = false; } }
}
