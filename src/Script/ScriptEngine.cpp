#include "MiniEngine/Script/ScriptEngine.hpp"

#include "MiniEngine/Core/Logger.hpp"

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
                m_time += deltaTime;
            }

            float GetTime() const
            {
                return m_time;
            }

        private:
            float m_time = 0.0f;
        };
    }

    void ScriptBinding::RegisterEngineAPI()
    {
    }

    void ScriptEngine::Initialize(Logger* logger)
    {
        m_logger = logger;
        m_binding.RegisterEngineAPI();
        RegisterFactory("OrbitScript", []()
        {
            return std::make_unique<OrbitScript>();
        });
    }

    void ScriptEngine::Shutdown()
    {
        for (const auto& instance : m_instances)
        {
            instance->OnDestroy();
        }

        m_instances.clear();
        m_factories.clear();
        m_logger = nullptr;
    }

    ScriptInstance* ScriptEngine::CreateInstance(const std::string& className)
    {
        std::unique_ptr<ScriptInstance> instance;
        const auto found = m_factories.find(className);
        if (found != m_factories.end())
        {
            instance = found->second();
        }
        else
        {
            instance = std::make_unique<ScriptInstance>();
        }

        ScriptInstance* raw = instance.get();
        m_instances.push_back(std::move(instance));
        raw->OnCreate();
        return raw;
    }

    void ScriptEngine::DestroyInstance(ScriptInstance* instance)
    {
        const auto it = std::find_if(m_instances.begin(), m_instances.end(), [instance](const std::unique_ptr<ScriptInstance>& current)
        {
            return current.get() == instance;
        });

        if (it == m_instances.end())
        {
            return;
        }

        (*it)->OnDestroy();
        m_instances.erase(it);
    }

    void ScriptEngine::UpdateAll(const float deltaTime)
    {
        for (const auto& instance : m_instances)
        {
            instance->OnUpdate(deltaTime);
        }
    }

    bool ScriptEngine::ReloadScript(const std::string& moduleName)
    {
        if (m_logger != nullptr)
        {
            m_logger->Info("ReloadScript called for module: " + moduleName);
        }
        return true;
    }

    void ScriptEngine::RegisterFactory(const std::string& className, Factory factory)
    {
        m_factories[className] = std::move(factory);
    }

    void HotReloadManager::CheckForChanges()
    {
    }

    void HotReloadManager::ReloadChangedScripts(ScriptEngine& engine)
    {
        (void)engine;
    }
}
