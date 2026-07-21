// 文件：tools/Commandlet.cpp
// 作用：提供无人值守资源检查和自动化任务入口。
// 约定：命令行工具以非交互方式运行，失败时返回非零退出码并输出可定位的错误。
// 类型：实现文件。

#include "MiniEngine/Automation/Automation.hpp"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    MiniEngine::CommandletRegistry registry;
    MiniEngine::RegisterBuiltInCommandlets(registry);
    if (argc < 2 || std::string(argv[1]) == "help" || std::string(argv[1]) == "--help")
    {
        std::cout << registry.GetHelp();
        return argc < 2 ? 2 : 0;
    }
    std::vector<std::string> arguments;
    for (int index = 2; index < argc; ++index)
        arguments.emplace_back(argv[index]);
    const auto result = registry.Run(argv[1], arguments);
    if (!result.output.empty())
        std::cout << result.output << '\n';
    for (const auto &error : result.errors)
        std::cerr << error << '\n';
    return result.exitCode;
}
