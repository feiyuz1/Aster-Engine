#include "MiniEngine/Core/Module.hpp"

namespace MiniEngine
{
bool ModuleManager::Register(std::unique_ptr<IModule> module)
{
    if (!module || !module->GetName() || m_modules.count(module->GetName()))
        return false;
    m_modules[module->GetName()] = std::move(module);
    return true;
}
bool ModuleManager::StartupAll()
{
    for (auto &pair : m_modules)
    {
        if (!pair.second->Startup())
        {
            ShutdownAll();
            return false;
        }
        m_started.push_back(pair.second.get());
    }
    return true;
}
void ModuleManager::ShutdownAll()
{
    for (auto it = m_started.rbegin(); it != m_started.rend(); ++it)
        (*it)->Shutdown();
    m_started.clear();
}
IModule *ModuleManager::Find(const std::string &name) const
{
    const auto found = m_modules.find(name);
    return found == m_modules.end() ? nullptr : found->second.get();
}
} // namespace MiniEngine
