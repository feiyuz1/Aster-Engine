#include "MiniEngine/AI/BehaviorTree.hpp"

#include <algorithm>

namespace MiniEngine
{
bool Blackboard::Remove(const std::string &key)
{
    return m_values.erase(key) != 0;
}

void Blackboard::Clear()
{
    m_values.clear();
}

ActionNode::ActionNode(Action action) : m_action(std::move(action))
{
}

BehaviorStatus ActionNode::Tick(Blackboard &blackboard, const float deltaTime)
{
    return m_action ? m_action(blackboard, deltaTime) : BehaviorStatus::Failure;
}

ConditionNode::ConditionNode(Condition condition) : m_condition(std::move(condition))
{
}

BehaviorStatus ConditionNode::Tick(Blackboard &blackboard, const float)
{
    return m_condition && m_condition(blackboard) ? BehaviorStatus::Success : BehaviorStatus::Failure;
}

void SequenceNode::Add(std::unique_ptr<BehaviorNode> child)
{
    if (child)
        m_children.push_back(std::move(child));
}

BehaviorStatus SequenceNode::Tick(Blackboard &blackboard, const float deltaTime)
{
    while (m_current < m_children.size())
    {
        const BehaviorStatus status = m_children[m_current]->Tick(blackboard, deltaTime);
        if (status == BehaviorStatus::Running)
            return status;
        if (status == BehaviorStatus::Failure)
        {
            Reset();
            return status;
        }
        m_children[m_current]->Reset();
        ++m_current;
    }
    Reset();
    return BehaviorStatus::Success;
}

void SequenceNode::Reset()
{
    for (auto &child : m_children)
        child->Reset();
    m_current = 0;
}

void SelectorNode::Add(std::unique_ptr<BehaviorNode> child)
{
    if (child)
        m_children.push_back(std::move(child));
}

BehaviorStatus SelectorNode::Tick(Blackboard &blackboard, const float deltaTime)
{
    while (m_current < m_children.size())
    {
        const BehaviorStatus status = m_children[m_current]->Tick(blackboard, deltaTime);
        if (status == BehaviorStatus::Running)
            return status;
        if (status == BehaviorStatus::Success)
        {
            Reset();
            return status;
        }
        m_children[m_current]->Reset();
        ++m_current;
    }
    Reset();
    return BehaviorStatus::Failure;
}

void SelectorNode::Reset()
{
    for (auto &child : m_children)
        child->Reset();
    m_current = 0;
}

WaitNode::WaitNode(const float seconds) : m_duration(std::max(0.0f, seconds))
{
}

BehaviorStatus WaitNode::Tick(Blackboard &, const float deltaTime)
{
    m_elapsed += std::max(0.0f, deltaTime);
    return m_elapsed >= m_duration ? BehaviorStatus::Success : BehaviorStatus::Running;
}

void WaitNode::Reset()
{
    m_elapsed = 0.0f;
}

void BehaviorTree::SetRoot(std::unique_ptr<BehaviorNode> root)
{
    m_root = std::move(root);
}

BehaviorStatus BehaviorTree::Tick(const float deltaTime)
{
    return m_root ? m_root->Tick(m_blackboard, deltaTime) : BehaviorStatus::Failure;
}

void BehaviorTree::Reset()
{
    if (m_root)
        m_root->Reset();
}

Blackboard &BehaviorTree::GetBlackboard()
{
    return m_blackboard;
}

const Blackboard &BehaviorTree::GetBlackboard() const
{
    return m_blackboard;
}
} // namespace MiniEngine
