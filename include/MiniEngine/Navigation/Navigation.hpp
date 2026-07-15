#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <cstddef>
#include <vector>

namespace MiniEngine
{
struct NavCell
{
    int x = 0;
    int z = 0;
    bool operator==(const NavCell &other) const
    {
        return x == other.x && z == other.z;
    }
};

class NavigationGrid
{
  public:
    bool Configure(const Vec3 &origin, int width, int height, float cellSize);
    void Clear();
    bool IsConfigured() const;
    bool SetWalkable(int x, int z, bool walkable);
    bool SetTraversalCost(int x, int z, float cost);
    bool IsWalkable(int x, int z) const;
    void MarkBlocked(const Aabb &bounds);
    bool WorldToCell(const Vec3 &position, NavCell &cell) const;
    Vec3 CellToWorld(const NavCell &cell) const;
    std::vector<Vec3> FindPath(const Vec3 &start, const Vec3 &goal, bool allowDiagonal = true) const;
    int GetWidth() const;
    int GetHeight() const;
    float GetCellSize() const;

  private:
    bool Contains(int x, int z) const;
    std::size_t Index(int x, int z) const;
    Vec3 m_origin{};
    int m_width = 0;
    int m_height = 0;
    float m_cellSize = 1.0f;
    std::vector<float> m_costs;
};
} // namespace MiniEngine
