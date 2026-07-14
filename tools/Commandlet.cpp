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
