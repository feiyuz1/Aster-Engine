#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
    class Logger;

    class ScriptInstance
    {
    public:
        virtual ~ScriptInstance() = default;
        virtual void OnCreate()
        {
        }
        virtual void OnUpdate(float deltaTime)
        {
            (void)deltaTime;
        }
        virtual void OnDestroy()
        {
        }
    };

    class ScriptBinding
    {
    public:
        void RegisterEngineAPI();
    };

    class ScriptEngine
    {
    public:
        using Factory = std::function<std::unique_ptr<ScriptInstance>()>;

        void Initialize(Logger* logger = nullptr);
        void Shutdown();

        ScriptInstance* CreateInstance(const std::string& className);
        void DestroyInstance(ScriptInstance* instance);
        void UpdateAll(float deltaTime);

        bool ReloadScript(const std::string& moduleName);
        void RegisterFactory(const std::string& className, Factory factory);

    private:
        Logger* m_logger = nullptr;
        ScriptBinding m_binding;
        std::unordered_map<std::string, Factory> m_factories;
        std::vector<std::unique_ptr<ScriptInstance>> m_instances;
    };

    class HotReloadManager
    {
    public:
        void CheckForChanges();
        void ReloadChangedScripts(ScriptEngine& engine);
    };
}
