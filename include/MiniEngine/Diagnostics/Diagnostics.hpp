#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace MiniEngine
{
using ConsoleValue = std::variant<bool, int, float, std::string>;

struct ConsoleVariableInfo
{
    std::string name;
    std::string description;
    ConsoleValue value;
    bool readOnly = false;
};

class ConsoleManager
{
  public:
    using Command = std::function<std::string(const std::vector<std::string> &)>;
    using Changed = std::function<void(const ConsoleValue &)>;

    bool RegisterVariable(std::string name, ConsoleValue defaultValue, std::string description = {}, bool readOnly = false,
                          Changed changed = {});
    bool RegisterCommand(std::string name, Command command, std::string description = {});
    bool SetVariable(const std::string &name, const std::string &value, std::string *error = nullptr);
    const ConsoleValue *FindVariable(const std::string &name) const;
    std::vector<ConsoleVariableInfo> GetVariables() const;
    std::vector<std::string> GetCommands() const;
    std::string Execute(const std::string &line);
    static std::string ToString(const ConsoleValue &value);

  private:
    struct Variable
    {
        ConsoleVariableInfo info;
        Changed changed;
    };
    struct CommandRecord
    {
        Command callback;
        std::string description;
    };
    std::unordered_map<std::string, Variable> m_variables;
    std::unordered_map<std::string, CommandRecord> m_commands;
};

struct StatSample
{
    double value = 0.0;
    double peak = 0.0;
};

class StatRegistry
{
  public:
    void Set(const std::string &name, double value);
    void Add(const std::string &name, double amount = 1.0);
    StatSample Get(const std::string &name) const;
    std::unordered_map<std::string, StatSample> Snapshot() const;
    void ResetPeaks();
    void Clear();

  private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, StatSample> m_stats;
};

class DiagnosticsService
{
  public:
    void Initialize();
    void Shutdown();
    ConsoleManager &GetConsole();
    StatRegistry &GetStats();
    bool IsInitialized() const;

  private:
    ConsoleManager m_console;
    StatRegistry m_stats;
    bool m_initialized = false;
};
} // namespace MiniEngine
