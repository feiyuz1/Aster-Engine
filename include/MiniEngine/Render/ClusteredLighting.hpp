#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <cstdint>
#include <vector>

namespace MiniEngine
{
enum class RenderLightType : std::uint8_t
{
    Directional,
    Point,
    Spot
};

struct RenderLight
{
    RenderLightType type = RenderLightType::Directional;
    Vec3 position{};
    float range = 10.0f;
    Vec3 direction{0.0f, -1.0f, 0.0f};
    float innerConeCosine = 0.9f;
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float outerConeCosine = 0.8f;
};

struct ClusteredLightGrid
{
    std::uint32_t tilesX = 16;
    std::uint32_t tilesY = 9;
    std::uint32_t slicesZ = 24;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> counts;
    std::vector<std::uint32_t> lightIndices;

    std::size_t GetClusterCount() const;
    std::uint32_t DepthSlice(float viewDepth) const;
};

class ClusteredLightBuilder
{
  public:
    static ClusteredLightGrid Build(const std::vector<RenderLight> &lights, const Mat4 &view, const Mat4 &projection,
                                    std::uint32_t viewportWidth, std::uint32_t viewportHeight, float nearPlane, float farPlane,
                                    std::uint32_t tilesX = 16, std::uint32_t tilesY = 9, std::uint32_t slicesZ = 24,
                                    std::uint32_t maximumLightsPerCluster = 64);
};
} // namespace MiniEngine
