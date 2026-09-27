// 文件：src/World/WorldPartition.cpp
// 作用：按空间单元管理大型世界内容的加载、卸载和可见状态。
// 约定：分区加载改变实体集合，持有实体引用的系统必须响应卸载。
// 类型：实现文件。

#include "MiniEngine/World/WorldPartition.hpp"

#include <algorithm>
#include <cmath>

namespace MiniEngine
{
std::size_t WorldCellIdHash::operator()(const WorldCellId &id) const
{
    const std::size_t x = std::hash<int>{}(id.x), z = std::hash<int>{}(id.z);
    return x ^ (z + static_cast<std::size_t>(0x9e3779b9u) + (x << 6u) + (x >> 2u));
}

bool WorldPartition::RegisterCell(WorldPartitionCell cell)
{
    if (cell.assetPath.empty() || cell.bounds.minimum.x > cell.bounds.maximum.x || cell.bounds.minimum.z > cell.bounds.maximum.z)
        return false;
    cell.loaded = false;
    return m_cells.emplace(cell.id, std::move(cell)).second;
}

bool WorldPartition::RemoveCell(const WorldCellId &id, const UnloadCell &unload)
{
    const auto found = m_cells.find(id);
    if (found == m_cells.end())
        return false;
    if (found->second.loaded && unload)
        unload(found->second);
    m_cells.erase(found);
    return true;
}

void WorldPartition::SetStreamingDistances(const float loadDistance, const float unloadDistance)
{
    m_loadDistance = std::max(0.0f, loadDistance);
    m_unloadDistance = std::max(m_loadDistance, unloadDistance);
}

void WorldPartition::Update(const Vec3 &streamingSource, const LoadCell &load, const UnloadCell &unload)
{
    for (auto &pair : m_cells)
    {
        auto &cell = pair.second;
        const float distance = DistanceToBoundsXZ(streamingSource, cell.bounds);
        if (!cell.loaded && distance <= m_loadDistance && load)
            cell.loaded = load(cell);
        else if (cell.loaded && distance > m_unloadDistance)
        {
            if (unload)
                unload(cell);
            cell.loaded = false;
        }
    }
}

void WorldPartition::UnloadAll(const UnloadCell &unload)
{
    for (auto &pair : m_cells)
        if (pair.second.loaded)
        {
            if (unload)
                unload(pair.second);
            pair.second.loaded = false;
        }
}

const WorldPartitionCell *WorldPartition::FindCell(const WorldCellId &id) const
{
    const auto found = m_cells.find(id);
    return found == m_cells.end() ? nullptr : &found->second;
}

std::vector<WorldCellId> WorldPartition::GetLoadedCells() const
{
    std::vector<WorldCellId> result;
    for (const auto &pair : m_cells)
        if (pair.second.loaded)
            result.push_back(pair.first);
    std::sort(result.begin(), result.end(),
              [](const WorldCellId &left, const WorldCellId &right) { return left.z == right.z ? left.x < right.x : left.z < right.z; });
    return result;
}

std::size_t WorldPartition::GetCellCount() const
{
    return m_cells.size();
}

float WorldPartition::DistanceToBoundsXZ(const Vec3 &point, const Aabb &bounds)
{
    const float dx = std::max({bounds.minimum.x - point.x, 0.0f, point.x - bounds.maximum.x});
    const float dz = std::max({bounds.minimum.z - point.z, 0.0f, point.z - bounds.maximum.z});
    return std::sqrt(dx * dx + dz * dz);
}
} // 结束 MiniEngine 命名空间
