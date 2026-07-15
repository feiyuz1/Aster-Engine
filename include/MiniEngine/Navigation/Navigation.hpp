#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
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

struct NavPolygon
{
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> neighbors;
    float traversalCost = 1.0f;
    bool walkable = true;
};

class PolygonNavMesh
{
  public:
    bool Build(const std::vector<Vec3> &vertices, const std::vector<std::uint32_t> &triangleIndices, float maximumSlopeDegrees = 50.0f);
    void Clear();
    int FindPolygon(const Vec3 &position) const;
    std::vector<Vec3> FindPath(const Vec3 &start, const Vec3 &goal) const;
    std::uint64_t AddDynamicObstacle(const Aabb &bounds);
    bool UpdateDynamicObstacle(std::uint64_t id, const Aabb &bounds);
    bool RemoveDynamicObstacle(std::uint64_t id);
    void RebuildDynamicObstacles();
    const std::vector<NavPolygon> &GetPolygons() const;
    bool IsPolygonBlocked(std::size_t polygon) const;

  private:
    std::vector<NavPolygon> m_polygons;
    std::vector<bool> m_dynamicBlocked;
    std::unordered_map<std::uint64_t, Aabb> m_obstacles;
    std::uint64_t m_nextObstacle = 1;
};

struct CrowdAgent
{
    std::uint64_t id = 0;
    Vec3 position{};
    Vec3 velocity{};
    Vec3 target{};
    float radius = 0.35f;
    float maximumSpeed = 3.0f;
    std::vector<Vec3> path;
    std::size_t pathPoint = 0;
};

class NavCrowd
{
  public:
    explicit NavCrowd(const PolygonNavMesh *navMesh = nullptr);
    void SetNavMesh(const PolygonNavMesh *navMesh);
    std::uint64_t AddAgent(const Vec3 &position, float radius = 0.35f, float maximumSpeed = 3.0f);
    bool RemoveAgent(std::uint64_t id);
    bool SetTarget(std::uint64_t id, const Vec3 &target);
    void Update(float deltaTime);
    const std::vector<CrowdAgent> &GetAgents() const;

  private:
    const PolygonNavMesh *m_navMesh = nullptr;
    std::vector<CrowdAgent> m_agents;
    std::uint64_t m_nextId = 1;
};

struct EnvironmentQueryItem
{
    Vec3 position{};
    float score = 0.0f;
    bool discarded = false;
};

class EnvironmentQuery
{
  public:
    using Test = std::function<float(const Vec3 &)>;
    void GenerateGrid(const Vec3 &center, float radius, float spacing);
    void GeneratePathPoints(const std::vector<Vec3> &path);
    void AddTest(Test test, float weight = 1.0f, bool lowerIsBetter = false);
    std::vector<EnvironmentQueryItem> Execute(std::size_t maximumResults = 1) const;

  private:
    struct WeightedTest
    {
        Test function;
        float weight = 1.0f;
        bool lowerIsBetter = false;
    };
    std::vector<Vec3> m_points;
    std::vector<WeightedTest> m_tests;
};
} // namespace MiniEngine
