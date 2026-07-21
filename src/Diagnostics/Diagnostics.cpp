// 文件：src/Diagnostics/Diagnostics.cpp
// 作用：收集帧性能、内存和系统状态，向调试界面提供快照。
// 约定：诊断采样不得改变被观测系统的生命周期或成为显著性能负担。
// 类型：实现文件。

#include "MiniEngine/Diagnostics/Diagnostics.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace MiniEngine
{
namespace
{
std::vector<std::string> Tokenize(const std::string &line)
{
    std::vector<std::string> tokens;
    std::istringstream input(line);
    while (input >> std::ws && input.peek() != std::char_traits<char>::eof())
    {
        std::string token;
        if (input.peek() == '"')
            input >> std::quoted(token);
        else
            input >> token;
        if (!token.empty())
            tokens.push_back(std::move(token));
    }
    return tokens;
}

bool ParseBool(std::string value, bool &result)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (value == "1" || value == "true" || value == "on" || value == "yes")
    {
        result = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "off" || value == "no")
    {
        result = false;
        return true;
    }
    return false;
}
} // namespace

bool ConsoleManager::RegisterVariable(std::string name, ConsoleValue defaultValue, std::string description, const bool readOnly,
                                      Changed changed)
{
    if (name.empty())
        return false;
    ConsoleVariableInfo info{name, std::move(description), std::move(defaultValue), readOnly};
    return m_variables.emplace(std::move(name), Variable{std::move(info), std::move(changed)}).second;
}

bool ConsoleManager::RegisterCommand(std::string name, Command command, std::string description)
{
    if (name.empty() || !command)
        return false;
    return m_commands.emplace(std::move(name), CommandRecord{std::move(command), std::move(description)}).second;
}

bool ConsoleManager::SetVariable(const std::string &name, const std::string &value, std::string *error)
{
    const auto found = m_variables.find(name);
    if (found == m_variables.end())
    {
        if (error)
            *error = "Unknown variable: " + name;
        return false;
    }
    if (found->second.info.readOnly)
    {
        if (error)
            *error = "Variable is read-only: " + name;
        return false;
    }
    ConsoleValue parsed;
    try
    {
        if (std::holds_alternative<bool>(found->second.info.value))
        {
            bool boolean = false;
            if (!ParseBool(value, boolean))
                throw std::invalid_argument("bool");
            parsed = boolean;
        }
        else if (std::holds_alternative<int>(found->second.info.value))
            parsed = std::stoi(value);
        else if (std::holds_alternative<float>(found->second.info.value))
            parsed = std::stof(value);
        else
            parsed = value;
    }
    catch (...)
    {
        if (error)
            *error = "Invalid value for " + name + ": " + value;
        return false;
    }
    found->second.info.value = parsed;
    if (found->second.changed)
        found->second.changed(found->second.info.value);
    return true;
}

const ConsoleValue *ConsoleManager::FindVariable(const std::string &name) const
{
    const auto found = m_variables.find(name);
    return found == m_variables.end() ? nullptr : &found->second.info.value;
}

std::vector<ConsoleVariableInfo> ConsoleManager::GetVariables() const
{
    std::vector<ConsoleVariableInfo> result;
    result.reserve(m_variables.size());
    for (const auto &pair : m_variables)
        result.push_back(pair.second.info);
    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) { return left.name < right.name; });
    return result;
}

std::vector<std::string> ConsoleManager::GetCommands() const
{
    std::vector<std::string> result;
    result.reserve(m_commands.size());
    for (const auto &pair : m_commands)
        result.push_back(pair.first);
    std::sort(result.begin(), result.end());
    return result;
}

std::string ConsoleManager::Execute(const std::string &line)
{
    auto tokens = Tokenize(line);
    if (tokens.empty())
        return {};
    const std::string name = tokens.front();
    tokens.erase(tokens.begin());
    if (const auto command = m_commands.find(name); command != m_commands.end())
        return command->second.callback(tokens);
    const auto variable = m_variables.find(name);
    if (variable == m_variables.end())
        return "Unknown command or variable: " + name;
    if (tokens.empty())
        return name + " = " + ToString(variable->second.info.value);
    std::string error;
    if (!SetVariable(name, tokens.front(), &error))
        return error;
    return name + " = " + ToString(variable->second.info.value);
}

std::string ConsoleManager::ToString(const ConsoleValue &value)
{
    if (const auto *boolean = std::get_if<bool>(&value))
        return *boolean ? "true" : "false";
    if (const auto *integer = std::get_if<int>(&value))
        return std::to_string(*integer);
    if (const auto *real = std::get_if<float>(&value))
    {
        std::ostringstream output;
        output << *real;
        return output.str();
    }
    return std::get<std::string>(value);
}

void StatRegistry::Set(const std::string &name, const double value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &stat = m_stats[name];
    stat.value = value;
    stat.peak = std::max(stat.peak, value);
}

void StatRegistry::Add(const std::string &name, const double amount)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto &stat = m_stats[name];
    stat.value += amount;
    stat.peak = std::max(stat.peak, stat.value);
}

StatSample StatRegistry::Get(const std::string &name) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_stats.find(name);
    return found == m_stats.end() ? StatSample{} : found->second;
}

std::unordered_map<std::string, StatSample> StatRegistry::Snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

void StatRegistry::ResetPeaks()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &pair : m_stats)
        pair.second.peak = pair.second.value;
}

void StatRegistry::Clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stats.clear();
}

void DiagnosticsService::Initialize()
{
    if (m_initialized)
        return;
    m_console.RegisterCommand("echo", [](const std::vector<std::string> &arguments) {
        std::string result;
        for (const auto &argument : arguments)
            result += (result.empty() ? "" : " ") + argument;
        return result;
    });
    m_console.RegisterCommand("cvar.list", [this](const std::vector<std::string> &) {
        std::string result;
        for (const auto &variable : m_console.GetVariables())
            result += variable.name + "=" + ConsoleManager::ToString(variable.value) + "\n";
        return result;
    });
    m_console.RegisterCommand("stat.list", [this](const std::vector<std::string> &) {
        std::vector<std::string> names;
        const auto snapshot = m_stats.Snapshot();
        for (const auto &pair : snapshot)
            names.push_back(pair.first);
        std::sort(names.begin(), names.end());
        std::string result;
        for (const auto &name : names)
            result += name + "=" + std::to_string(snapshot.at(name).value) + "\n";
        return result;
    });
    m_initialized = true;
}

void DiagnosticsService::Shutdown()
{
    m_stats.Clear();
    m_console = {};
    m_initialized = false;
}

ConsoleManager &DiagnosticsService::GetConsole()
{
    return m_console;
}

StatRegistry &DiagnosticsService::GetStats()
{
    return m_stats;
}

bool DiagnosticsService::IsInitialized() const
{
    return m_initialized;
}
} // namespace MiniEngine
