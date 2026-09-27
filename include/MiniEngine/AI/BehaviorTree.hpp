// 文件：include/MiniEngine/AI/BehaviorTree.hpp
// 作用：定义 Blackboard、行为树节点、运行时执行和调试信息。
// 约定：行为节点返回运行状态，长任务必须可在后续帧继续。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Types.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace MiniEngine
{
using BlackboardValue = std::variant<bool, int, float, Vec3, std::string, Entity>;

class Blackboard
{
  public:
    /// @brief 设置目标对象或值。
    /// @param key 用于查找数据的键。
    /// @param value 要写入、比较或计算的值。
    template <typename T> void Set(std::string key, T value)
    {
        m_values[std::move(key)] = BlackboardValue{std::move(value)};
    }
    /// @brief 获取目标对象或值。
    /// @param key 用于查找数据的键。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    template <typename T> const T *Get(const std::string &key) const
    {
        const auto found = m_values.find(key);
        return found == m_values.end() ? nullptr : std::get_if<T>(&found->second);
    }
    /// @brief 移除指定元素。
    /// @param key 用于查找数据的键。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Remove(const std::string &key);
    /// @brief 清空对象保存的状态和数据。
    void Clear();

  private:
    std::unordered_map<std::string, BlackboardValue> m_values;
};

enum class BehaviorStatus
{
    Success,
    Failure,
    Running
};

class BehaviorNode
{
  public:
    /// @brief 析构 `BehaviorNode` 对象并释放其持有资源。
    virtual ~BehaviorNode() = default;
    /// @brief 推进一次按帧更新。
    /// @param blackboard 行为树读取和修改的 Blackboard。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    /// @return 返回接口计算或查询得到的结果。
    virtual BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) = 0;
    /// @brief 将对象恢复为初始状态。
    virtual void Reset()
    {
    }
};

class ActionNode final : public BehaviorNode
{
  public:
    using Action = std::function<BehaviorStatus(Blackboard &, float)>;
    /// @brief 构造 `ActionNode` 对象。
    /// @param action 满足条件时调用的回调函数。
    explicit ActionNode(Action action);
    /// @brief 推进一次按帧更新。
    /// @param blackboard 行为树读取和修改的 Blackboard。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    /// @return 返回接口计算或查询得到的结果。
    BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) override;

  private:
    Action m_action;
};

class ConditionNode final : public BehaviorNode
{
  public:
    using Condition = std::function<bool(const Blackboard &)>;
    /// @brief 构造 `ConditionNode` 对象。
    /// @param condition 决定转移或执行结果的条件回调。
    explicit ConditionNode(Condition condition);
    /// @brief 推进一次按帧更新。
    /// @param blackboard 行为树读取和修改的 Blackboard。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    /// @return 返回接口计算或查询得到的结果。
    BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) override;

  private:
    Condition m_condition;
};

class SequenceNode final : public BehaviorNode
{
  public:
    /// @brief 添加一个元素。
    /// @param child 子实体或子控件标识。
    void Add(std::unique_ptr<BehaviorNode> child);
    /// @brief 推进一次按帧更新。
    /// @param blackboard 行为树读取和修改的 Blackboard。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    /// @return 返回接口计算或查询得到的结果。
    BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) override;
    /// @brief 将对象恢复为初始状态。
    void Reset() override;

  private:
    std::vector<std::unique_ptr<BehaviorNode>> m_children;
    std::size_t m_current = 0;
};

class SelectorNode final : public BehaviorNode
{
  public:
    /// @brief 添加一个元素。
    /// @param child 子实体或子控件标识。
    void Add(std::unique_ptr<BehaviorNode> child);
    /// @brief 推进一次按帧更新。
    /// @param blackboard 行为树读取和修改的 Blackboard。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    /// @return 返回接口计算或查询得到的结果。
    BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) override;
    /// @brief 将对象恢复为初始状态。
    void Reset() override;

  private:
    std::vector<std::unique_ptr<BehaviorNode>> m_children;
    std::size_t m_current = 0;
};

class WaitNode final : public BehaviorNode
{
  public:
    /// @brief 构造 `WaitNode` 对象。
    /// @param seconds 时间长度，单位为秒。
    explicit WaitNode(float seconds);
    /// @brief 推进一次按帧更新。
    /// @param blackboard 行为树读取和修改的 Blackboard。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    /// @return 返回接口计算或查询得到的结果。
    BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) override;
    /// @brief 将对象恢复为初始状态。
    void Reset() override;

  private:
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
};

class BehaviorTree
{
  public:
    /// @brief 设置 `Root`。
    /// @param root 根目录、根节点或根实体。
    void SetRoot(std::unique_ptr<BehaviorNode> root);
    /// @brief 推进一次按帧更新。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    /// @return 返回接口计算或查询得到的结果。
    BehaviorStatus Tick(float deltaTime);
    /// @brief 将对象恢复为初始状态。
    void Reset();
    /// @brief 获取 `Blackboard`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    Blackboard &GetBlackboard();
    /// @brief 获取 `Blackboard`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const Blackboard &GetBlackboard() const;

  private:
    std::unique_ptr<BehaviorNode> m_root;
    Blackboard m_blackboard;
};

struct BehaviorGraphNode
{
    std::uint32_t id = 0;
    std::string type;
    std::string label;
    Vec2 editorPosition{};
    std::vector<std::uint32_t> children;
};

class BehaviorTreeGraph
{
  public:
    std::uint32_t root = 0;
    std::vector<BehaviorGraphNode> nodes;
    /// @brief 判断是否满足 `Valid` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsValid() const;
    /// @brief 查找匹配的对象。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const BehaviorGraphNode *Find(std::uint32_t id) const;
};

/// @brief 保存 `BehaviorTreeGraph`。
/// @param graph 要执行、保存或设置的图定义。
/// @param path 文件、资源或目录路径。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool SaveBehaviorTreeGraph(const BehaviorTreeGraph &graph, const std::filesystem::path &path);
/// @brief 加载 `BehaviorTreeGraph`。
/// @param path 文件、资源或目录路径。
/// @param graph 要执行、保存或设置的图定义。
/// @return 操作成功时返回 `true`，否则返回 `false`。
bool LoadBehaviorTreeGraph(const std::filesystem::path &path, BehaviorTreeGraph &graph);
} // 结束 MiniEngine 命名空间
