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
    template <typename T> void Set(std::string key, T value)
    {
        m_values[std::move(key)] = BlackboardValue{std::move(value)};
    }
    template <typename T> const T *Get(const std::string &key) const
    {
        const auto found = m_values.find(key);
        return found == m_values.end() ? nullptr : std::get_if<T>(&found->second);
    }
    bool Remove(const std::string &key);
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
    virtual ~BehaviorNode() = default;
    virtual BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) = 0;
    virtual void Reset()
    {
    }
};

class ActionNode final : public BehaviorNode
{
  public:
    using Action = std::function<BehaviorStatus(Blackboard &, float)>;
    explicit ActionNode(Action action);
    BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) override;

  private:
    Action m_action;
};

class ConditionNode final : public BehaviorNode
{
  public:
    using Condition = std::function<bool(const Blackboard &)>;
    explicit ConditionNode(Condition condition);
    BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) override;

  private:
    Condition m_condition;
};

class SequenceNode final : public BehaviorNode
{
  public:
    void Add(std::unique_ptr<BehaviorNode> child);
    BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) override;
    void Reset() override;

  private:
    std::vector<std::unique_ptr<BehaviorNode>> m_children;
    std::size_t m_current = 0;
};

class SelectorNode final : public BehaviorNode
{
  public:
    void Add(std::unique_ptr<BehaviorNode> child);
    BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) override;
    void Reset() override;

  private:
    std::vector<std::unique_ptr<BehaviorNode>> m_children;
    std::size_t m_current = 0;
};

class WaitNode final : public BehaviorNode
{
  public:
    explicit WaitNode(float seconds);
    BehaviorStatus Tick(Blackboard &blackboard, float deltaTime) override;
    void Reset() override;

  private:
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
};

class BehaviorTree
{
  public:
    void SetRoot(std::unique_ptr<BehaviorNode> root);
    BehaviorStatus Tick(float deltaTime);
    void Reset();
    Blackboard &GetBlackboard();
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
    bool IsValid() const;
    const BehaviorGraphNode *Find(std::uint32_t id) const;
};

bool SaveBehaviorTreeGraph(const BehaviorTreeGraph &graph, const std::filesystem::path &path);
bool LoadBehaviorTreeGraph(const std::filesystem::path &path, BehaviorTreeGraph &graph);
} // namespace MiniEngine
