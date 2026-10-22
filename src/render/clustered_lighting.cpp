// 文件：src/render/clustered_lighting.cpp
// 作用：把局部光源分配到视锥分簇，供 Forward+ 光照阶段查询。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：实现文件。

#include "tinyengine/render/clustered_lighting.h"

#include <algorithm>
#include <cmath>

namespace tinyengine
{
namespace
{
Vec3 transformPosition(const Mat4& matrix, const Vec3& position)
{
    return {
        matrix.at(0, 0) * position.x + matrix.at(0, 1) * position.y + matrix.at(0, 2) * position.z + matrix.at(0, 3),
        matrix.at(1, 0) * position.x + matrix.at(1, 1) * position.y + matrix.at(1, 2) * position.z + matrix.at(1, 3),
        matrix.at(2, 0) * position.x + matrix.at(2, 1) * position.y + matrix.at(2, 2) * position.z + matrix.at(2, 3)};
}

std::uint32_t clampTile(const float normalized, const std::uint32_t count)
{
    return static_cast<std::uint32_t>(std::clamp(static_cast<int>(std::floor(normalized * static_cast<float>(count))),
                                                 0, static_cast<int>(count) - 1));
}
} // namespace

std::size_t ClusteredLightGrid::getClusterCount() const
{
    return static_cast<std::size_t>(tilesX) * tilesY * slicesZ;
}

std::uint32_t ClusteredLightGrid::depthSlice(const float viewDepth) const
{
    if (slicesZ <= 1 || farPlane <= nearPlane)
        return 0;
    const float depth = std::clamp(viewDepth, nearPlane, farPlane);
    const float normalized = std::log(depth / nearPlane) / std::log(farPlane / nearPlane);
    return std::min(slicesZ - 1, static_cast<std::uint32_t>(normalized * static_cast<float>(slicesZ)));
}

ClusteredLightGrid ClusteredLightBuilder::build(const std::vector<RenderLight>& lights, const Mat4& view,
                                                const Mat4& projection, const std::uint32_t viewportWidth,
                                                const std::uint32_t viewportHeight, const float nearPlane,
                                                const float farPlane, const std::uint32_t tilesX,
                                                const std::uint32_t tilesY, const std::uint32_t slicesZ,
                                                const std::uint32_t maximumLightsPerCluster)
{
    ClusteredLightGrid result;
    result.tilesX = std::max(1u, tilesX);
    result.tilesY = std::max(1u, tilesY);
    result.slicesZ = std::max(1u, slicesZ);
    result.nearPlane = std::max(0.001f, nearPlane);
    result.farPlane = std::max(result.nearPlane + 0.001f, farPlane);
    const std::size_t clusterCount = result.getClusterCount();
    // 一次收集引用，再按前缀和写入连续数组，避免每个光簇各自分配小 vector。
    std::vector<std::pair<std::size_t, std::uint32_t>> references;
    references.reserve(clusterCount);
    result.counts.assign(clusterCount, 0);
    const auto append =
        [&](const std::uint32_t x, const std::uint32_t y, const std::uint32_t z, const std::uint32_t light)
    {
        const std::size_t cluster = (static_cast<std::size_t>(z) * result.tilesY + y) * result.tilesX + x;
        if (result.counts[cluster] < maximumLightsPerCluster)
        {
            ++result.counts[cluster];
            references.emplace_back(cluster, light);
        }
    };

    for (std::uint32_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex)
    {
        const auto& light = lights[lightIndex];
        if (light.type == RenderLightType::Directional)
        {
            for (std::uint32_t z = 0; z < result.slicesZ; ++z)
                for (std::uint32_t y = 0; y < result.tilesY; ++y)
                    for (std::uint32_t x = 0; x < result.tilesX; ++x)
                        append(x, y, z, lightIndex);
            continue;
        }

        const Vec3 viewPosition = transformPosition(view, light.position);
        const float centerDepth = -viewPosition.z;
        const float range = std::max(0.001f, light.range);
        if (centerDepth + range < result.nearPlane || centerDepth - range > result.farPlane)
            continue;
        const float safeDepth = std::max(result.nearPlane, centerDepth);
        const float ndcX = projection.at(0, 0) * viewPosition.x / safeDepth;
        const float ndcY = projection.at(1, 1) * viewPosition.y / safeDepth;
        const float radiusX = std::abs(projection.at(0, 0) * range / std::max(result.nearPlane, centerDepth - range));
        const float radiusY = std::abs(projection.at(1, 1) * range / std::max(result.nearPlane, centerDepth - range));
        const float minimumX = std::clamp(ndcX - radiusX, -1.0f, 1.0f) * 0.5f + 0.5f;
        const float maximumX = std::clamp(ndcX + radiusX, -1.0f, 1.0f) * 0.5f + 0.5f;
        const float minimumY = std::clamp(ndcY - radiusY, -1.0f, 1.0f) * 0.5f + 0.5f;
        const float maximumY = std::clamp(ndcY + radiusY, -1.0f, 1.0f) * 0.5f + 0.5f;
        const std::uint32_t x0 = clampTile(minimumX, result.tilesX), x1 = clampTile(maximumX, result.tilesX);
        const std::uint32_t y0 = clampTile(minimumY, result.tilesY), y1 = clampTile(maximumY, result.tilesY);
        const std::uint32_t z0 = result.depthSlice(std::max(result.nearPlane, centerDepth - range));
        const std::uint32_t z1 = result.depthSlice(std::min(result.farPlane, centerDepth + range));
        for (std::uint32_t z = z0; z <= z1; ++z)
            for (std::uint32_t y = y0; y <= y1; ++y)
                for (std::uint32_t x = x0; x <= x1; ++x)
                    append(x, y, z, lightIndex);
    }

    result.offsets.resize(clusterCount);
    std::uint32_t offset = 0;
    for (std::size_t cluster = 0; cluster < clusterCount; ++cluster)
    {
        result.offsets[cluster] = offset;
        offset += result.counts[cluster];
    }
    result.lightIndices.resize(offset);
    auto cursors = result.offsets;
    for (const auto& reference : references)
        result.lightIndices[cursors[reference.first]++] = reference.second;
    (void)viewportWidth;
    (void)viewportHeight;
    return result;
}
} // namespace tinyengine
