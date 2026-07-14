#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
class IModule
{
  public:
    virtual ~IModule() = default;
    virtual const char *GetName() const = 0;
    virtual bool Startup() = 0;
    virtual void Shutdown() = 0;
};

class ModuleManager
{
  public:
    bool Register(std::unique_ptr<IModule> module);
    bool StartupAll();
    void ShutdownAll();
    IModule *Find(const std::string &name) const;

  private:
    std::unordered_map<std::string, std::unique_ptr<IModule>> m_modules;
    std::vector<IModule *> m_started;
};
} // namespace MiniEngine
