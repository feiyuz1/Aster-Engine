// 文件：include/MiniEngine/Terrain/HeightField.hpp
// 作用：表示高度场地形数据，并提供采样、分块和碰撞构建。
// 约定：大地形按块工作，查询接口不得假设全部块常驻。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Resource/Resource.hpp"

#include <vector>

namespace MiniEngine
{
struct TerrainMeshData
{
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

class HeightField
{
  public:
    /// @brief 应用配置并重建必要状态。
    /// @param width 目标宽度。
    /// @param height 目标高度。
    /// @param spacing 布局元素之间的间距。
    /// @param heights 目标尺寸、数量、索引或限制值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Configure(int width, int height, float spacing, std::vector<float> heights);
    /// @brief 采样 `Height`。
    /// @param x X 轴坐标或分量。
    /// @param z Z 轴坐标或分量。
    /// @return 返回计算得到的数值、数量或索引。
    float SampleHeight(float x, float z) const;
    /// @brief 采样 `Normal`。
    /// @param x X 轴坐标或分量。
    /// @param z Z 轴坐标或分量。
    /// @return 返回接口计算或查询得到的结果。
    Vec3 SampleNormal(float x, float z) const;
    /// @brief 构建 `Mesh`。
    /// @return 返回接口计算或查询得到的结果。
    TerrainMeshData BuildMesh() const;
    /// @brief 获取 `Width`。
    /// @return 返回计算得到的数值、数量或索引。
    int GetWidth() const;
    /// @brief 获取 `Height`。
    /// @return 返回计算得到的数值、数量或索引。
    int GetHeight() const;

  private:
    /// @brief 访问指定行列或索引处的元素。
    /// @param x X 轴坐标或分量。
    /// @param z Z 轴坐标或分量。
    /// @return 返回计算得到的数值、数量或索引。
    float At(int x, int z) const;
    int m_width = 0;
    int m_height = 0;
    float m_spacing = 1.0f;
    std::vector<float> m_heights;
};
} // namespace MiniEngine
