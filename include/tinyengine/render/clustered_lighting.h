// 文件：include/tinyengine/render/clustered_lighting.h
// 作用：把局部光源分配到视锥分簇，供 Forward+ 光照阶段查询。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：公开声明。

#pragma once

#include "tinyengine/math/math.h"

#include <cstdint>
#include <vector>

namespace tinyengine
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

    /// @brief 获取 `ClusterCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getClusterCount() const;
    /// @brief 计算深度 `Slice`。
    /// @param viewDepth 接口所需的输入参数。
    /// @return 返回计算得到的数值、数量或索引。
    std::uint32_t depthSlice(float viewDepth) const;
};

class ClusteredLightBuilder
{
  public:
    /// @brief 根据当前输入构建结果。
    /// @param lights 接口所需的输入参数。
    /// @param view 当前帧的相机与光照视图数据。
    /// @param projection 相关文件、资源或目录路径。
    /// @param viewportWidth 目标尺寸、数量、索引或限制值。
    /// @param viewportHeight 目标尺寸、数量、索引或限制值。
    /// @param nearPlane 相机近裁剪面距离。
    /// @param farPlane 相机远裁剪面距离。
    /// @param tilesX 接口所需的输入参数。
    /// @param tilesY 接口所需的输入参数。
    /// @param slicesZ 接口所需的输入参数。
    /// @param maximumLightsPerCluster 目标尺寸、数量、索引或限制值。
    /// @return 返回接口计算或查询得到的结果。
    static ClusteredLightGrid build(const std::vector<RenderLight>& lights, const Mat4& view, const Mat4& projection,
                                    std::uint32_t viewportWidth, std::uint32_t viewportHeight, float nearPlane,
                                    float farPlane, std::uint32_t tilesX = 16, std::uint32_t tilesY = 9,
                                    std::uint32_t slicesZ = 24, std::uint32_t maximumLightsPerCluster = 64);
};
} // namespace tinyengine
