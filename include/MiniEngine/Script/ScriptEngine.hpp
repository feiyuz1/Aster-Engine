#pragma once

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
    class Logger;
    class World;

    class ScriptInstance
    {
    public:
        virtual ~ScriptInstance() = default;
        virtual void OnCreate() {}
        virtual void OnUpdate(float) {}
        virtual void OnDestroy() {}
        Entity GetEntity() const { return m_entity; }
        World* GetWorld() const { return m_world; }
    private:
        friend class ScriptEngine;
        Entity m_entity = InvalidEntity;
        World* m_world = nullptr;
    };

    class ScriptBinding
    {
    public:
        void RegisterEngineAPI();
        bool IsRegistered() const { return m_registered; }
    private:
        bool m_registered = false;
    };

    class ScriptEngine
    {
    public:
        using Factory = std::function<std::unique_ptr<ScriptInstance>()>;
        void Initialize(Logger* logger = nullptr);
        void Shutdown();
        ScriptInstance* CreateInstance(const std::string& className, Entity entity = InvalidEntity, World* world = nullptr);
        void DestroyInstance(ScriptInstance* instance);
        void DestroyEntity(Entity entity);
        void UpdateAll(float deltaTime);
        bool ReloadScript(const std::string& className);
        void RegisterFactory(const std::string& className, Factory factory);
        std::size_t GetInstanceCount() const;
    private:
        struct Record { std::string className; Entity entity = InvalidEntity; World* world = nullptr; std::unique_ptr<ScriptInstance> instance; };
        std::unique_ptr<ScriptInstance> Instantiate(const std::string& className, Entity entity, World* world) const;
        Logger* m_logger = nullptr;
        ScriptBinding m_binding;
        std::unordered_map<std::string, Factory> m_factories;
        std::vector<Record> m_instances;
    };

    class HotReloadManager
    {
    public:
        void Watch(std::string className, std::string path);
        void CheckForChanges();
        void ReloadChangedScripts(ScriptEngine& engine);
    private:
        struct Watched { std::string className; std::string path; FileWatcher watcher; bool changed = false; };
        std::vector<Watched> m_watched;
    };
}
