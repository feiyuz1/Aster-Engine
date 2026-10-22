// 文件：include/tinyengine/navigation/navigation.h
// 作用：实现网格/多边形寻路、动态障碍、Crowd 与 EQS 查询。
// 约定：导航查询可能跨帧执行，动态重建时应避免暴露半成品数据。
// 类型：公开声明。

#pragma once

#include "tinyengine/math/math.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace tinyengine
{
struct NavCell
{
    int x = 0;
    int z = 0;
    /// @brief 实现 `==` 运算符语义。
    /// @param other 用于比较的另一个对象。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool operator==(const NavCell& other) const
    {
        return x == other.x && z == other.z;
    }
};

class NavigationGrid
{
  public:
    /// @brief 应用配置并重建必要状态。
    /// @param origin 射线、查询或坐标系原点。
    /// @param width 目标宽度。
    /// @param height 目标高度。
    /// @param cellSize 目标尺寸、数量、索引或限制值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool configure(const Vec3& origin, int width, int height, float cellSize);
    /// @brief 清空对象保存的状态和数据。
    void clear();
    /// @brief 判断是否满足 `Configured` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isConfigured() const;
    /// @brief 设置 `Walkable`。
    /// @param x X 轴坐标或分量。
    /// @param z Z 轴坐标或分量。
    /// @param walkable 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool setWalkable(int x, int z, bool walkable);
    /// @brief 设置 `TraversalCost`。
    /// @param x X 轴坐标或分量。
    /// @param z Z 轴坐标或分量。
    /// @param cost 导航或查询使用的代价值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool setTraversalCost(int x, int z, float cost);
    /// @brief 判断是否满足 `Walkable` 条件。
    /// @param x X 轴坐标或分量。
    /// @param z Z 轴坐标或分量。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isWalkable(int x, int z) const;
    /// @brief 标记 `Blocked`。
    /// @param bounds 查询或对象使用的轴对齐包围盒。
    void markBlocked(const Aabb& bounds);
    /// @brief 把世界坐标转换为 `ToCell`。
    /// @param position 世界空间或局部空间位置。
    /// @param cell 输入或输出的导航网格单元。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool worldToCell(const Vec3& position, NavCell& cell) const;
    /// @brief 把网格坐标转换为 `ToWorld`。
    /// @param cell 输入或输出的导航网格单元。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 cellToWorld(const NavCell& cell) const;
    /// @brief 查找 `Path`。
    /// @param start 路径、范围或状态的起点。
    /// @param goal 寻路或查询的目标位置。
    /// @param allowDiagonal 是否启用对应行为。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<Vec3> FindPath(const Vec3& start, const Vec3& goal, bool allowDiagonal = true) const;
    /// @brief 获取 `Width`。
    /// @return 返回计算得到的数值、数量或索引。
    int getWidth() const;
    /// @brief 获取 `Height`。
    /// @return 返回计算得到的数值、数量或索引。
    int getHeight() const;
    /// @brief 获取 `CellSize`。
    /// @return 返回计算得到的数值、数量或索引。
    float getCellSize() const;

  private:
    /// @brief 判断集合是否包含目标元素。
    /// @param x X 轴坐标或分量。
    /// @param z Z 轴坐标或分量。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool contains(int x, int z) const;
    /// @brief 执行 `Index` 所表示的操作。
    /// @param x X 轴坐标或分量。
    /// @param z Z 轴坐标或分量。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t index(int x, int z) const;
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
    /// @brief 根据当前输入构建结果。
    /// @param vertices 接口所需的输入参数。
    /// @param triangleIndices 本次操作使用的数值。
    /// @param maximumSlopeDegrees 角色可站立坡面的最大角度。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool build(const std::vector<Vec3>& vertices, const std::vector<std::uint32_t>& triangleIndices,
               float maximumSlopeDegrees = 50.0f);
    /// @brief 清空对象保存的状态和数据。
    void clear();
    /// @brief 查找 `Polygon`。
    /// @param position 世界空间或局部空间位置。
    /// @return 返回计算得到的数值、数量或索引。
    int findPolygon(const Vec3& position) const;
    /// @brief 查找 `Path`。
    /// @param start 路径、范围或状态的起点。
    /// @param goal 寻路或查询的目标位置。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<Vec3> FindPath(const Vec3& start, const Vec3& goal) const;
    /// @brief 添加 `DynamicObstacle`。
    /// @param bounds 查询或对象使用的轴对齐包围盒。
    /// @return 返回计算得到的数值、数量或索引。
    std::uint64_t addDynamicObstacle(const Aabb& bounds);
    /// @brief 更新 `DynamicObstacle`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param bounds 查询或对象使用的轴对齐包围盒。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool updateDynamicObstacle(std::uint64_t id, const Aabb& bounds);
    /// @brief 移除 `DynamicObstacle`。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool removeDynamicObstacle(std::uint64_t id);
    /// @brief 重新构建 `DynamicObstacles`。
    void rebuildDynamicObstacles();
    /// @brief 获取 `Polygons`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<NavPolygon>& GetPolygons() const;
    /// @brief 判断是否满足 `PolygonBlocked` 条件。
    /// @param polygon 接口所需的输入参数。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isPolygonBlocked(std::size_t polygon) const;

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
    /// @brief 构造 `NavCrowd` 对象。
    /// @param navMesh 用于查询或更新的导航网格。
    explicit NavCrowd(const PolygonNavMesh* navMesh = nullptr);
    /// @brief 设置 `NavMesh`。
    /// @param navMesh 用于查询或更新的导航网格。
    void setNavMesh(const PolygonNavMesh* navMesh);
    /// @brief 添加 `Agent`。
    /// @param position 世界空间或局部空间位置。
    /// @param radius 球体、影响范围或查询半径。
    /// @param maximumSpeed 目标尺寸、数量、索引或限制值。
    /// @return 返回计算得到的数值、数量或索引。
    std::uint64_t addAgent(const Vec3& position, float radius = 0.35f, float maximumSpeed = 3.0f);
    /// @brief 移除 `Agent`。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool removeAgent(std::uint64_t id);
    /// @brief 设置 `Target`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param target 目标数据、目标对象或目标位置。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool setTarget(std::uint64_t id, const Vec3& target);
    /// @brief 更新对象当前状态。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void Update(float deltaTime);
    /// @brief 获取 `Agents`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<CrowdAgent>& GetAgents() const;

  private:
    const PolygonNavMesh* m_navMesh = nullptr;
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
    using Test = std::function<float(const Vec3&)>;
    /// @brief 生成 `Grid`。
    /// @param center 接口所需的输入参数。
    /// @param radius 球体、影响范围或查询半径。
    /// @param spacing 布局元素之间的间距。
    void generateGrid(const Vec3& center, float radius, float spacing);
    /// @brief 生成 `PathPoints`。
    /// @param path 文件、资源或目录路径。
    void generatePathPoints(const std::vector<Vec3>& path);
    /// @brief 添加 `Test`。
    /// @param test 接口所需的输入参数。
    /// @param weight 归一化或待归一化的混合权重。
    /// @param lowerIsBetter 接口所需的输入参数。
    void addTest(Test test, float weight = 1.0f, bool lowerIsBetter = false);
    /// @brief 执行已准备好的操作。
    /// @param maximumResults 目标尺寸、数量、索引或限制值。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<EnvironmentQueryItem> execute(std::size_t maximumResults = 1) const;

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
} // namespace tinyengine
