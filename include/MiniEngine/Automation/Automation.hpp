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
