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
