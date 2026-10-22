// 文件：src/ai/behavior_tree.cpp
// 作用：定义 Blackboard、行为树节点、运行时执行和调试信息。
// 约定：行为节点返回运行状态，长任务必须可在后续帧继续。
// 类型：实现文件。

#include "tinyengine/ai/behavior_tree.h"
#include "tinyengine/platform/platform.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace tinyengine
{
bool Blackboard::Remove(const std::string& key)
{
    return m_values.erase(key) != 0;
}

void Blackboard::clear()
{
    m_values.clear();
}

ActionNode::ActionNode(Action action) : m_action(std::move(action)) {}

BehaviorStatus ActionNode::Tick(Blackboard& blackboard, const float deltaTime)
{
    return m_action ? m_action(blackboard, deltaTime) : BehaviorStatus::Failure;
}

ConditionNode::ConditionNode(Condition condition) : m_condition(std::move(condition)) {}

BehaviorStatus ConditionNode::Tick(Blackboard& blackboard, const float)
{
    return m_condition && m_condition(blackboard) ? BehaviorStatus::Success : BehaviorStatus::Failure;
}

void SequenceNode::Add(std::unique_ptr<BehaviorNode> child)
{
    if (child)
        m_children.push_back(std::move(child));
}

BehaviorStatus SequenceNode::Tick(Blackboard& blackboard, const float deltaTime)
{
    while (m_current < m_children.size())
    {
        const BehaviorStatus status = m_children[m_current]->Tick(blackboard, deltaTime);
        if (status == BehaviorStatus::Running)
            return status;
        if (status == BehaviorStatus::Failure)
        {
            reset();
            return status;
        }
        m_children[m_current]->reset();
        ++m_current;
    }
    reset();
    return BehaviorStatus::Success;
}

void SequenceNode::reset()
{
    for (auto& child : m_children)
        child->reset();
    m_current = 0;
}

void SelectorNode::Add(std::unique_ptr<BehaviorNode> child)
{
    if (child)
        m_children.push_back(std::move(child));
}

BehaviorStatus SelectorNode::Tick(Blackboard& blackboard, const float deltaTime)
{
    while (m_current < m_children.size())
    {
        const BehaviorStatus status = m_children[m_current]->Tick(blackboard, deltaTime);
        if (status == BehaviorStatus::Running)
            return status;
        if (status == BehaviorStatus::Success)
        {
            reset();
            return status;
        }
        m_children[m_current]->reset();
        ++m_current;
    }
    reset();
    return BehaviorStatus::Failure;
}

void SelectorNode::reset()
{
    for (auto& child : m_children)
        child->reset();
    m_current = 0;
}

WaitNode::WaitNode(const float seconds) : m_duration(std::max(0.0f, seconds)) {}

BehaviorStatus WaitNode::Tick(Blackboard&, const float deltaTime)
{
    m_elapsed += std::max(0.0f, deltaTime);
    return m_elapsed >= m_duration ? BehaviorStatus::Success : BehaviorStatus::Running;
}

void WaitNode::reset()
{
    m_elapsed = 0.0f;
}

void BehaviorTree::setRoot(std::unique_ptr<BehaviorNode> root)
{
    m_root = std::move(root);
}

BehaviorStatus BehaviorTree::Tick(const float deltaTime)
{
    return m_root ? m_root->Tick(m_blackboard, deltaTime) : BehaviorStatus::Failure;
}

void BehaviorTree::reset()
{
    if (m_root)
        m_root->reset();
}

Blackboard& BehaviorTree::GetBlackboard()
{
    return m_blackboard;
}

const Blackboard& BehaviorTree::GetBlackboard() const
{
    return m_blackboard;
}

const BehaviorGraphNode* BehaviorTreeGraph::Find(const std::uint32_t id) const
{
    const auto found =
        std::find_if(nodes.begin(), nodes.end(), [id](const BehaviorGraphNode& node) { return node.id == id; });
    return found == nodes.end() ? nullptr : &*found;
}

bool BehaviorTreeGraph::isValid() const
{
    if (nodes.empty() || !Find(root))
        return false;
    std::unordered_set<std::uint32_t> ids;
    for (const auto& node : nodes)
        if (!node.id || node.type.empty() || !ids.insert(node.id).second)
            return false;
    for (const auto& node : nodes)
        for (const auto child : node.children)
            if (!ids.count(child))
                return false;
    std::unordered_set<std::uint32_t> visiting, visited;
    std::function<bool(std::uint32_t)> visit = [&](const std::uint32_t id)
    {
        if (visiting.count(id))
            return false;
        if (visited.count(id))
            return true;
        visiting.insert(id);
        for (const auto child : Find(id)->children)
            if (!visit(child))
                return false;
        visiting.erase(id);
        visited.insert(id);
        return true;
    };
    return visit(root);
}

bool saveBehaviorTreeGraph(const BehaviorTreeGraph& graph, const std::filesystem::path& path)
{
    if (!graph.isValid())
        return false;
    std::ostringstream output;
    output << "TINY_BTGRAPH 1\nroot " << graph.root << '\n';
    for (const auto& node : graph.nodes)
    {
        output << "node " << node.id << ' ' << std::quoted(node.type) << ' ' << std::quoted(node.label) << ' '
               << node.editorPosition.x << ' ' << node.editorPosition.y;
        for (const auto child : node.children)
            output << ' ' << child;
        output << " 0\n";
    }
    return PlatformFile::writeAllText(path, output.str());
}

bool loadBehaviorTreeGraph(const std::filesystem::path& path, BehaviorTreeGraph& graph)
{
    std::istringstream input(PlatformFile::readAllText(path));
    std::string magic, token;
    int version = 0;
    BehaviorTreeGraph result;
    if (!(input >> magic >> version) || magic != "TINY_BTGRAPH" || version != 1)
        return false;
    while (input >> token)
    {
        if (token == "root")
            input >> result.root;
        else if (token == "node")
        {
            BehaviorGraphNode node;
            if (!(input >> node.id >> std::quoted(node.type) >> std::quoted(node.label) >> node.editorPosition.x >>
                  node.editorPosition.y))
                return false;
            std::uint32_t child = 0;
            while (input >> child && child != 0)
                node.children.push_back(child);
            result.nodes.push_back(std::move(node));
        }
        else
            return false;
        if (!input)
            return false;
    }
    if (!result.isValid())
        return false;
    graph = std::move(result);
    return true;
}
} // namespace tinyengine
