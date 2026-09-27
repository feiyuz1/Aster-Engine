// 文件：include/MiniEngine/Diagnostics/Diagnostics.hpp
// 作用：收集帧性能、内存和系统状态，向调试界面提供快照。
// 约定：诊断采样不得改变被观测系统的生命周期或成为显著性能负担。
// 类型：公开声明。

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

    /// @brief 注册 `Variable`。
    /// @param name 对象名称或注册键。
    /// @param defaultValue 本次操作使用的数值。
    /// @param description 资源、通道或对象描述。
    /// @param readOnly 接口所需的输入参数。
    /// @param changed 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool RegisterVariable(std::string name, ConsoleValue defaultValue, std::string description = {}, bool readOnly = false,
                          Changed changed = {});
    /// @brief 注册 `Command`。
    /// @param name 对象名称或注册键。
    /// @param command 接口所需的输入参数。
    /// @param description 资源、通道或对象描述。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool RegisterCommand(std::string name, Command command, std::string description = {});
    /// @brief 设置 `Variable`。
    /// @param name 对象名称或注册键。
    /// @param value 要写入、比较或计算的值。
    /// @param error 可选的错误信息输出；可为空。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SetVariable(const std::string &name, const std::string &value, std::string *error = nullptr);
    /// @brief 查找 `Variable`。
    /// @param name 对象名称或注册键。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const ConsoleValue *FindVariable(const std::string &name) const;
    /// @brief 获取 `Variables`。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<ConsoleVariableInfo> GetVariables() const;
    /// @brief 获取 `Commands`。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<std::string> GetCommands() const;
    /// @brief 执行已准备好的操作。
    /// @param line 接口所需的输入参数。
    /// @return 返回对应的文本或路径。
    std::string Execute(const std::string &line);
    /// @brief 将枚举或状态转换为可读文本。
    /// @param value 要写入、比较或计算的值。
    /// @return 返回对应的文本或路径。
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
    /// @brief 设置目标对象或值。
    /// @param name 对象名称或注册键。
    /// @param value 要写入、比较或计算的值。
    void Set(const std::string &name, double value);
    /// @brief 添加一个元素。
    /// @param name 对象名称或注册键。
    /// @param amount 本次操作使用的数值。
    void Add(const std::string &name, double amount = 1.0);
    /// @brief 获取目标对象或值。
    /// @param name 对象名称或注册键。
    /// @return 返回接口计算或查询得到的结果。
    StatSample Get(const std::string &name) const;
    /// @brief 获取当前状态的只读快照。
    /// @return 返回查询或处理得到的结果集合。
    std::unordered_map<std::string, StatSample> Snapshot() const;
    /// @brief 重置 `Peaks`。
    void ResetPeaks();
    /// @brief 清空对象保存的状态和数据。
    void Clear();

  private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, StatSample> m_stats;
};

class DiagnosticsService
{
  public:
    /// @brief 初始化对象及其依赖资源。
    void Initialize();
    /// @brief 停止服务并释放对象持有的资源。
    void Shutdown();
    /// @brief 获取 `Console`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    ConsoleManager &GetConsole();
    /// @brief 获取 `Stats`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    StatRegistry &GetStats();
    /// @brief 判断是否满足 `Initialized` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsInitialized() const;

  private:
    ConsoleManager m_console;
    StatRegistry m_stats;
    bool m_initialized = false;
};
} // 结束 MiniEngine 命名空间
