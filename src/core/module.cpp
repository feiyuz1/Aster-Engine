// 文件：src/core/module.cpp
// 作用：定义引擎模块生命周期及按依赖顺序初始化的模块管理器。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：实现文件。

#include "tinyengine/core/module.h"

#include <algorithm>
#include <unordered_set>

namespace tinyengine
{
bool ModuleManager::Register(std::unique_ptr<IModule> module)
{
    if (!module || !module->GetName() || m_modules.count(module->GetName()))
        return false;
    const std::string name = module->GetName();
    if (name.empty())
        return false;
    m_registrationOrder.push_back(name);
    m_modules[name] = std::move(module);
    return true;
}

bool ModuleManager::unregister(const std::string& name)
{
    const auto module = m_modules.find(name);
    if (module == m_modules.end() ||
        std::find(m_started.begin(), m_started.end(), module->second.get()) != m_started.end())
        return false;
    m_modules.erase(module);
    m_registrationOrder.erase(std::remove(m_registrationOrder.begin(), m_registrationOrder.end(), name),
                              m_registrationOrder.end());
    return true;
}

bool ModuleManager::startupAll()
{
    if (!m_started.empty())
        return true;
    std::unordered_set<std::string> startedNames;
    for (const auto& name : m_registrationOrder)
        for (const auto& dependency : m_modules.at(name)->GetDependencies())
            if (!m_modules.count(dependency))
                return false;
    while (startedNames.size() < m_modules.size())
    {
        bool madeProgress = false;
        for (const auto& name : m_registrationOrder)
        {
            if (startedNames.count(name))
                continue;
            auto& module = m_modules.at(name);
            const auto dependencies = module->GetDependencies();
            if (!std::all_of(dependencies.begin(), dependencies.end(),
                             [&](const std::string& dependency) { return startedNames.count(dependency) != 0; }))
                continue;
            if (!module->Startup())
            {
                shutdownAll();
                return false;
            }
            m_started.push_back(module.get());
            startedNames.insert(name);
            madeProgress = true;
        }
        if (!madeProgress)
        {
            shutdownAll();
            return false;
        }
    }
    return true;
}

void ModuleManager::shutdownAll()
{
    for (auto it = m_started.rbegin(); it != m_started.rend(); ++it)
        (*it)->shutdown();
    m_started.clear();
}
IModule* ModuleManager::Find(const std::string& name) const
{
    const auto found = m_modules.find(name);
    return found == m_modules.end() ? nullptr : found->second.get();
}

std::vector<std::string> ModuleManager::GetRegisteredNames() const
{
    return m_registrationOrder;
}
} // namespace tinyengine
