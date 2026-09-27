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
    /// @brief 析构 `IModule` 对象并释放其持有资源。
    virtual ~IModule() = default;
    /// @brief 获取 `Name`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    virtual const char *GetName() const = 0;
    /// @brief 获取 `Dependencies`。
    /// @return 返回查询或处理得到的结果集合。
    virtual std::vector<std::string> GetDependencies() const
    {
        return {};
    }
    /// @brief 启动 `up`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    virtual bool Startup() = 0;
    /// @brief 停止服务并释放对象持有的资源。
    virtual void Shutdown() = 0;
};

class ModuleManager
{
  public:
    /// @brief 注册对象或处理器。
    /// @param module 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Register(std::unique_ptr<IModule> module);
    /// @brief 取消注册指定对象。
    /// @param name 对象名称或注册键。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Unregister(const std::string &name);
    /// @brief 启动 `upAll`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool StartupAll();
    /// @brief 执行 `ShutdownAll` 所表示的操作。
    void ShutdownAll();
    /// @brief 查找匹配的对象。
    /// @param name 对象名称或注册键。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    IModule *Find(const std::string &name) const;
    /// @brief 获取 `RegisteredNames`。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<std::string> GetRegisteredNames() const;

  private:
    std::unordered_map<std::string, std::unique_ptr<IModule>> m_modules;
    std::vector<std::string> m_registrationOrder;
    std::vector<IModule *> m_started;
};
} // 结束 MiniEngine 命名空间
