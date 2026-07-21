// 文件：include/MiniEngine/Automation/Automation.hpp
// 作用：提供命令式自动化测试、编辑器 UI 驱动钩子和结果记录。
// 约定：测试钩子只观测或驱动公开行为，不应成为产品逻辑依赖。
// 类型：公开声明。

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
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
    using Commandlet = std::function<CommandletResult(const std::vector<std::string> &)>;
    bool Register(std::string name, std::string description, Commandlet commandlet);
    CommandletResult Run(const std::string &name, const std::vector<std::string> &arguments) const;
    std::string GetHelp() const;

  private:
    struct Record
    {
        std::string description;
        Commandlet callback;
    };
    std::unordered_map<std::string, Record> m_commandlets;
};

void RegisterBuiltInCommandlets(CommandletRegistry &registry);
} // namespace MiniEngine
