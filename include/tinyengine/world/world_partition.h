// 文件：include/tinyengine/world/world_partition.h
// 作用：按空间单元管理大型世界内容的加载、卸载和可见状态。
// 约定：分区加载改变实体集合，持有实体引用的系统必须响应卸载。
// 类型：公开声明。

#pragma once

#include "tinyengine/math/math.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tinyengine
{
struct WorldCellId
{
    int x = 0;
    int z = 0;
    /// @brief 实现 `==` 运算符语义。
    /// @param other 用于比较的另一个对象。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool operator==(const WorldCellId& other) const
    {
        return x == other.x && z == other.z;
    }
};

struct WorldCellIdHash
{
    /// @brief 实现 `()` 运算符语义。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t operator()(const WorldCellId& id) const;
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
    using LoadCell = std::function<bool(const WorldPartitionCell&)>;
    using UnloadCell = std::function<void(const WorldPartitionCell&)>;

    /// @brief 注册 `Cell`。
    /// @param cell 输入或输出的导航网格单元。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool registerCell(WorldPartitionCell cell);
    /// @brief 移除 `Cell`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param unload 是否同时卸载对应资源。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool removeCell(const WorldCellId& id, const UnloadCell& unload = {});
    /// @brief 设置 `StreamingDistances`。
    /// @param loadDistance 本次操作使用的数值。
    /// @param unloadDistance 本次操作使用的数值。
    void setStreamingDistances(float loadDistance, float unloadDistance);
    /// @brief 更新对象当前状态。
    /// @param streamingSource 相关文件、资源或目录路径。
    /// @param load 接口所需的输入参数。
    /// @param unload 是否同时卸载对应资源。
    void Update(const Vec3& streamingSource, const LoadCell& load, const UnloadCell& unload);
    /// @brief 卸载 `All`。
    /// @param unload 是否同时卸载对应资源。
    void unloadAll(const UnloadCell& unload = {});
    /// @brief 查找 `Cell`。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const WorldPartitionCell* findCell(const WorldCellId& id) const;
    /// @brief 获取 `LoadedCells`。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<WorldCellId> GetLoadedCells() const;
    /// @brief 获取 `CellCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getCellCount() const;

  private:
    /// @brief 计算距离 `ToBoundsXZ`。
    /// @param point 相关坐标或空间位置。
    /// @param bounds 查询或对象使用的轴对齐包围盒。
    /// @return 返回计算得到的数值、数量或索引。
    static float distanceToBoundsXz(const Vec3& point, const Aabb& bounds);
    std::unordered_map<WorldCellId, WorldPartitionCell, WorldCellIdHash> m_cells;
    float m_loadDistance = 500.0f;
    float m_unloadDistance = 650.0f;
};
} // namespace tinyengine
