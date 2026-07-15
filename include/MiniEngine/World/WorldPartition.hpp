#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
struct WorldCellId
{
    int x = 0;
    int z = 0;
    bool operator==(const WorldCellId &other) const
    {
        return x == other.x && z == other.z;
    }
};

struct WorldCellIdHash
{
    std::size_t operator()(const WorldCellId &id) const;
};

struct WorldPartitionCell
{
    WorldCellId id;
    Aabb bounds;
    std::string assetPath;
    bool loaded = false;
};

class WorldPartition
{
  public:
    using LoadCell = std::function<bool(const WorldPartitionCell &)>;
    using UnloadCell = std::function<void(const WorldPartitionCell &)>;

    bool RegisterCell(WorldPartitionCell cell);
    bool RemoveCell(const WorldCellId &id, const UnloadCell &unload = {});
    void SetStreamingDistances(float loadDistance, float unloadDistance);
    void Update(const Vec3 &streamingSource, const LoadCell &load, const UnloadCell &unload);
    void UnloadAll(const UnloadCell &unload = {});
    const WorldPartitionCell *FindCell(const WorldCellId &id) const;
    std::vector<WorldCellId> GetLoadedCells() const;
    std::size_t GetCellCount() const;

  private:
    static float DistanceToBoundsXZ(const Vec3 &point, const Aabb &bounds);
    std::unordered_map<WorldCellId, WorldPartitionCell, WorldCellIdHash> m_cells;
    float m_loadDistance = 500.0f;
    float m_unloadDistance = 650.0f;
};
} // namespace MiniEngine
