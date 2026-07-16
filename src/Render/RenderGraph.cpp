#include "MiniEngine/Render/RenderGraph.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <unordered_set>

namespace MiniEngine
{
namespace
{
bool CompatibleForAliasing(const RenderGraphResourceDesc &left, const RenderGraphResourceDesc &right)
{
    return left.transient && right.transient && !left.external && !right.external && left.type == right.type && left.width == right.width &&
           left.height == right.height && left.depth == right.depth && left.format == right.format;
}
} // namespace

RenderGraphResourceHandle RenderGraph::CreateResource(RenderGraphResourceDesc desc)
{
    desc.external = false;
    m_resources.push_back(std::move(desc));
    m_compiled = false;
    return {static_cast<std::uint32_t>(m_resources.size())};
}

RenderGraphResourceHandle RenderGraph::ImportResource(RenderGraphResourceDesc desc)
{
    desc.external = true;
    desc.transient = false;
    m_resources.push_back(std::move(desc));
    m_compiled = false;
    return {static_cast<std::uint32_t>(m_resources.size())};
}

std::size_t RenderGraph::AddPass(RenderGraphPassDesc pass)
{
    m_passes.push_back(std::move(pass));
    m_compiled = false;
    return m_passes.size() - 1;
}

bool RenderGraph::Compile(std::string *error)
{
    m_executionOrder.clear();
    m_lifetimes.assign(m_resources.size(), {});
    m_physicalResourceCount = 0;
    const auto fail = [&](const std::string &message) {
        if (error)
            *error = message;
        return false;
    };
    if (m_passes.empty())
    {
        m_compiled = true;
        return true;
    }

    const std::size_t passCount = m_passes.size();
    std::vector<std::unordered_set<std::size_t>> edges(passCount);
    std::vector<std::size_t> indegrees(passCount, 0);
    std::vector<std::size_t> lastWriter(m_resources.size(), std::numeric_limits<std::size_t>::max());
    std::vector<std::vector<std::size_t>> readers(m_resources.size());
    std::vector<bool> initialized(m_resources.size(), false);
    for (std::size_t resource = 0; resource < m_resources.size(); ++resource)
    {
        initialized[resource] = m_resources[resource].external;
        m_lifetimes[resource].firstPass = passCount;
    }

    const auto addEdge = [&](const std::size_t from, const std::size_t to) {
        if (from != to && edges[from].insert(to).second)
            ++indegrees[to];
    };
    for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex)
    {
        const auto &pass = m_passes[passIndex];
        if (pass.name.empty())
            return fail("RenderGraph pass name cannot be empty");
        for (const auto handle : pass.reads)
        {
            if (!IsValid(handle))
                return fail("RenderGraph pass '" + pass.name + "' reads an invalid resource");
            const std::size_t resource = handle.id - 1;
            if (!initialized[resource])
                return fail("RenderGraph pass '" + pass.name + "' reads resource '" + m_resources[resource].name +
                            "' before it is written");
            if (lastWriter[resource] != std::numeric_limits<std::size_t>::max())
                addEdge(lastWriter[resource], passIndex);
            readers[resource].push_back(passIndex);
            m_lifetimes[resource].firstPass = std::min(m_lifetimes[resource].firstPass, passIndex);
            m_lifetimes[resource].lastPass = std::max(m_lifetimes[resource].lastPass, passIndex);
        }
        for (const auto handle : pass.writes)
        {
            if (!IsValid(handle))
                return fail("RenderGraph pass '" + pass.name + "' writes an invalid resource");
            const std::size_t resource = handle.id - 1;
            if (lastWriter[resource] != std::numeric_limits<std::size_t>::max())
                addEdge(lastWriter[resource], passIndex);
            for (const std::size_t reader : readers[resource])
                addEdge(reader, passIndex);
            readers[resource].clear();
            lastWriter[resource] = passIndex;
            initialized[resource] = true;
            m_lifetimes[resource].firstPass = std::min(m_lifetimes[resource].firstPass, passIndex);
            m_lifetimes[resource].lastPass = std::max(m_lifetimes[resource].lastPass, passIndex);
        }
    }

    std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
    for (std::size_t pass = 0; pass < passCount; ++pass)
        if (indegrees[pass] == 0)
            ready.push(pass);
    while (!ready.empty())
    {
        const std::size_t pass = ready.top();
        ready.pop();
        m_executionOrder.push_back(pass);
        for (const std::size_t dependent : edges[pass])
            if (--indegrees[dependent] == 0)
                ready.push(dependent);
    }
    if (m_executionOrder.size() != passCount)
        return fail("RenderGraph contains a dependency cycle");

    struct AliasSlot
    {
        RenderGraphResourceDesc desc;
        std::size_t lastPass = 0;
        std::uint32_t id = 0;
    };
    std::vector<AliasSlot> slots;
    for (std::size_t resource = 0; resource < m_resources.size(); ++resource)
    {
        auto &lifetime = m_lifetimes[resource];
        if (lifetime.firstPass == passCount)
        {
            lifetime.firstPass = lifetime.lastPass = 0;
            continue;
        }
        if (m_resources[resource].external || !m_resources[resource].transient)
        {
            lifetime.aliasSlot = static_cast<std::uint32_t>(++m_physicalResourceCount);
            continue;
        }
        auto reusable = std::find_if(slots.begin(), slots.end(), [&](const AliasSlot &slot) {
            return slot.lastPass < lifetime.firstPass && CompatibleForAliasing(slot.desc, m_resources[resource]);
        });
        if (reusable == slots.end())
        {
            const std::uint32_t id = static_cast<std::uint32_t>(++m_physicalResourceCount);
            slots.push_back({m_resources[resource], lifetime.lastPass, id});
            lifetime.aliasSlot = id;
        }
        else
        {
            reusable->lastPass = lifetime.lastPass;
            lifetime.aliasSlot = reusable->id;
        }
    }
    m_compiled = true;
    if (error)
        error->clear();
    return true;
}

bool RenderGraph::Execute(std::string *error) const
{
    if (!m_compiled)
    {
        if (error)
            *error = "RenderGraph must be compiled before execution";
        return false;
    }
    for (const std::size_t passIndex : m_executionOrder)
    {
        const auto &pass = m_passes[passIndex];
        if (pass.execute)
            pass.execute();
    }
    if (error)
        error->clear();
    return true;
}

void RenderGraph::Reset()
{
    m_resources.clear();
    m_passes.clear();
    m_lifetimes.clear();
    m_executionOrder.clear();
    m_physicalResourceCount = 0;
    m_compiled = false;
}

const std::vector<std::size_t> &RenderGraph::GetExecutionOrder() const
{
    return m_executionOrder;
}

const RenderGraphResourceDesc *RenderGraph::GetResource(const RenderGraphResourceHandle handle) const
{
    return IsValid(handle) ? &m_resources[handle.id - 1] : nullptr;
}

const RenderGraphResourceLifetime *RenderGraph::GetLifetime(const RenderGraphResourceHandle handle) const
{
    return IsValid(handle) && m_lifetimes.size() == m_resources.size() ? &m_lifetimes[handle.id - 1] : nullptr;
}

std::size_t RenderGraph::GetPassCount() const
{
    return m_passes.size();
}

std::size_t RenderGraph::GetPhysicalResourceCount() const
{
    return m_physicalResourceCount;
}

bool RenderGraph::IsValid(const RenderGraphResourceHandle handle) const
{
    return handle.id > 0 && handle.id <= m_resources.size();
}
} // namespace MiniEngine
