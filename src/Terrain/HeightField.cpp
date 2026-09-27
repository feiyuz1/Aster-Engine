// 文件：src/Terrain/HeightField.cpp
// 作用：表示高度场地形数据，并提供采样、分块和碰撞构建。
// 约定：大地形按块工作，查询接口不得假设全部块常驻。
// 类型：实现文件。

#include "MiniEngine/Terrain/HeightField.hpp"

#include <algorithm>
#include <cmath>

namespace MiniEngine
{
bool HeightField::Configure(const int width, const int height, const float spacing, std::vector<float> heights)
{
    if (width < 2 || height < 2 || spacing <= 0.0f || heights.size() != static_cast<std::size_t>(width) * height)
        return false;
    m_width = width;
    m_height = height;
    m_spacing = spacing;
    m_heights = std::move(heights);
    return true;
}

float HeightField::SampleHeight(const float x, const float z) const
{
    if (m_heights.empty())
        return 0.0f;
    const float gridX = std::clamp(x / m_spacing, 0.0f, static_cast<float>(m_width - 1));
    const float gridZ = std::clamp(z / m_spacing, 0.0f, static_cast<float>(m_height - 1));
    const int x0 = static_cast<int>(std::floor(gridX)), z0 = static_cast<int>(std::floor(gridZ));
    const int x1 = std::min(x0 + 1, m_width - 1), z1 = std::min(z0 + 1, m_height - 1);
    const float tx = gridX - x0, tz = gridZ - z0;
    const float top = At(x0, z0) * (1.0f - tx) + At(x1, z0) * tx;
    const float bottom = At(x0, z1) * (1.0f - tx) + At(x1, z1) * tx;
    return top * (1.0f - tz) + bottom * tz;
}

Vec3 HeightField::SampleNormal(const float x, const float z) const
{
    const float left = SampleHeight(x - m_spacing, z), right = SampleHeight(x + m_spacing, z);
    const float back = SampleHeight(x, z - m_spacing), front = SampleHeight(x, z + m_spacing);
    return Normalize(Vec3{left - right, 2.0f * m_spacing, back - front});
}

TerrainMeshData HeightField::BuildMesh() const
{
    TerrainMeshData mesh;
    if (m_heights.empty())
        return mesh;
    mesh.vertices.reserve(m_heights.size());
    for (int z = 0; z < m_height; ++z)
        for (int x = 0; x < m_width; ++x)
        {
            const Vec3 position{static_cast<float>(x) * m_spacing, At(x, z), static_cast<float>(z) * m_spacing};
            mesh.vertices.push_back({position,
                                     SampleNormal(position.x, position.z),
                                     {static_cast<float>(x) / (m_width - 1), static_cast<float>(z) / (m_height - 1)},
                                     {1, 0, 0}});
        }
    for (int z = 0; z + 1 < m_height; ++z)
        for (int x = 0; x + 1 < m_width; ++x)
        {
            const auto topLeft = static_cast<std::uint32_t>(z * m_width + x);
            const auto bottomLeft = static_cast<std::uint32_t>((z + 1) * m_width + x);
            mesh.indices.insert(mesh.indices.end(), {topLeft, bottomLeft, topLeft + 1, topLeft + 1, bottomLeft, bottomLeft + 1});
        }
    return mesh;
}

int HeightField::GetWidth() const
{
    return m_width;
}

int HeightField::GetHeight() const
{
    return m_height;
}

float HeightField::At(const int x, const int z) const
{
    return m_heights[static_cast<std::size_t>(z) * m_width + x];
}
} // 结束 MiniEngine 命名空间
