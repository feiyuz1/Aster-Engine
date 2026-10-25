// 文件：src/physics/physics_acceleration.h
// 作用：物理内部的包围盒树、网格缓存和场景快照。
// 约定：所有坐标空间由调用方明确；同一 PhysicsWorld 仅在所属线程访问。
// 类型：内部声明。

#pragma once

#include "tinyengine/physics/physics_world.h"

#include <array>
#include <functional>
#include <string>
#include <unordered_map>

namespace tinyengine::detail
{
struct BoundsEntry
{
    std::uint64_t payload;
    Aabb bounds;
};

/// @brief 将局部包围盒保守变换到目标空间，支持负比例和零比例。
/// @param bounds 局部包围盒。
/// @param matrix 局部到目标空间的仿射矩阵。
/// @return 包含全部变换后角点的轴对齐包围盒。
Aabb transformBounds(const Aabb& bounds, const Mat4& matrix);

/// @brief 比较包围盒所有坐标。
/// @param left 左侧包围盒。
/// @param right 右侧包围盒。
/// @return 坐标逐项相等时为 true。
bool sameBounds(const Aabb& left, const Aabb& right);

class BoundsTree
{
  public:
    /// @brief 更新叶子并自底向上重算节点；成员变化或质量明显退化时重建。
    /// @param entries 当前全部负载及其包围盒；负载必须唯一。
    /// @return 本次是否进行了非空树的重建。
    bool update(std::vector<BoundsEntry> entries);
    /// @brief 遍历与查询相交的叶子，支持访问者提前终止。
    /// @param intersects 在树坐标空间检查包围盒的谓词。
    /// @param visitor 接收负载，返回 false 立即终止。
    /// @param boundsTests 累计实际执行的包围盒谓词次数。
    void visit(const std::function<bool(const Aabb&)>& intersects, const std::function<bool(std::uint64_t)>& visitor,
               std::uint64_t& boundsTests) const;

  private:
    struct Node
    {
        Aabb bounds;
        std::size_t begin = 0, end = 0;
        int left = -1, right = -1;
    };
    /// @brief 递归按中心分布最长轴构建平衡二叉树。
    /// @param begin 起始叶子位置。
    /// @param end 末尾之后的叶子位置。
    /// @return 新节点索引。
    int build(std::size_t begin, std::size_t end);
    std::vector<BoundsEntry> m_entries;
    std::vector<Node> m_nodes;
    std::unordered_map<std::uint64_t, std::size_t> m_payloadIndices;
    double m_buildCost = 0.0;
};

struct CollisionMesh
{
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    Aabb bounds;
    BoundsTree tree;
};

struct MeshCacheEntry
{
    std::string sourceKey;
    std::uint64_t checkedEpoch = 0;
    std::shared_ptr<const CollisionMesh> mesh;
};

/// @brief 场景差异同步的单实体记录：缓存上次同步时决定包围盒的全部输入。
/// @note 网格/凸包形状的包围盒还依赖网格资产内容（可能热重载），不参与签名，每次重算。
struct SceneSyncRecord
{
    std::uint64_t entity = 0;
    std::uint64_t lastSeen = 0; // 最近一次同步戳；与当前戳不符即实体/组件已消失
    Aabb bounds;
    Vec3 position{}, rotation{}, scale{};
    Vec3 halfExtents{};
    float radius = 0.0f;
    float height = 0.0f;
    std::uint32_t shape = 0;
    std::uint32_t bodyType = 0;
    std::string meshPath;

    /// @brief 判断自上次同步以来包围盒的输入是否完全一致。
    bool matches(const Vec3& currentPosition, const Vec3& currentRotation, const Vec3& currentScale,
                 std::uint32_t currentShape, const Vec3& currentHalfExtents, float currentRadius,
                 float currentHeight, const std::string& currentMeshPath, std::uint32_t currentBodyType) const
    {
        return sameVec(position, currentPosition) && sameVec(rotation, currentRotation) &&
               sameVec(scale, currentScale) && shape == currentShape &&
               sameVec(halfExtents, currentHalfExtents) && radius == currentRadius && height == currentHeight &&
               bodyType == currentBodyType && meshPath == currentMeshPath;
    }

  private:
    static bool sameVec(const Vec3& left, const Vec3& right)
    {
        return left.x == right.x && left.y == right.y && left.z == right.z;
    }

  public:
};

struct PhysicsAcceleration
{
    /// @brief 开始一次读取操作；每个操作内同一路径只检查一次来源元数据。
    void beginOperation();
    /// @brief 加载或复用局部空间碰撞网格及其三角形树。
    /// @param path 与 MeshResource 相同的逻辑或物理路径。
    /// @return 成功时返回缓存几何；失败返回空指针，来源变化后重试。
    std::shared_ptr<const CollisionMesh> meshFor(const std::string& path);
    std::uint64_t epoch = 0;
    std::unordered_map<std::string, MeshCacheEntry> meshes;
    PhysicsStatistics statistics;
    BoundsTree staticTree, dynamicTree;
    std::vector<BoundsEntry> sceneEntries;
    // ---- 场景差异同步状态（与 sceneEntries 同下标对齐）----
    std::vector<SceneSyncRecord> sceneRecords;
    std::unordered_map<std::uint64_t, std::size_t> sceneIndex;
    std::uint64_t syncStamp = 0;
    bool sceneStructureDirty = true; // 首次同步或实体增删后需要重建树
};
} // namespace tinyengine::detail
