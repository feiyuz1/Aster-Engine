// 文件：include/tinyengine/automation/automation.h
// 作用：提供命令式自动化测试、编辑器 UI 驱动钩子和结果记录。
// 约定：测试钩子只观测或驱动公开行为，不应成为产品逻辑依赖。
// 类型：公开声明。

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tinyengine
{
struct CommandletResult
{
    int exitCode = 0;
    std::string output;
    std::vector<std::string> errors;
};

class CommandletRegistry
{
  public:
    using Commandlet = std::function<CommandletResult(const std::vector<std::string>&)>;
    /// @brief 注册对象或处理器。
    /// @param name 对象名称或注册键。
    /// @param description 资源、通道或对象描述。
    /// @param commandlet 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Register(std::string name, std::string description, Commandlet commandlet);
    /// @brief 运行主循环或任务流程。
    /// @param name 对象名称或注册键。
    /// @param arguments 命令、脚本或格式化参数集合。
    /// @return 返回接口计算或查询得到的结果。
    CommandletResult Run(const std::string& name, const std::vector<std::string>& arguments) const;
    /// @brief 获取 `Help`。
    /// @return 返回对应的文本或路径。
    std::string getHelp() const;

  private:
    struct Record
    {
        std::string description;
        Commandlet callback;
    };
    std::unordered_map<std::string, Record> m_commandlets;
};

/// @brief 注册 `BuiltInCommandlets`。
/// @param registry 要查询或更新的资产注册表。
void registerBuiltInCommandlets(CommandletRegistry& registry);
} // namespace tinyengine
