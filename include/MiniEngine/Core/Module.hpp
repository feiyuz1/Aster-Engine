// 文件：include/MiniEngine/Core/Module.hpp
// 作用：定义引擎模块生命周期及按依赖顺序初始化的模块管理器。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：公开声明。

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
    virtual std::vector<std::string> GetDependencies() const
    {
        return {};
    }
    virtual bool Startup() = 0;
    virtual void Shutdown() = 0;
};

class ModuleManager
{
  public:
    bool Register(std::unique_ptr<IModule> module);
    bool Unregister(const std::string &name);
    bool StartupAll();
    void ShutdownAll();
    IModule *Find(const std::string &name) const;
    std::vector<std::string> GetRegisteredNames() const;

  private:
    std::unordered_map<std::string, std::unique_ptr<IModule>> m_modules;
    std::vector<std::string> m_registrationOrder;
    std::vector<IModule *> m_started;
};
} // namespace MiniEngine
