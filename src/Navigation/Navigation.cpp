#include "MiniEngine/Navigation/Navigation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace MiniEngine
{
bool NavigationGrid::Configure(const Vec3 &origin, const int width, const int height, const float cellSize)
{
    if (width <= 0 || height <= 0 || cellSize <= 0.0f || width > 32768 || height > 32768)
        return false;
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (count > 64ull * 1024ull * 1024ull)
        return false;
    m_origin = origin;
    m_width = width;
    m_height = height;
    m_cellSize = cellSize;
    m_costs.assign(count, 1.0f);
    return true;
}

void NavigationGrid::Clear()
{
    m_width = m_height = 0;
    m_costs.clear();
}

bool NavigationGrid::IsConfigured() const
{
    return m_width > 0 && m_height > 0 && m_costs.size() == static_cast<std::size_t>(m_width) * m_height;
}

bool NavigationGrid::SetWalkable(const int x, const int z, const bool walkable)
{
    if (!Contains(x, z))
        return false;
    m_costs[Index(x, z)] = walkable ? std::max(1.0f, m_costs[Index(x, z)]) : 0.0f;
    return true;
}

bool NavigationGrid::SetTraversalCost(const int x, const int z, const float cost)
{
    if (!Contains(x, z) || cost < 1.0f || !std::isfinite(cost))
        return false;
    m_costs[Index(x, z)] = cost;
    return true;
}

bool NavigationGrid::IsWalkable(const int x, const int z) const
{
    return Contains(x, z) && m_costs[Index(x, z)] > 0.0f;
}

void NavigationGrid::MarkBlocked(const Aabb &bounds)
{
    if (!IsConfigured())
        return;
    const int minimumX = std::max(0, static_cast<int>(std::floor((bounds.minimum.x - m_origin.x) / m_cellSize)));
    const int maximumX = std::min(m_width - 1, static_cast<int>(std::floor((bounds.maximum.x - m_origin.x) / m_cellSize)));
    const int minimumZ = std::max(0, static_cast<int>(std::floor((bounds.minimum.z - m_origin.z) / m_cellSize)));
    const int maximumZ = std::min(m_height - 1, static_cast<int>(std::floor((bounds.maximum.z - m_origin.z) / m_cellSize)));
    for (int z = minimumZ; z <= maximumZ; ++z)
        for (int x = minimumX; x <= maximumX; ++x)
            SetWalkable(x, z, false);
}

bool NavigationGrid::WorldToCell(const Vec3 &position, NavCell &cell) const
{
    if (!IsConfigured())
        return false;
    cell.x = static_cast<int>(std::floor((position.x - m_origin.x) / m_cellSize));
    cell.z = static_cast<int>(std::floor((position.z - m_origin.z) / m_cellSize));
    return Contains(cell.x, cell.z);
}

Vec3 NavigationGrid::CellToWorld(const NavCell &cell) const
{
    return {m_origin.x + (static_cast<float>(cell.x) + 0.5f) * m_cellSize, m_origin.y,
            m_origin.z + (static_cast<float>(cell.z) + 0.5f) * m_cellSize};
}

std::vector<Vec3> NavigationGrid::FindPath(const Vec3 &start, const Vec3 &goal, const bool allowDiagonal) const
{
    NavCell startCell, goalCell;
    if (!WorldToCell(start, startCell) || !WorldToCell(goal, goalCell) || !IsWalkable(startCell.x, startCell.z) ||
        !IsWalkable(goalCell.x, goalCell.z))
        return {};
    struct OpenNode
    {
        float score = 0.0f;
        std::size_t index = 0;
        bool operator<(const OpenNode &other) const
        {
            return score > other.score;
        }
    };
    const std::size_t count = m_costs.size();
    const float infinity = std::numeric_limits<float>::infinity();
    std::vector<float> distance(count, infinity);
    std::vector<std::size_t> previous(count, count);
    std::vector<bool> closed(count, false);
    std::priority_queue<OpenNode> open;
    const std::size_t startIndex = Index(startCell.x, startCell.z), goalIndex = Index(goalCell.x, goalCell.z);
    const auto heuristic = [&](const int x, const int z) {
        const int dx = std::abs(goalCell.x - x), dz = std::abs(goalCell.z - z);
        return allowDiagonal ? static_cast<float>(std::max(dx, dz)) : static_cast<float>(dx + dz);
    };
    distance[startIndex] = 0.0f;
    open.push({heuristic(startCell.x, startCell.z), startIndex});
    constexpr int offsets[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    while (!open.empty())
    {
        const std::size_t current = open.top().index;
        open.pop();
        if (closed[current])
            continue;
        if (current == goalIndex)
            break;
        closed[current] = true;
        const int x = static_cast<int>(current % static_cast<std::size_t>(m_width));
        const int z = static_cast<int>(current / static_cast<std::size_t>(m_width));
        const int neighborCount = allowDiagonal ? 8 : 4;
        for (int neighbor = 0; neighbor < neighborCount; ++neighbor)
        {
            const int nextX = x + offsets[neighbor][0], nextZ = z + offsets[neighbor][1];
            if (!IsWalkable(nextX, nextZ))
                continue;
            const bool diagonal = offsets[neighbor][0] != 0 && offsets[neighbor][1] != 0;
            if (diagonal && (!IsWalkable(x + offsets[neighbor][0], z) || !IsWalkable(x, z + offsets[neighbor][1])))
                continue;
            const std::size_t next = Index(nextX, nextZ);
            const float step = (diagonal ? 1.41421356f : 1.0f) * m_costs[next];
            if (distance[current] + step >= distance[next])
                continue;
            distance[next] = distance[current] + step;
            previous[next] = current;
            open.push({distance[next] + heuristic(nextX, nextZ), next});
        }
    }
    if (startIndex != goalIndex && previous[goalIndex] == count)
        return {};
    std::vector<Vec3> path;
    for (std::size_t current = goalIndex;; current = previous[current])
    {
        path.push_back(CellToWorld({static_cast<int>(current % static_cast<std::size_t>(m_width)),
                                    static_cast<int>(current / static_cast<std::size_t>(m_width))}));
        if (current == startIndex)
            break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

int NavigationGrid::GetWidth() const
{
    return m_width;
}

int NavigationGrid::GetHeight() const
{
    return m_height;
}

float NavigationGrid::GetCellSize() const
{
    return m_cellSize;
}

bool NavigationGrid::Contains(const int x, const int z) const
{
    return x >= 0 && z >= 0 && x < m_width && z < m_height;
}

std::size_t NavigationGrid::Index(const int x, const int z) const
{
    return static_cast<std::size_t>(z) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x);
}
} // namespace MiniEngine
