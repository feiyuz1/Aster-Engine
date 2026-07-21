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
    bool Configure(int width, int height, float spacing, std::vector<float> heights);
    float SampleHeight(float x, float z) const;
    Vec3 SampleNormal(float x, float z) const;
    TerrainMeshData BuildMesh() const;
    int GetWidth() const;
    int GetHeight() const;

  private:
    float At(int x, int z) const;
    int m_width = 0;
    int m_height = 0;
    float m_spacing = 1.0f;
    std::vector<float> m_heights;
};
} // namespace MiniEngine
