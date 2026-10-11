// 文件：src/Render/SdlGlRenderDevice.cpp
// 作用：实现基于 SDL2 与 OpenGL 的渲染设备、窗口输入和 GPU 资源提交。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：实现文件。

#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#include "GlMaterialProgramCache.hpp"

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Render/BuiltinShaders.hpp"
#include "MiniEngine/Render/ShaderPipeline.hpp"
#include "MiniEngine/UI/TextLayout.hpp"
#include "MiniEngine/VFX/ParticleSystem.hpp"

#define SDL_MAIN_HANDLED
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif
#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#ifdef TINYENGINE_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace MiniEngine
{
namespace
{
Mat4 InversePerspective(const Mat4 &projection)
{
    Mat4 inverseProjection{};
    inverseProjection.At(0, 0) = 1.0f / projection.At(0, 0);
    inverseProjection.At(1, 1) = 1.0f / projection.At(1, 1);
    inverseProjection.At(0, 3) = projection.At(0, 2) / projection.At(0, 0);
    inverseProjection.At(1, 3) = projection.At(1, 2) / projection.At(1, 1);
    inverseProjection.At(2, 3) = -1.0f;
    inverseProjection.At(3, 2) = 1.0f / projection.At(2, 3);
    inverseProjection.At(3, 3) = projection.At(2, 2) / projection.At(2, 3);
    return inverseProjection;
}

// Perspective 与 LookAt 的解析逆矩阵，避免在 TAA 的每个像素里求逆。
Mat4 InverseCameraViewProjection(const FrameView &view)
{
    Mat4 inverseView = Mat4::Identity();
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            inverseView.At(row, column) = view.view.At(column, row);
    inverseView.At(0, 3) = view.cameraPosition.x;
    inverseView.At(1, 3) = view.cameraPosition.y;
    inverseView.At(2, 3) = view.cameraPosition.z;
    return inverseView * InversePerspective(view.projection);
}

template <typename T> bool LoadGl(T &target, const char *name)
{
    target = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return target != nullptr;
}

constexpr const char *VertexShaderSource = BuiltinRenderShaders::Vertex;
constexpr const char *FragmentShaderSource = BuiltinRenderShaders::Fragment;

constexpr const char *ShadowVertexShaderSource = R"GLSL(
            #version 330 core
            layout(location = 0) in vec3 aPosition;
            layout(location = 2) in vec2 aUv;
            layout(location = 8) in vec4 aInstance0;
            layout(location = 9) in vec4 aInstance1;
            layout(location = 10) in vec4 aInstance2;
            layout(location = 11) in vec4 aInstance3;
            uniform mat4 uLightMvp;
            uniform mat4 uLightViewProjection;
            uniform int uInstanced;
            out vec2 vUv;
            void main()
            {
                mat4 model = mat4(aInstance0, aInstance1, aInstance2, aInstance3);
                gl_Position = uInstanced != 0 ? uLightViewProjection * model * vec4(aPosition, 1.0) : uLightMvp * vec4(aPosition, 1.0);
                vUv = aUv;
            }
        )GLSL";

constexpr const char *ShadowFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uShadowAlphaTexture;
            uniform int uShadowHasAlphaTexture;
            uniform float uShadowOpacity;
            void main()
            {
                float alpha = uShadowOpacity * (uShadowHasAlphaTexture != 0 ? texture(uShadowAlphaTexture, vUv).a : 1.0);
                float threshold = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
                if (alpha < threshold) discard;
            }
        )GLSL";

constexpr const char *ToneVertexShaderSource = R"GLSL(
            #version 330 core
            out vec2 vUv;
            void main()
            {
                vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                vUv = position;
                gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
            }
        )GLSL";

constexpr const char *SkyFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform mat4 uProjection;
            uniform mat4 uView;
            uniform sampler2D uEnvironmentSky;
            layout(location = 0) out vec4 outColor;
            layout(location = 1) out vec2 outVelocity;
            layout(location = 2) out vec4 outAmbient;
            vec2 DirectionToEquirect(vec3 direction)
            {
                direction = normalize(direction);
                return vec2(atan(direction.z, direction.x) / 6.2831853 + 0.5,
                            asin(clamp(direction.y, -1.0, 1.0)) / 3.14159265 + 0.5);
            }
            void main()
            {
                vec2 ndc = vUv * 2.0 - 1.0;
                vec3 viewDirection = normalize(vec3((ndc.x + uProjection[2][0]) / uProjection[0][0],
                                                     (ndc.y + uProjection[2][1]) / uProjection[1][1], -1.0));
                vec3 worldDirection = normalize(transpose(mat3(uView)) * viewDirection);
                outColor = vec4(texture(uEnvironmentSky, DirectionToEquirect(worldDirection)).rgb, 1.0);
                outVelocity = vec2(0.0);
                outAmbient = vec4(0.0);
            }
        )GLSL";

constexpr const char *HzbFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uSource;
            uniform vec2 uSourceTexel;
            out float outDepth;
            void main()
            {
                vec2 base = vUv - uSourceTexel * 0.5;
                float a = texture(uSource, base).r;
                float b = texture(uSource, base + vec2(uSourceTexel.x, 0.0)).r;
                float c = texture(uSource, base + vec2(0.0, uSourceTexel.y)).r;
                float d = texture(uSource, base + uSourceTexel).r;
                outDepth = max(max(a, b), max(c, d));
            }
        )GLSL";

constexpr const char *GtaoFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uDepth;
            uniform vec2 uInverseResolution;
            uniform mat4 uInverseProjection;
            out vec4 outOcclusion;
            vec3 ViewPosition(vec2 uv)
            {
                // 深度来自最近的像素，反投影必须使用那个像素的中心。
                vec2 size = vec2(textureSize(uDepth, 0));
                uv = (floor(uv * size) + 0.5) / size;
                vec4 position = uInverseProjection * vec4(uv * 2.0 - 1.0, texture(uDepth, uv).r * 2.0 - 1.0, 1.0);
                return position.xyz / position.w;
            }
            void main()
            {
                float center = texture(uDepth, vUv).r;
                if (center >= 0.99999) { outOcclusion = vec4(1.0, 0.5, 0.5, 1.0); return; }
                vec3 position = ViewPosition(vUv);
                vec3 left = position - ViewPosition(vUv - vec2(uInverseResolution.x, 0.0));
                vec3 right = ViewPosition(vUv + vec2(uInverseResolution.x, 0.0)) - position;
                vec3 down = position - ViewPosition(vUv - vec2(0.0, uInverseResolution.y));
                vec3 up = ViewPosition(vUv + vec2(0.0, uInverseResolution.y)) - position;
                vec3 dx = abs(left.z) < abs(right.z) ? left : right;
                vec3 dy = abs(down.z) < abs(up.z) ? down : up;
                vec3 normal = cross(dx, dy);
                if (dot(normal, normal) < 1e-12) { outOcclusion = vec4(1.0, 0.5, 0.5, 1.0); return; }
                normal = normalize(normal);
                if (dot(normal, -position) < 0.0) normal = -normal;
                const float radius = 0.3;
                vec2 sampleRadius = radius / (2.0 * max(-position.z, 0.001)) /
                                    vec2(uInverseProjection[0][0], uInverseProjection[1][1]);
                float horizon = 0.0;
                const int directions = 8;
                const int steps = 8;
                // 空间固定的旋转打散规则射线，移动相机时不会引入逐帧随机闪烁。
                float rotation = fract(sin(dot(floor(gl_FragCoord.xy), vec2(12.9898, 78.233))) * 43758.5453);
                for (int direction = 0; direction < directions; ++direction)
                {
                    float angle = 6.2831853 * (float(direction) + rotation) / float(directions);
                    vec2 ray = vec2(cos(angle), sin(angle));
                    float directionOcclusion = 0.0;
                    for (int stepIndex = 1; stepIndex <= steps; ++stepIndex)
                    {
                        vec2 uv = vUv + ray * sampleRadius * (float(stepIndex) / float(steps));
                        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || texture(uDepth, uv).r >= 0.99999) continue;
                        vec3 delta = ViewPosition(uv) - position;
                        float distance = length(delta);
                        if (distance < 0.001 || distance >= radius) continue;
                        // 同一平面上的邻点不形成遮挡，只有表面法线以上的近邻参与。
                        float elevation = max(0.0, dot(normal, delta) / distance - 0.03);
                        float falloff = 1.0 - distance * distance / (radius * radius);
                        // 对采样覆盖率积分，细小遮挡物不能仅凭一次命中就遮住整条射线。
                        directionOcclusion += elevation * falloff;
                    }
                    horizon += directionOcclusion / float(steps);
                }
                outOcclusion = vec4(clamp(1.0 - horizon / float(directions) * 2.0, 0.3, 1.0), normal * 0.5 + 0.5);
            }
        )GLSL";

// 用表面法线和平面距离过滤近邻，平滑采样噪声而不跨墙角、轮廓传播暗色。
constexpr const char *GtaoFilterFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uSource;
            uniform sampler2D uDepth;
            uniform mat4 uInverseProjection;
            out float outOcclusion;
            vec3 ViewPosition(vec2 uv)
            {
                vec2 size = vec2(textureSize(uDepth, 0));
                uv = (floor(uv * size) + 0.5) / size;
                vec4 position = uInverseProjection * vec4(uv * 2.0 - 1.0, texture(uDepth, uv).r * 2.0 - 1.0, 1.0);
                return position.xyz / position.w;
            }
            void main()
            {
                if (texture(uDepth, vUv).r >= 0.99999) { outOcclusion = 1.0; return; }
                ivec2 size = textureSize(uSource, 0);
                ivec2 center = clamp(ivec2(vUv * vec2(size)), ivec2(0), size - 1);
                vec4 sampleCenter = texelFetch(uSource, center, 0);
                vec3 normal = normalize(sampleCenter.gba * 2.0 - 1.0);
                vec3 position = ViewPosition((vec2(center) + 0.5) / vec2(size));
                float sum = 0.0, weights = 0.0;
                for (int y = -2; y <= 2; ++y)
                    for (int x = -2; x <= 2; ++x)
                    {
                        ivec2 pixel = clamp(center + ivec2(x, y), ivec2(0), size - 1);
                        vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
                        if (texture(uDepth, uv).r >= 0.99999) continue;
                        vec4 value = texelFetch(uSource, pixel, 0);
                        vec3 sampleNormal = normalize(value.gba * 2.0 - 1.0);
                        float planeDistance = abs(dot(normal, ViewPosition(uv) - position));
                        float weight = exp(-float(x*x + y*y) / 8.0) *
                                       pow(max(dot(normal, sampleNormal), 0.0), 16.0) * exp(-planeDistance / 0.01);
                        sum += value.r * weight;
                        weights += weight;
                    }
                outOcclusion = weights > 0.0001 ? sum / weights : sampleCenter.r;
            }
        )GLSL";

constexpr const char *BloomFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uSource;
            uniform vec2 uSourceTexel;
            uniform int uExtract;
            out vec4 outColor;
            void main()
            {
                vec3 color = vec3(0.0);
                float weights[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
                color += texture(uSource, vUv).rgb * weights[0];
                for (int offset = 1; offset < 5; ++offset)
                {
                    vec2 delta = uSourceTexel * float(offset);
                    color += (texture(uSource, vUv + delta).rgb + texture(uSource, vUv - delta).rgb) * weights[offset];
                }
                if (uExtract != 0)
                {
                    float brightness = max(color.r, max(color.g, color.b));
                    color *= smoothstep(0.8, 1.8, brightness);
                }
                outColor = vec4(color, 1.0);
            }
        )GLSL";

constexpr const char *TaaFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uScene;
            uniform sampler2D uAmbient;
            uniform sampler2D uVelocity;
            uniform sampler2D uHistory;
            uniform sampler2D uDepth;
            uniform sampler2D uHistoryDepth;
            uniform mat4 uReprojectDepth;
            uniform vec2 uDepthRange;
            uniform sampler2D uGtao;
            uniform sampler2D uBloom0;
            uniform sampler2D uBloom1;
            uniform sampler2D uBloom2;
            uniform sampler2D uBloom3;
            uniform sampler2D uBloom4;
            uniform vec2 uInverseResolution;
            uniform int uHistoryValid;
            uniform int uTaaEnabled;
            layout(location = 0) out vec4 outColor;
            layout(location = 1) out float outHistoryDepth;
            float LinearDepth(float depth)
            {
                return uDepthRange.x * uDepthRange.y /
                       max(uDepthRange.y - depth * (uDepthRange.y - uDepthRange.x), 0.00001);
            }
            float SampleOcclusion(vec2 uv)
            {
                vec2 depthSize = vec2(textureSize(uDepth, 0));
                uv = (clamp(floor(uv * depthSize), vec2(0.0), depthSize - 1.0) + 0.5) / depthSize;
                float centerDepth = texture(uDepth, uv).r;
                if (centerDepth >= 0.99999) return 1.0;
                vec2 stepUv = 1.0 / depthSize;
                float left = centerDepth - texture(uDepth, uv - vec2(stepUv.x, 0.0)).r;
                float right = texture(uDepth, uv + vec2(stepUv.x, 0.0)).r - centerDepth;
                float down = centerDepth - texture(uDepth, uv - vec2(0.0, stepUv.y)).r;
                float up = texture(uDepth, uv + vec2(0.0, stepUv.y)).r - centerDepth;
                // 两侧深度差异反号表示孤立前景，不能将背景拼成前景的斜面。
                vec2 gradient = vec2(left * right < 0.0 ? 0.0 : (abs(left) < abs(right) ? left : right),
                                     down * up < 0.0 ? 0.0 : (abs(down) < abs(up) ? down : up));
                vec2 size = vec2(textureSize(uGtao, 0));
                vec2 pixel = uv * size - 0.5;
                vec2 fraction = fract(pixel);
                ivec2 base = ivec2(floor(pixel));
                float sum = 0.0, weights = 0.0;
                for (int y = 0; y < 2; ++y)
                    for (int x = 0; x < 2; ++x)
                    {
                        ivec2 tap = clamp(base + ivec2(x, y), ivec2(0), ivec2(size) - 1);
                        vec2 tapUv = (vec2(tap) + 0.5) / size;
                        tapUv = (floor(tapUv * depthSize) + 0.5) / depthSize;
                        // 同一平面的原始深度在屏幕空间线性变化，不能将坡度当成物体边界。
                        float expectedDepth = centerDepth + dot(gradient, (tapUv - uv) * depthSize);
                        if (expectedDepth < 0.0 || expectedDepth >= 0.99999) continue;
                        float tapDepth = LinearDepth(texture(uDepth, tapUv).r);
                        float depth = LinearDepth(expectedDepth);
                        if (abs(tapDepth - depth) > max(0.01, depth * 0.001)) continue;
                        float weight = (x == 0 ? 1.0 - fraction.x : fraction.x) * (y == 0 ? 1.0 - fraction.y : fraction.y);
                        sum += texelFetch(uGtao, tap, 0).r * weight;
                        weights += weight;
                    }
                return weights > 0.0001 ? sum / weights : 1.0;
            }
            vec3 Composite(vec2 uv)
            {
                // AO 仅遮蔽间接光；直接光和自发光应保留原值。
                vec3 scene = texture(uScene, uv).rgb - texture(uAmbient, uv).rgb * (1.0 - SampleOcclusion(uv));
                vec3 bloom = texture(uBloom0, uv).rgb * 0.34 + texture(uBloom1, uv).rgb * 0.25 +
                             texture(uBloom2, uv).rgb * 0.18 + texture(uBloom3, uv).rgb * 0.13 +
                             texture(uBloom4, uv).rgb * 0.10;
                return scene + bloom;
            }
            vec2 PreviousDepthGradient(vec2 uv, float depth, vec4 previousClip)
            {
                float left = texture(uDepth, uv - vec2(uInverseResolution.x, 0.0)).r;
                float right = texture(uDepth, uv + vec2(uInverseResolution.x, 0.0)).r;
                float down = texture(uDepth, uv - vec2(0.0, uInverseResolution.y)).r;
                float up = texture(uDepth, uv + vec2(0.0, uInverseResolution.y)).r;
                vec2 offsetX = vec2(uInverseResolution.x, 0.0), offsetY = vec2(0.0, uInverseResolution.y);
                float depthX = right, depthY = up;
                if (abs(depth - left) < abs(right - depth)) { offsetX = -offsetX; depthX = left; }
                if (abs(depth - down) < abs(up - depth)) { offsetY = -offsetY; depthY = down; }
                // 两侧深度朝同一方向跳变时，这是孤立细节而非连续的坡面。
                if ((depth - left) * (right - depth) < 0.0 || depthX >= 0.999999) depthX = depth;
                if ((depth - down) * (up - depth) < 0.0 || depthY >= 0.999999) depthY = depth;
                vec4 clipX = uReprojectDepth * vec4((uv + offsetX) * 2.0 - 1.0, depthX * 2.0 - 1.0, 1.0);
                vec4 clipY = uReprojectDepth * vec4((uv + offsetY) * 2.0 - 1.0, depthY * 2.0 - 1.0, 1.0);
                if (clipX.w <= 0.0 || clipY.w <= 0.0) return vec2(0.0);
                vec3 origin = previousClip.xyz / previousClip.w;
                vec3 deltaX = (clipX.xyz / clipX.w - origin) * 0.5;
                vec3 deltaY = (clipY.xyz / clipY.w - origin) * 0.5;
                float determinant = deltaX.x * deltaY.y - deltaX.y * deltaY.x;
                if (abs(determinant) < 1e-12) return vec2(0.0);
                return vec2(deltaX.z * deltaY.y - deltaY.z * deltaX.y,
                            deltaX.x * deltaY.z - deltaY.x * deltaX.z) / determinant;
            }
            void main()
            {
                vec3 current = Composite(vUv);
                float depth = texture(uDepth, vUv).r;
                outHistoryDepth = depth;
                if (uTaaEnabled == 0 || uHistoryValid == 0)
                {
                    outColor = vec4(current, 1.0);
                    return;
                }
                vec2 velocity = texture(uVelocity, vUv).rg;
                vec2 historyUv = vUv - velocity;
                vec4 previousClip = uReprojectDepth * vec4(vUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
                if (any(lessThan(historyUv, vec2(0.0))) || any(greaterThan(historyUv, vec2(1.0))) ||
                    depth >= 0.999999 || previousClip.w <= 0.0)
                {
                    outColor = vec4(current, 1.0);
                    return;
                }
                float expectedDepth = previousClip.z / previousClip.w * 0.5 + 0.5;
                float expectedDistance = LinearDepth(expectedDepth);
                if (expectedDepth < 0.0 || expectedDepth > 1.0)
                {
                    outColor = vec4(current, 1.0);
                    return;
                }
                // 颜色和深度须来自同一组 texel。直接线性过滤颜色、最近点过滤深度
                // 会把豆子等小物体的历史颜色与地板混在一起，即使最近深度合法。
                ivec2 historySize = textureSize(uHistory, 0);
                vec2 historyPixel = historyUv * vec2(historySize) - 0.5;
                ivec2 historyBase = ivec2(floor(historyPixel));
                vec2 fraction = fract(historyPixel);
                vec3 history = vec3(0.0);
                float validWeight = 0.0;
                float depthTolerance = max(0.001, expectedDistance * 0.0005);
                vec2 depthGradient = PreviousDepthGradient(vUv, depth, previousClip);
                for (int y = 0; y < 2; ++y)
                    for (int x = 0; x < 2; ++x)
                    {
                        ivec2 pixel = clamp(historyBase + ivec2(x, y), ivec2(0), historySize - 1);
                        float previousDepth = texelFetch(uHistoryDepth, pixel, 0).r;
                        vec2 tapUv = (vec2(pixel) + 0.5) / vec2(historySize);
                        float tapDepth = expectedDepth + dot(depthGradient, tapUv - historyUv);
                        if (tapDepth < 0.0 || tapDepth > 1.0 || previousDepth >= 0.999999 ||
                            abs(LinearDepth(previousDepth) - LinearDepth(tapDepth)) > depthTolerance) continue;
                        float weight = (x == 0 ? 1.0 - fraction.x : fraction.x) * (y == 0 ? 1.0 - fraction.y : fraction.y);
                        history += texelFetch(uHistory, pixel, 0).rgb * weight;
                        validWeight += weight;
                    }
                if (validWeight <= 0.0001)
                {
                    outColor = vec4(current, 1.0);
                    return;
                }
                history /= validWeight;
                vec3 minimumColor = vec3(1e20), maximumColor = vec3(-1e20);
                for (int y = -1; y <= 1; ++y)
                    for (int x = -1; x <= 1; ++x)
                    {
                        vec3 sampleColor = Composite(vUv + vec2(x, y) * uInverseResolution);
                        minimumColor = min(minimumColor, sampleColor);
                        maximumColor = max(maximumColor, sampleColor);
                    }
                history = clamp(history, minimumColor, maximumColor);
                float historyWeight = mix(0.92, 0.72, clamp(length(velocity) * 120.0, 0.0, 1.0));
                outColor = vec4(mix(current, history, historyWeight), 1.0);
            }
        )GLSL";

constexpr const char *FinalToneFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uResolved;
            uniform float uExposure;
            uniform vec2 uInverseResolution;
            uniform int uFxaaEnabled;
            out vec4 outColor;
            vec3 AcesFilm(vec3 color)
            {
                return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
            }
            void main()
            {
                vec3 color = texture(uResolved, vUv).rgb;
                if (uFxaaEnabled != 0)
                {
                    vec3 nw = texture(uResolved, vUv + vec2(-1.0, -1.0) * uInverseResolution).rgb;
                    vec3 ne = texture(uResolved, vUv + vec2(1.0, -1.0) * uInverseResolution).rgb;
                    vec3 sw = texture(uResolved, vUv + vec2(-1.0, 1.0) * uInverseResolution).rgb;
                    vec3 se = texture(uResolved, vUv + vec2(1.0, 1.0) * uInverseResolution).rgb;
                    vec3 weights = vec3(0.299, 0.587, 0.114);
                    float center = dot(color, weights);
                    float minimumLuma = min(center, min(min(dot(nw, weights), dot(ne, weights)), min(dot(sw, weights), dot(se, weights))));
                    float maximumLuma = max(center, max(max(dot(nw, weights), dot(ne, weights)), max(dot(sw, weights), dot(se, weights))));
                    if (maximumLuma - minimumLuma > max(0.0312, maximumLuma * 0.125))
                    {
                        float lumaNw = dot(nw, weights), lumaNe = dot(ne, weights);
                        float lumaSw = dot(sw, weights), lumaSe = dot(se, weights);
                        // 沿边缘滤波，孤立细节的梯度为零，应保留中心像素。
                        vec2 direction = vec2(-((lumaNw + lumaNe) - (lumaSw + lumaSe)),
                                               (lumaNw + lumaSw) - (lumaNe + lumaSe));
                        float reduce = max((lumaNw + lumaNe + lumaSw + lumaSe) * 0.03125, 0.0078125);
                        direction /= min(abs(direction.x), abs(direction.y)) + reduce;
                        direction = clamp(direction, vec2(-8.0), vec2(8.0)) * uInverseResolution;
                        vec3 narrow = 0.5 * (texture(uResolved, vUv - direction / 6.0).rgb +
                                            texture(uResolved, vUv + direction / 6.0).rgb);
                        vec3 wide = narrow * 0.5 + 0.25 * (texture(uResolved, vUv - direction * 0.5).rgb +
                                                         texture(uResolved, vUv + direction * 0.5).rgb);
                        float wideLuma = dot(wide, weights);
                        color = wideLuma < minimumLuma || wideLuma > maximumLuma ? narrow : wide;
                    }
                }
                outColor = vec4(pow(AcesFilm(color * uExposure), vec3(1.0 / 2.2)), 1.0);
            }
        )GLSL";

constexpr const char *PresentFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uSceneColor;
            out vec4 outColor;
            void main() { outColor = texture(uSceneColor, vUv); }
        )GLSL";

constexpr const char *GpuCullComputeShaderSource = R"GLSL(
            #version 430 core
            layout(local_size_x = 64) in;
            struct CullInstance { mat4 model; mat4 previousModel; vec4 boundsMin; vec4 boundsMax; };
            layout(std430, binding = 0) readonly buffer CullInput { CullInstance instances[]; };
            layout(std430, binding = 1) writeonly buffer VisibleModels { mat4 visibleModels[]; };
            layout(std430, binding = 2) writeonly buffer VisiblePreviousModels { mat4 visiblePreviousModels[]; };
            layout(std430, binding = 3) buffer IndirectCommand
            {
                uint count;
                uint instanceCount;
                uint firstIndex;
                int baseVertex;
                uint baseInstance;
            } command;
            uniform mat4 uPreviousViewProjection;
            uniform uint uInputCount;
            uniform int uHzbValid;
            uniform int uHzbCount;
            uniform sampler2D uHzb[12];
            float SampleHzb(int level, vec2 uv)
            {
                return level == 0 ? texture(uHzb[0], uv).r : level == 1 ? texture(uHzb[1], uv).r :
                       level == 2 ? texture(uHzb[2], uv).r : level == 3 ? texture(uHzb[3], uv).r :
                       level == 4 ? texture(uHzb[4], uv).r : level == 5 ? texture(uHzb[5], uv).r :
                       level == 6 ? texture(uHzb[6], uv).r : level == 7 ? texture(uHzb[7], uv).r :
                       level == 8 ? texture(uHzb[8], uv).r : level == 9 ? texture(uHzb[9], uv).r :
                       level == 10 ? texture(uHzb[10], uv).r : texture(uHzb[11], uv).r;
            }
            vec2 HzbSize(int level)
            {
                return vec2(level == 0 ? textureSize(uHzb[0], 0) : level == 1 ? textureSize(uHzb[1], 0) :
                       level == 2 ? textureSize(uHzb[2], 0) : level == 3 ? textureSize(uHzb[3], 0) :
                       level == 4 ? textureSize(uHzb[4], 0) : level == 5 ? textureSize(uHzb[5], 0) :
                       level == 6 ? textureSize(uHzb[6], 0) : level == 7 ? textureSize(uHzb[7], 0) :
                       level == 8 ? textureSize(uHzb[8], 0) : level == 9 ? textureSize(uHzb[9], 0) :
                       level == 10 ? textureSize(uHzb[10], 0) : textureSize(uHzb[11], 0));
            }
            bool Visible(CullInstance instance)
            {
                // 相机移动、历史失效时必须保留 CPU 本帧判定可见的实例。
                if (uHzbValid == 0 || uHzbCount == 0) return true;
                float matrixDelta = 0.0;
                for (int column = 0; column < 4; ++column)
                    matrixDelta += dot(abs(instance.model[column] - instance.previousModel[column]), vec4(1.0));
                if (matrixDelta > 0.0001) return true;
                vec3 minimumNdc = vec3(1e20), maximumNdc = vec3(-1e20);
                for (int corner = 0; corner < 8; ++corner)
                {
                    vec3 position = vec3((corner & 1) != 0 ? instance.boundsMax.x : instance.boundsMin.x,
                                         (corner & 2) != 0 ? instance.boundsMax.y : instance.boundsMin.y,
                                         (corner & 4) != 0 ? instance.boundsMax.z : instance.boundsMin.z);
                    vec4 clip = uPreviousViewProjection * vec4(position, 1.0);
                    if (clip.w <= 0.0001) return true;
                    vec3 ndc = clip.xyz / clip.w;
                    minimumNdc = min(minimumNdc, ndc);
                    maximumNdc = max(maximumNdc, ndc);
                }
                // 跨越历史画面边界或近裁剪面的盒子不能用旧深度判死。
                if (any(lessThan(minimumNdc, vec3(-1.0))) || any(greaterThan(maximumNdc, vec3(1.0)))) return true;
                vec2 uvMin = minimumNdc.xy * 0.5 + 0.5;
                vec2 uvMax = maximumNdc.xy * 0.5 + 0.5;
                vec2 extentPixels = (uvMax - uvMin) * vec2(textureSize(uHzb[0], 0));
                int level = clamp(int(ceil(log2(max(1.0, max(extentPixels.x, extentPixels.y))))), 0, uHzbCount - 1);
                // 奇数尺寸向上取整，实际层级并非严格的 2^level 比例。
                // 只有矩形每轴最多跨两个 texel 时，四个角才能覆盖全部候选深度。
                for (;;)
                {
                    vec2 size = HzbSize(level);
                    vec2 first = floor(clamp(uvMin * size, vec2(0.0), size - 1.0));
                    vec2 last = floor(clamp(uvMax * size, vec2(0.0), size - 1.0));
                    if (all(lessThanEqual(last - first, vec2(1.0)))) break;
                    if (level + 1 >= uHzbCount) return true;
                    ++level;
                }
                float maximumDepth = max(max(SampleHzb(level, uvMin), SampleHzb(level, uvMax)),
                                         max(SampleHzb(level, vec2(uvMin.x, uvMax.y)), SampleHzb(level, vec2(uvMax.x, uvMin.y))));
                float nearestDepth = minimumNdc.z * 0.5 + 0.5;
                return nearestDepth <= maximumDepth + 0.002;
            }
            void main()
            {
                uint index = gl_GlobalInvocationID.x;
                if (index >= uInputCount) return;
                CullInstance instance = instances[index];
                if (!Visible(instance)) return;
                uint visibleIndex = atomicAdd(command.instanceCount, 1u);
                visibleModels[visibleIndex] = instance.model;
                visiblePreviousModels[visibleIndex] = instance.previousModel;
            }
        )GLSL";

constexpr const char *UiVertexShaderSource = R"GLSL(
            #version 330 core
            layout(location = 0) in vec2 aPosition;
            layout(location = 1) in vec2 aUv;
            uniform vec2 uViewport;
            out vec2 vUv;
            void main()
            {
                vec2 ndc = vec2(aPosition.x / uViewport.x * 2.0 - 1.0, 1.0 - aPosition.y / uViewport.y * 2.0);
                gl_Position = vec4(ndc, 0.0, 1.0); vUv = aUv;
            }
        )GLSL";

constexpr const char *UiFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform vec4 uColor;
            uniform sampler2D uUiTexture;
            uniform int uHasUiTexture;
            out vec4 outColor;
            void main() { outColor = uHasUiTexture != 0 ? texture(uUiTexture, vUv) * uColor : uColor; }
        )GLSL";

constexpr const char *ParticleVertexShaderSource = R"GLSL(
            #version 330 core
            layout(location=0) in vec4 aParticlePositionSize;
            layout(location=1) in vec4 aParticleColor;
            layout(location=2) in vec4 aParticleUvRect;
            uniform mat4 uParticleView;
            uniform mat4 uParticleProjection;
            out vec2 vParticleUv;
            out vec4 vParticleColor;
            void main()
            {
                vec2 corners[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
                vec2 corner = corners[gl_VertexID];
                mat3 cameraRotation = transpose(mat3(uParticleView));
                vec3 world = aParticlePositionSize.xyz + (cameraRotation[0] * corner.x + cameraRotation[1] * corner.y) * aParticlePositionSize.w;
                gl_Position = uParticleProjection * uParticleView * vec4(world, 1.0);
                vec2 localUv = corner * 0.5 + 0.5;
                vParticleUv = mix(aParticleUvRect.xy, aParticleUvRect.zw, localUv);
                vParticleColor = aParticleColor;
            }
        )GLSL";

constexpr const char *ParticleFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vParticleUv;
            in vec4 vParticleColor;
            uniform sampler2D uParticleTexture;
            uniform int uHasParticleTexture;
            layout(location = 0) out vec4 outColor;
            layout(location = 1) out vec2 outVelocity;
            layout(location = 2) out vec4 outAmbient;
            void main()
            {
                if (uHasParticleTexture != 0) outColor = texture(uParticleTexture, vParticleUv) * vParticleColor;
                else
                {
                    float distanceToCenter = length(vParticleUv - vec2(0.5));
                    if (distanceToCenter > 0.5) discard;
                    outColor = vec4(vParticleColor.rgb, vParticleColor.a * smoothstep(0.5, 0.35, distanceToCenter));
                }
                outVelocity = vec2(0.0);
                outAmbient = vec4(0.0, 0.0, 0.0, outColor.a);
            }
        )GLSL";

constexpr const char *GpuParticleVertexShaderSource = R"GLSL(
            #version 330 core
            uniform mat4 uParticleView;
            uniform mat4 uParticleProjection;
            uniform vec3 uEmitterPosition;
            uniform vec3 uEmitterGravity;
            uniform vec4 uEmitterStartColor;
            uniform vec4 uEmitterEndColor;
            uniform vec4 uEmitterParams;
            uniform vec4 uEmitterFlipbook;
            uniform float uEmitterTime;
            uniform float uEmitterCapacity;
            uniform float uEmitterFps;
            out vec2 vParticleUv;
            out vec4 vParticleColor;
            float hash(float value) { return fract(sin(value * 91.3458 + 17.17) * 47453.5453); }
            void main()
            {
                vec2 corners[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
                float rate = max(0.001, uEmitterParams.x);
                float life = max(0.001, uEmitterParams.y);
                float cycle = max(life, uEmitterCapacity / rate);
                float firstBirth = float(gl_InstanceID) / rate;
                float age = mod(max(0.0, uEmitterTime - firstBirth), cycle);
                bool active = uEmitterTime >= firstBirth && age < life;
                float seed = float(gl_InstanceID) + 1.0;
                vec3 direction = normalize(vec3((hash(seed) * 2.0 - 1.0) * uEmitterParams.w, 1.0,
                                                (hash(seed + 7.0) * 2.0 - 1.0) * uEmitterParams.w));
                vec3 center = uEmitterPosition + direction * uEmitterParams.z * age + uEmitterGravity * (0.5 * age * age);
                float alpha = clamp(age / life, 0.0, 1.0);
                float size = mix(uEmitterFlipbook.z, uEmitterFlipbook.w, alpha);
                mat3 cameraRotation = transpose(mat3(uParticleView));
                vec2 corner = corners[gl_VertexID];
                vec3 world = center + (cameraRotation[0] * corner.x + cameraRotation[1] * corner.y) * size;
                gl_Position = active ? uParticleProjection * uParticleView * vec4(world, 1.0) : vec4(2.0, 2.0, 2.0, 1.0);
                float frameCount = max(1.0, uEmitterFlipbook.x * uEmitterFlipbook.y);
                float rawFrame = floor(age * abs(uEmitterFps));
                float frame = uEmitterFps >= 0.0 ? mod(rawFrame, frameCount) : min(rawFrame, frameCount - 1.0);
                vec2 cell = vec2(mod(frame, uEmitterFlipbook.x), floor(frame / uEmitterFlipbook.x));
                vParticleUv = (cell + corner * 0.5 + 0.5) / uEmitterFlipbook.xy;
                vParticleColor = active ? mix(uEmitterStartColor, uEmitterEndColor, alpha) : vec4(0.0);
            }
        )GLSL";

constexpr const char *GpuParticleFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vParticleUv;
            in vec4 vParticleColor;
            uniform sampler2D uParticleTexture;
            uniform int uHasParticleTexture;
            layout(location = 0) out vec4 outColor;
            layout(location = 1) out vec2 outVelocity;
            layout(location = 2) out vec4 outAmbient;
            void main()
            {
                if (vParticleColor.a <= 0.0) discard;
                if (uHasParticleTexture != 0) outColor = texture(uParticleTexture, vParticleUv) * vParticleColor;
                else
                {
                    float distanceToCenter = length(vParticleUv - vec2(0.5));
                    if (distanceToCenter > 0.5) discard;
                    outColor = vec4(vParticleColor.rgb, vParticleColor.a * smoothstep(0.5, 0.35, distanceToCenter));
                }
                outVelocity = vec2(0.0);
                outAmbient = vec4(0.0, 0.0, 0.0, outColor.a);
            }
        )GLSL";
} // namespace

// OpenGL 细节隐藏在 PImpl 中，避免公开头文件暴露 SDL/OpenGL 类型并缩短上层重编译范围。
// 该对象与创建它的线程绑定；缓存中的 GLuint 只允许在上下文仍有效时销毁。
struct SdlGlRenderDevice::Impl
{
    using GenVertexArraysProc = void(APIENTRY *)(GLsizei, GLuint *);
    using BindVertexArrayProc = void(APIENTRY *)(GLuint);
    using DeleteVertexArraysProc = void(APIENTRY *)(GLsizei, const GLuint *);
    using GenBuffersProc = void(APIENTRY *)(GLsizei, GLuint *);
    using BindBufferProc = void(APIENTRY *)(GLenum, GLuint);
    using BufferDataProc = void(APIENTRY *)(GLenum, GLsizeiptr, const void *, GLenum);
    using DeleteBuffersProc = void(APIENTRY *)(GLsizei, const GLuint *);
    using VertexAttribPointerProc = void(APIENTRY *)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
    using EnableVertexAttribArrayProc = void(APIENTRY *)(GLuint);
    using VertexAttribDivisorProc = void(APIENTRY *)(GLuint, GLuint);
    using DrawElementsInstancedProc = void(APIENTRY *)(GLenum, GLsizei, GLenum, const void *, GLsizei);
    using DrawArraysInstancedProc = void(APIENTRY *)(GLenum, GLint, GLsizei, GLsizei);
    using DrawElementsIndirectProc = void(APIENTRY *)(GLenum, GLenum, const void *);
    using DispatchComputeProc = void(APIENTRY *)(GLuint, GLuint, GLuint);
    using BindBufferBaseProc = void(APIENTRY *)(GLenum, GLuint, GLuint);
    using MemoryBarrierProc = void(APIENTRY *)(GLbitfield);
    using CreateShaderProc = GLuint(APIENTRY *)(GLenum);
    using ShaderSourceProc = void(APIENTRY *)(GLuint, GLsizei, const GLchar *const *, const GLint *);
    using CompileShaderProc = void(APIENTRY *)(GLuint);
    using GetShaderivProc = void(APIENTRY *)(GLuint, GLenum, GLint *);
    using GetShaderInfoLogProc = void(APIENTRY *)(GLuint, GLsizei, GLsizei *, GLchar *);
    using DeleteShaderProc = void(APIENTRY *)(GLuint);
    using CreateProgramProc = GLuint(APIENTRY *)();
    using AttachShaderProc = void(APIENTRY *)(GLuint, GLuint);
    using LinkProgramProc = void(APIENTRY *)(GLuint);
    using GetProgramivProc = void(APIENTRY *)(GLuint, GLenum, GLint *);
    using GetProgramInfoLogProc = void(APIENTRY *)(GLuint, GLsizei, GLsizei *, GLchar *);
    using DeleteProgramProc = void(APIENTRY *)(GLuint);
    using UseProgramProc = void(APIENTRY *)(GLuint);
    using GetUniformLocationProc = GLint(APIENTRY *)(GLuint, const GLchar *);
    using UniformMatrix4fvProc = void(APIENTRY *)(GLint, GLsizei, GLboolean, const GLfloat *);
    using Uniform4fProc = void(APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    using Uniform3fProc = void(APIENTRY *)(GLint, GLfloat, GLfloat, GLfloat);
    using Uniform3iProc = void(APIENTRY *)(GLint, GLint, GLint, GLint);
    using Uniform2fProc = void(APIENTRY *)(GLint, GLfloat, GLfloat);
    using Uniform1fProc = void(APIENTRY *)(GLint, GLfloat);
    using Uniform1iProc = void(APIENTRY *)(GLint, GLint);
    using Uniform1uiProc = void(APIENTRY *)(GLint, GLuint);
    using ActiveTextureProc = void(APIENTRY *)(GLenum);
    using GenerateMipmapProc = void(APIENTRY *)(GLenum);
    using CompressedTexImage2DProc = void(APIENTRY *)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
    using GenFramebuffersProc = void(APIENTRY *)(GLsizei, GLuint *);
    using BindFramebufferProc = void(APIENTRY *)(GLenum, GLuint);
    using DeleteFramebuffersProc = void(APIENTRY *)(GLsizei, const GLuint *);
    using FramebufferTexture2DProc = void(APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
    using CheckFramebufferStatusProc = GLenum(APIENTRY *)(GLenum);
    using DrawBuffersProc = void(APIENTRY *)(GLsizei, const GLenum *);
    using TexBufferProc = void(APIENTRY *)(GLenum, GLenum, GLuint);
    using GenQueriesProc = void(APIENTRY *)(GLsizei, GLuint *);
    using DeleteQueriesProc = void(APIENTRY *)(GLsizei, const GLuint *);
    using BeginQueryProc = void(APIENTRY *)(GLenum, GLuint);
    using EndQueryProc = void(APIENTRY *)(GLenum);
    using GetQueryObjectivProc = void(APIENTRY *)(GLuint, GLenum, GLint *);
    using GetQueryObjectui64vProc = void(APIENTRY *)(GLuint, GLenum, GLuint64 *);

    SDL_Window *window = nullptr;
    SDL_GLContext context = nullptr;
    InputState input;
    FrameView view;
    std::unique_ptr<GlMaterialProgramCache> materialPrograms;
    GLuint program = 0, shadowProgram = 0, skyProgram = 0, toneProgram = 0, presentProgram = 0, hzbProgram = 0, gtaoProgram = 0,
           gtaoFilterProgram = 0, bloomProgram = 0, taaProgram = 0, gpuCullProgram = 0, uiProgram = 0, particleProgram = 0,
           gpuParticleProgram = 0, fullScreenVao = 0, particleInstanceBuffer = 0;
    GLuint uiVao = 0, uiVertexBuffer = 0, uiIndexBuffer = 0, fontAtlas = 0;
    struct Glyph
    {
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
        float advance = 0.62f;
    };
    std::unordered_map<std::uint32_t, Glyph> glyphs;
    int nextGlyph = 0;
#ifdef TINYENGINE_HAS_FREETYPE
    FT_Library freeType = nullptr;
    FT_Face freeTypeFace = nullptr;
#endif
    GLuint sceneFramebuffer = 0, toneFramebuffer = 0, postFramebuffer = 0, sceneHdrColor = 0, sceneColor = 0, sceneDepth = 0,
           sceneVelocity = 0, sceneAmbient = 0, historyHdrColor = 0, gtaoTexture = 0, gtaoRawTexture = 0;
    std::vector<GLuint> hzbTextures;
    std::array<GLuint, 5> bloomTextures{};
    std::array<GLuint, 2> taaTextures{};
    std::array<GLuint, 2> taaDepthTextures{};
    int temporalTextureIndex = 0;
    std::array<GLuint, FrameView::MaxShadowCascades> shadowFramebuffers{}, shadowDepths{};
    int shadowResolution = FrameView::ShadowMapResolution;
    std::size_t activeShadowCascade = 0;
    float exposure = 1.0f;
    bool fxaaEnabled = true;
    bool bloomEnabled = true, gtaoEnabled = true, taaEnabled = true, historyValid = false;
    bool indirectDrawSupported = false;
    bool computeCullingSupported = false;
    float maximumAnisotropy = 1.0f;
    GLuint timerQueries[2]{};
    bool timerIssued[2]{};
    int timerIndex = 0;
    bool timerActive = false;
    float lastGpuFrameMilliseconds = 0.0f;
    int sceneWidth = 0, sceneHeight = 0;
    bool sceneTargetEnabled = false;
    bool presentSceneToWindow = true;
    bool previousUiNext = false, previousUiAccept = false;
    SDL_GameController *controller = nullptr;
    GLint mvpLocation = -1, modelLocation = -1, previousModelLocation = -1, viewProjectionLocation = -1,
          previousViewProjectionLocation = -1, viewLocation = -1, instancedLocation = -1, baseColorLocation = -1;
    std::array<GLint, FrameView::MaxShadowCascades> shadowViewProjectionLocations{}, shadowCascadeSplitLocations{}, shadowMapLocations{};
    GLint bonesLocation = -1;
    GLint metallicLocation = -1, roughnessLocation = -1, emissiveLocation = -1, cameraPositionLocation = -1;
    GLint textureLocation = -1, hasTextureLocation = -1;
    GLint lightCountLocation = -1;
    GLint lightDataLocation = -1, clusterDataLocation = -1, clusterDimensionsLocation = -1, clusterDepthRangeLocation = -1,
          clusterViewportLocation = -1;
    GLuint lightDataBuffer = 0, lightDataTexture = 0, clusterDataBuffer = 0, clusterDataTexture = 0;
    GLint normalTextureLocation = -1, hasNormalTextureLocation = -1, metallicTextureLocation = -1, hasMetallicTextureLocation = -1;
    GLint roughnessTextureLocation = -1, hasRoughnessTextureLocation = -1, aoTextureLocation = -1, hasAoTextureLocation = -1;
    GLint emissiveTextureLocation = -1, hasEmissiveTextureLocation = -1;
    GLint packedTextureLocation = -1, hasPackedTextureLocation = -1, packedLayoutRmaLocation = -1;
    GLint environmentIrradianceLocation = -1, environmentPrefilterLocation = -1, environmentBrdfLocation = -1, hasEnvironmentLocation = -1;
    GLint shadowMvpLocation = -1, shadowLightViewProjectionLocation = -1, shadowInstancedLocation = -1, shadowAlphaTextureLocation = -1,
          shadowHasAlphaTextureLocation = -1, shadowOpacityLocation = -1;
    GLint skyProjectionLocation = -1, skyViewLocation = -1, skyEnvironmentLocation = -1;
    GLint exposureLocation = -1, inverseResolutionLocation = -1, fxaaEnabledLocation = -1;
    GLint hzbSourceLocation = -1, hzbSourceTexelLocation = -1;
    GLint gtaoDepthLocation = -1, gtaoInverseResolutionLocation = -1, gtaoInverseProjectionLocation = -1;
    GLint gtaoFilterSourceLocation = -1, gtaoFilterDepthLocation = -1, gtaoFilterInverseProjectionLocation = -1;
    GLint bloomSourceLocation = -1, bloomSourceTexelLocation = -1, bloomExtractLocation = -1;
    GLint taaSceneLocation = -1, taaVelocityLocation = -1, taaHistoryLocation = -1, taaGtaoLocation = -1, taaInverseResolutionLocation = -1,
          taaHistoryValidLocation = -1, taaEnabledLocation = -1;
    GLint taaDepthLocation = -1, taaHistoryDepthLocation = -1, taaReprojectDepthLocation = -1, taaDepthRangeLocation = -1;
    GLint taaAmbientLocation = -1;
    std::array<GLint, 5> taaBloomLocations{};
    GLint toneResolvedLocation = -1;
    GLint presentSceneColorLocation = -1;
    GLint gpuCullPreviousViewProjectionLocation = -1, gpuCullInputCountLocation = -1, gpuCullHzbValidLocation = -1,
          gpuCullHzbCountLocation = -1;
    std::array<GLint, 12> gpuCullHzbLocations{};
    GLint uiViewportLocation = -1, uiColorLocation = -1, uiTextureLocation = -1, uiHasTextureLocation = -1;
    GLint particleViewLocation = -1, particleProjectionLocation = -1, particlePositionLocation = -1, particleSizeLocation = -1,
          particleColorLocation = -1, particleTextureLocation = -1, particleHasTextureLocation = -1;
    GLint gpuParticleViewLocation = -1, gpuParticleProjectionLocation = -1, gpuEmitterPositionLocation = -1, gpuEmitterGravityLocation = -1,
          gpuEmitterStartColorLocation = -1, gpuEmitterEndColorLocation = -1, gpuEmitterParamsLocation = -1,
          gpuEmitterFlipbookLocation = -1, gpuEmitterTimeLocation = -1, gpuEmitterCapacityLocation = -1, gpuEmitterFpsLocation = -1,
          gpuParticleTextureLocation = -1, gpuParticleHasTextureLocation = -1;
    FileWatcher vertexShaderWatcher, fragmentShaderWatcher;
    std::unordered_map<std::string, FileWatcher> shaderDependencyWatchers;
    std::filesystem::path vertexShaderPath{"assets/shaders/default.vert"}, fragmentShaderPath{"assets/shaders/default.frag"};
    std::string lastShaderError;

    GenVertexArraysProc GenVertexArrays = nullptr;
    BindVertexArrayProc BindVertexArray = nullptr;
    DeleteVertexArraysProc DeleteVertexArrays = nullptr;
    GenBuffersProc GenBuffers = nullptr;
    BindBufferProc BindBuffer = nullptr;
    BufferDataProc BufferData = nullptr;
    DeleteBuffersProc DeleteBuffers = nullptr;
    VertexAttribPointerProc VertexAttribPointer = nullptr;
    EnableVertexAttribArrayProc EnableVertexAttribArray = nullptr;
    VertexAttribDivisorProc VertexAttribDivisor = nullptr;
    DrawElementsInstancedProc DrawElementsInstanced = nullptr;
    DrawArraysInstancedProc DrawArraysInstanced = nullptr;
    DrawElementsIndirectProc DrawElementsIndirect = nullptr;
    DispatchComputeProc DispatchCompute = nullptr;
    BindBufferBaseProc BindBufferBase = nullptr;
    MemoryBarrierProc MemoryBarrier = nullptr;
    CreateShaderProc CreateShader = nullptr;
    ShaderSourceProc ShaderSource = nullptr;
    CompileShaderProc CompileShader = nullptr;
    GetShaderivProc GetShaderiv = nullptr;
    GetShaderInfoLogProc GetShaderInfoLog = nullptr;
    DeleteShaderProc DeleteShader = nullptr;
    CreateProgramProc CreateProgram = nullptr;
    AttachShaderProc AttachShader = nullptr;
    LinkProgramProc LinkProgram = nullptr;
    GetProgramivProc GetProgramiv = nullptr;
    GetProgramInfoLogProc GetProgramInfoLog = nullptr;
    DeleteProgramProc DeleteProgram = nullptr;
    UseProgramProc UseProgram = nullptr;
    GetUniformLocationProc GetUniformLocation = nullptr;
    UniformMatrix4fvProc UniformMatrix4fv = nullptr;
    Uniform4fProc Uniform4f = nullptr;
    Uniform3fProc Uniform3f = nullptr;
    Uniform3iProc Uniform3i = nullptr;
    Uniform2fProc Uniform2f = nullptr;
    Uniform1fProc Uniform1f = nullptr;
    Uniform1iProc Uniform1i = nullptr;
    Uniform1uiProc Uniform1ui = nullptr;
    ActiveTextureProc ActiveTexture = nullptr;
    GenerateMipmapProc GenerateMipmap = nullptr;
    CompressedTexImage2DProc CompressedTexImage2D = nullptr;
    GenFramebuffersProc GenFramebuffers = nullptr;
    BindFramebufferProc BindFramebuffer = nullptr;
    DeleteFramebuffersProc DeleteFramebuffers = nullptr;
    FramebufferTexture2DProc FramebufferTexture2D = nullptr;
    CheckFramebufferStatusProc CheckFramebufferStatus = nullptr;
    DrawBuffersProc DrawBuffers = nullptr;
    TexBufferProc TexBuffer = nullptr;
    GenQueriesProc GenQueries = nullptr;
    DeleteQueriesProc DeleteQueries = nullptr;
    BeginQueryProc BeginQuery = nullptr;
    EndQueryProc EndQuery = nullptr;
    GetQueryObjectivProc GetQueryObjectiv = nullptr;
    GetQueryObjectui64vProc GetQueryObjectui64v = nullptr;

    struct GpuMesh
    {
        GLuint vao = 0, vertexBuffer = 0, indexBuffer = 0, instanceBuffer = 0, previousInstanceBuffer = 0, indirectBuffer = 0,
               cullInputBuffer = 0;
        GLsizei indexCount = 0;
        std::size_t byteSize = 0;
    };
    struct UiVertex
    {
        Vec2 position;
        Vec2 uv;
    };
    std::unordered_map<std::string, GpuMesh> meshes;
    std::unordered_map<std::string, GLuint> textures;
    std::unordered_map<std::string, std::size_t> textureBytes;
    std::unordered_map<std::string, std::shared_ptr<TextureResource>> uiTextureResources;
    std::size_t gpuMemoryBudget = 512ull * 1024ull * 1024ull;

    std::size_t GetResidentBytes() const
    {
        std::size_t total = 0;
        for (const auto &pair : meshes)
            total += pair.second.byteSize;
        for (const auto &pair : textureBytes)
            total += pair.second;
        return total;
    }

    void DestroyCachedResources()
    {
        for (const auto &pair : meshes)
        {
            DeleteBuffers(1, &pair.second.vertexBuffer);
            DeleteBuffers(1, &pair.second.indexBuffer);
            if (pair.second.instanceBuffer)
                DeleteBuffers(1, &pair.second.instanceBuffer);
            if (pair.second.previousInstanceBuffer)
                DeleteBuffers(1, &pair.second.previousInstanceBuffer);
            if (pair.second.indirectBuffer)
                DeleteBuffers(1, &pair.second.indirectBuffer);
            if (pair.second.cullInputBuffer)
                DeleteBuffers(1, &pair.second.cullInputBuffer);
            DeleteVertexArrays(1, &pair.second.vao);
        }
        meshes.clear();
        for (const auto &pair : textures)
            glDeleteTextures(1, &pair.second);
        textures.clear();
        textureBytes.clear();
        uiTextureResources.clear();
    }

    bool LoadFunctions()
    {
        const bool required =
            LoadGl(GenVertexArrays, "glGenVertexArrays") && LoadGl(BindVertexArray, "glBindVertexArray") &&
            LoadGl(DeleteVertexArrays, "glDeleteVertexArrays") && LoadGl(GenBuffers, "glGenBuffers") &&
            LoadGl(BindBuffer, "glBindBuffer") && LoadGl(BufferData, "glBufferData") && LoadGl(DeleteBuffers, "glDeleteBuffers") &&
            LoadGl(VertexAttribPointer, "glVertexAttribPointer") && LoadGl(EnableVertexAttribArray, "glEnableVertexAttribArray") &&
            LoadGl(VertexAttribDivisor, "glVertexAttribDivisor") && LoadGl(DrawElementsInstanced, "glDrawElementsInstanced") &&
            LoadGl(DrawArraysInstanced, "glDrawArraysInstanced") && LoadGl(CreateShader, "glCreateShader") &&
            LoadGl(ShaderSource, "glShaderSource") && LoadGl(CompileShader, "glCompileShader") && LoadGl(GetShaderiv, "glGetShaderiv") &&
            LoadGl(GetShaderInfoLog, "glGetShaderInfoLog") && LoadGl(DeleteShader, "glDeleteShader") &&
            LoadGl(CreateProgram, "glCreateProgram") && LoadGl(AttachShader, "glAttachShader") && LoadGl(LinkProgram, "glLinkProgram") &&
            LoadGl(GetProgramiv, "glGetProgramiv") && LoadGl(GetProgramInfoLog, "glGetProgramInfoLog") &&
            LoadGl(DeleteProgram, "glDeleteProgram") && LoadGl(UseProgram, "glUseProgram") &&
            LoadGl(GetUniformLocation, "glGetUniformLocation") && LoadGl(UniformMatrix4fv, "glUniformMatrix4fv") &&
            LoadGl(Uniform4f, "glUniform4f") && LoadGl(Uniform3f, "glUniform3f") && LoadGl(Uniform2f, "glUniform2f") &&
            LoadGl(Uniform3i, "glUniform3i") && LoadGl(Uniform1f, "glUniform1f") && LoadGl(Uniform1i, "glUniform1i") &&
            LoadGl(ActiveTexture, "glActiveTexture") && LoadGl(GenerateMipmap, "glGenerateMipmap") &&
            LoadGl(CompressedTexImage2D, "glCompressedTexImage2D") && LoadGl(GenFramebuffers, "glGenFramebuffers") &&
            LoadGl(BindFramebuffer, "glBindFramebuffer") && LoadGl(DeleteFramebuffers, "glDeleteFramebuffers") &&
            LoadGl(FramebufferTexture2D, "glFramebufferTexture2D") && LoadGl(CheckFramebufferStatus, "glCheckFramebufferStatus") &&
            LoadGl(DrawBuffers, "glDrawBuffers") && LoadGl(TexBuffer, "glTexBuffer") && LoadGl(GenQueries, "glGenQueries") &&
            LoadGl(DeleteQueries, "glDeleteQueries") && LoadGl(BeginQuery, "glBeginQuery") && LoadGl(EndQuery, "glEndQuery") &&
            LoadGl(GetQueryObjectiv, "glGetQueryObjectiv") && LoadGl(GetQueryObjectui64v, "glGetQueryObjectui64v");
        indirectDrawSupported = LoadGl(DrawElementsIndirect, "glDrawElementsIndirect");
        computeCullingSupported = indirectDrawSupported && LoadGl(DispatchCompute, "glDispatchCompute") &&
                                  LoadGl(BindBufferBase, "glBindBufferBase") && LoadGl(MemoryBarrier, "glMemoryBarrier") &&
                                  LoadGl(Uniform1ui, "glUniform1ui");
        return required;
    }

    void DestroySceneTarget()
    {
        if (sceneHdrColor)
            glDeleteTextures(1, &sceneHdrColor);
        if (sceneAmbient)
            glDeleteTextures(1, &sceneAmbient);
        if (sceneColor)
            glDeleteTextures(1, &sceneColor);
        if (sceneDepth)
            glDeleteTextures(1, &sceneDepth);
        if (sceneVelocity)
            glDeleteTextures(1, &sceneVelocity);
        if (historyHdrColor)
            glDeleteTextures(1, &historyHdrColor);
        if (gtaoTexture)
            glDeleteTextures(1, &gtaoTexture);
        if (gtaoRawTexture)
            glDeleteTextures(1, &gtaoRawTexture);
        for (const GLuint texture : hzbTextures)
            if (texture)
                glDeleteTextures(1, &texture);
        hzbTextures.clear();
        for (GLuint &texture : bloomTextures)
        {
            if (texture)
                glDeleteTextures(1, &texture);
            texture = 0;
        }
        for (GLuint &texture : taaTextures)
        {
            if (texture)
                glDeleteTextures(1, &texture);
            texture = 0;
        }
        for (GLuint &texture : taaDepthTextures)
        {
            if (texture)
                glDeleteTextures(1, &texture);
            texture = 0;
        }
        if (sceneFramebuffer)
            DeleteFramebuffers(1, &sceneFramebuffer);
        if (toneFramebuffer)
            DeleteFramebuffers(1, &toneFramebuffer);
        if (postFramebuffer)
            DeleteFramebuffers(1, &postFramebuffer);
        sceneHdrColor = sceneColor = sceneDepth = sceneVelocity = sceneAmbient = historyHdrColor = gtaoTexture = gtaoRawTexture =
            sceneFramebuffer = toneFramebuffer = postFramebuffer = 0;
        historyValid = false;
        temporalTextureIndex = 0;
        sceneWidth = sceneHeight = 0;
    }

    bool CreateSceneTarget(const int width, const int height)
    {
        if (width < 1 || height < 1)
            return false;
        DestroySceneTarget();
        GenFramebuffers(1, &sceneFramebuffer);
        BindFramebuffer(GL_FRAMEBUFFER, sceneFramebuffer);
        glGenTextures(1, &sceneHdrColor);
        glBindTexture(GL_TEXTURE_2D, sceneHdrColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneHdrColor, 0);
        glGenTextures(1, &sceneDepth);
        glBindTexture(GL_TEXTURE_2D, sceneDepth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepth, 0);
        glGenTextures(1, &sceneVelocity);
        glBindTexture(GL_TEXTURE_2D, sceneVelocity);
        glTexImage2D(GL_TEXTURE_2D, 0, 0x822F, width, height, 0, 0x8227, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        FramebufferTexture2D(GL_FRAMEBUFFER, 0x8CE1, GL_TEXTURE_2D, sceneVelocity, 0);
        glGenTextures(1, &sceneAmbient);
        glBindTexture(GL_TEXTURE_2D, sceneAmbient);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        FramebufferTexture2D(GL_FRAMEBUFFER, 0x8CE2, GL_TEXTURE_2D, sceneAmbient, 0);
        const GLenum sceneAttachments[] = {GL_COLOR_ATTACHMENT0, 0x8CE1, 0x8CE2};
        DrawBuffers(3, sceneAttachments);
        bool complete = CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        GenFramebuffers(1, &toneFramebuffer);
        BindFramebuffer(GL_FRAMEBUFFER, toneFramebuffer);
        glGenTextures(1, &sceneColor);
        glBindTexture(GL_TEXTURE_2D, sceneColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);
        complete = complete && CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        GenFramebuffers(1, &postFramebuffer);
        const auto createTexture = [](GLuint &texture, const GLint internalFormat, const int textureWidth, const int textureHeight,
                                      const GLenum format, const GLenum type, const GLint filter) {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, textureWidth, textureHeight, 0, format, type, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        };
        int hzbWidth = std::max(1, (width + 1) / 2), hzbHeight = std::max(1, (height + 1) / 2);
        while (true)
        {
            GLuint texture = 0;
            createTexture(texture, 0x822E, hzbWidth, hzbHeight, GL_RED, GL_FLOAT, GL_NEAREST);
            hzbTextures.push_back(texture);
            if (hzbWidth == 1 && hzbHeight == 1)
                break;
            hzbWidth = std::max(1, (hzbWidth + 1) / 2);
            hzbHeight = std::max(1, (hzbHeight + 1) / 2);
        }
        createTexture(gtaoTexture, 0x822D, std::max(1, width / 2), std::max(1, height / 2), GL_RED, GL_FLOAT, GL_LINEAR);
        createTexture(gtaoRawTexture, GL_RGBA16F, std::max(1, width / 2), std::max(1, height / 2), GL_RGBA, GL_FLOAT, GL_NEAREST);
        int bloomWidth = std::max(1, width / 2), bloomHeight = std::max(1, height / 2);
        for (GLuint &texture : bloomTextures)
        {
            createTexture(texture, GL_RGBA16F, bloomWidth, bloomHeight, GL_RGBA, GL_FLOAT, GL_LINEAR);
            bloomWidth = std::max(1, bloomWidth / 2);
            bloomHeight = std::max(1, bloomHeight / 2);
        }
        for (GLuint &texture : taaTextures)
            createTexture(texture, GL_RGBA16F, width, height, GL_RGBA, GL_FLOAT, GL_LINEAR);
        for (GLuint &texture : taaDepthTextures)
            createTexture(texture, 0x822E, width, height, GL_RED, GL_FLOAT, GL_NEAREST);
        BindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (!complete)
        {
            DestroySceneTarget();
            return false;
        }
        sceneWidth = width;
        sceneHeight = height;
        return true;
    }

    void DestroyShadowTarget()
    {
        for (std::size_t index = 0; index < FrameView::MaxShadowCascades; ++index)
        {
            if (shadowDepths[index])
                glDeleteTextures(1, &shadowDepths[index]);
            if (shadowFramebuffers[index])
                DeleteFramebuffers(1, &shadowFramebuffers[index]);
            shadowDepths[index] = shadowFramebuffers[index] = 0;
        }
    }

    bool CreateShadowTarget()
    {
        DestroyShadowTarget();
        bool complete = true;
        for (std::size_t index = 0; index < FrameView::MaxShadowCascades; ++index)
        {
            GenFramebuffers(1, &shadowFramebuffers[index]);
            BindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffers[index]);
            glGenTextures(1, &shadowDepths[index]);
            glBindTexture(GL_TEXTURE_2D, shadowDepths[index]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowResolution, shadowResolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                         nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            const GLfloat border[] = {1.0f, 1.0f, 1.0f, 1.0f};
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
            FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepths[index], 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            complete = complete && CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        }
        BindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (!complete)
            DestroyShadowTarget();
        return complete;
    }

    GLuint Compile(const GLenum type, const char *source)
    {
        const GLuint shader = CreateShader(type);
        ShaderSource(shader, 1, &source, nullptr);
        CompileShader(shader);
        GLint success = GL_FALSE;
        GetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success == GL_TRUE)
            return shader;
        GLchar log[4096]{};
        GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        lastShaderError = log;
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Shader compilation failed: %s", log);
        DeleteShader(shader);
        return 0;
    }

    GLuint CreateLinkedProgram(const char *vertexSource, const char *fragmentSource)
    {
        const GLuint vertex = Compile(GL_VERTEX_SHADER, vertexSource);
        const GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource);
        if (!vertex || !fragment)
        {
            if (vertex)
                DeleteShader(vertex);
            if (fragment)
                DeleteShader(fragment);
            return 0;
        }
        const GLuint candidate = CreateProgram();
        AttachShader(candidate, vertex);
        AttachShader(candidate, fragment);
        LinkProgram(candidate);
        DeleteShader(vertex);
        DeleteShader(fragment);
        GLint success = GL_FALSE;
        GetProgramiv(candidate, GL_LINK_STATUS, &success);
        if (success != GL_TRUE)
        {
            GLchar log[4096]{};
            GetProgramInfoLog(candidate, sizeof(log), nullptr, log);
            lastShaderError = log;
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Program link failed: %s", log);
            DeleteProgram(candidate);
            return 0;
        }
        return candidate;
    }

    GLuint CreateComputeProgram(const char *source)
    {
        const GLuint shader = Compile(0x91B9, source);
        if (!shader)
            return 0;
        const GLuint candidate = CreateProgram();
        AttachShader(candidate, shader);
        LinkProgram(candidate);
        DeleteShader(shader);
        GLint success = GL_FALSE;
        GetProgramiv(candidate, GL_LINK_STATUS, &success);
        if (success == GL_TRUE)
            return candidate;
        GLchar log[4096]{};
        GetProgramInfoLog(candidate, sizeof(log), nullptr, log);
        lastShaderError = log;
        DeleteProgram(candidate);
        return 0;
    }

    bool CreateDefaultProgram(const char *vertexSource = VertexShaderSource, const char *fragmentSource = FragmentShaderSource)
    {
        const GLuint candidate = CreateLinkedProgram(vertexSource, fragmentSource);
        if (!candidate)
            return false;
        if (program)
            DeleteProgram(program);
        program = candidate;
        lastShaderError.clear();
        mvpLocation = GetUniformLocation(program, "uMvp");
        bonesLocation = GetUniformLocation(program, "uBones");
        modelLocation = GetUniformLocation(program, "uModel");
        previousModelLocation = GetUniformLocation(program, "uPreviousModel");
        viewProjectionLocation = GetUniformLocation(program, "uViewProjection");
        previousViewProjectionLocation = GetUniformLocation(program, "uPreviousViewProjection");
        viewLocation = GetUniformLocation(program, "uView");
        instancedLocation = GetUniformLocation(program, "uInstanced");
        for (std::size_t index = 0; index < FrameView::MaxShadowCascades; ++index)
        {
            const std::string suffix = "[" + std::to_string(index) + "]";
            shadowViewProjectionLocations[index] = GetUniformLocation(program, ("uShadowViewProjections" + suffix).c_str());
            shadowCascadeSplitLocations[index] = GetUniformLocation(program, ("uShadowCascadeSplits" + suffix).c_str());
            shadowMapLocations[index] = GetUniformLocation(program, ("uShadowMaps" + suffix).c_str());
        }
        baseColorLocation = GetUniformLocation(program, "uBaseColor");
        lightCountLocation = GetUniformLocation(program, "uLightCount");
        lightDataLocation = GetUniformLocation(program, "uLightData");
        clusterDataLocation = GetUniformLocation(program, "uClusterData");
        clusterDimensionsLocation = GetUniformLocation(program, "uClusterDimensions");
        clusterDepthRangeLocation = GetUniformLocation(program, "uClusterDepthRange");
        clusterViewportLocation = GetUniformLocation(program, "uViewportSize");
        metallicLocation = GetUniformLocation(program, "uMetallic");
        roughnessLocation = GetUniformLocation(program, "uRoughness");
        emissiveLocation = GetUniformLocation(program, "uEmissive");
        cameraPositionLocation = GetUniformLocation(program, "uCameraPosition");
        textureLocation = GetUniformLocation(program, "uTexture");
        hasTextureLocation = GetUniformLocation(program, "uHasTexture");
        normalTextureLocation = GetUniformLocation(program, "uNormalTexture");
        hasNormalTextureLocation = GetUniformLocation(program, "uHasNormalTexture");
        metallicTextureLocation = GetUniformLocation(program, "uMetallicTexture");
        hasMetallicTextureLocation = GetUniformLocation(program, "uHasMetallicTexture");
        roughnessTextureLocation = GetUniformLocation(program, "uRoughnessTexture");
        hasRoughnessTextureLocation = GetUniformLocation(program, "uHasRoughnessTexture");
        aoTextureLocation = GetUniformLocation(program, "uAoTexture");
        hasAoTextureLocation = GetUniformLocation(program, "uHasAoTexture");
        emissiveTextureLocation = GetUniformLocation(program, "uEmissiveTexture");
        hasEmissiveTextureLocation = GetUniformLocation(program, "uHasEmissiveTexture");
        packedTextureLocation = GetUniformLocation(program, "uPackedTexture");
        hasPackedTextureLocation = GetUniformLocation(program, "uHasPackedTexture");
        packedLayoutRmaLocation = GetUniformLocation(program, "uPackedLayoutRma");
        environmentIrradianceLocation = GetUniformLocation(program, "uEnvironmentIrradiance");
        environmentPrefilterLocation = GetUniformLocation(program, "uEnvironmentPrefilter");
        environmentBrdfLocation = GetUniformLocation(program, "uEnvironmentBrdfLut");
        hasEnvironmentLocation = GetUniformLocation(program, "uHasEnvironment");
        return true;
    }

    bool CreateFontAtlas()
    {
        constexpr int width = 1024, height = 1024;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
#ifdef TINYENGINE_HAS_FREETYPE
        if (FT_Init_FreeType(&freeType) != 0)
            return false;
        const char *configured = std::getenv("TINYENGINE_FONT_PATH");
        const char *candidates[] = {configured, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/Library/Fonts/Arial Unicode.ttf"};
        for (const char *candidate : candidates)
            if (candidate && FT_New_Face(freeType, candidate, 0, &freeTypeFace) == 0)
                break;
        if (!freeTypeFace)
            return false;
        FT_Set_Pixel_Sizes(freeTypeFace, 0, 36);
#endif
        glGenTextures(1, &fontAtlas);
        glBindTexture(GL_TEXTURE_2D, fontAtlas);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return fontAtlas != 0;
    }

    const Glyph &GetGlyph(const std::uint32_t requestedCodepoint)
    {
        auto found = glyphs.find(requestedCodepoint);
        if (found != glyphs.end())
            return found->second;
        constexpr int atlasSize = 1024, cell = 48, columns = atlasSize / cell;
        std::uint32_t codepoint = requestedCodepoint;
        if (nextGlyph >= columns * columns)
            codepoint = '?';
        if (const auto fallback = glyphs.find(codepoint); fallback != glyphs.end())
            return fallback->second;
        const int slot = nextGlyph++, cellX = slot % columns * cell, cellY = slot / columns * cell;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(cell) * cell * 4, 255);
        for (std::size_t index = 0; index < static_cast<std::size_t>(cell) * cell; ++index)
            pixels[index * 4 + 3] = 0;
        float advance = 0.62f;
#ifdef _WIN32
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = cell;
        info.bmiHeader.biHeight = -cell;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void *bitmapPixels = nullptr;
        HDC device = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = device ? CreateDIBSection(device, &info, DIB_RGB_COLORS, &bitmapPixels, nullptr, 0) : nullptr;
        HFONT font = CreateFontW(-36, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        if (device && bitmap && bitmapPixels && font)
        {
            const HGDIOBJ oldBitmap = SelectObject(device, bitmap), oldFont = SelectObject(device, font);
            std::fill_n(static_cast<std::uint8_t *>(bitmapPixels), static_cast<std::size_t>(cell) * cell * 4, static_cast<std::uint8_t>(0));
            SetBkColor(device, RGB(0, 0, 0));
            SetTextColor(device, RGB(255, 255, 255));
            SetBkMode(device, OPAQUE);
            wchar_t utf16[2]{};
            int count = 1;
            if (codepoint <= 0xFFFF)
                utf16[0] = static_cast<wchar_t>(codepoint);
            else
            {
                const std::uint32_t value = codepoint - 0x10000;
                utf16[0] = static_cast<wchar_t>(0xD800 + (value >> 10u));
                utf16[1] = static_cast<wchar_t>(0xDC00 + (value & 0x3FFu));
                count = 2;
            }
            TextOutW(device, 3, 3, utf16, count);
            SIZE extent{};
            GetTextExtentPoint32W(device, utf16, count, &extent);
            advance = std::max(0.2f, static_cast<float>(extent.cx) / 36.0f);
            const auto *source = static_cast<const std::uint8_t *>(bitmapPixels);
            for (std::size_t index = 0; index < static_cast<std::size_t>(cell) * cell; ++index)
                pixels[index * 4 + 3] = std::max(source[index * 4], std::max(source[index * 4 + 1], source[index * 4 + 2]));
            SelectObject(device, oldFont);
            SelectObject(device, oldBitmap);
        }
        if (font)
            DeleteObject(font);
        if (bitmap)
            DeleteObject(bitmap);
        if (device)
            DeleteDC(device);
#elif defined(TINYENGINE_HAS_FREETYPE)
        if (freeTypeFace && FT_Load_Char(freeTypeFace, codepoint, FT_LOAD_RENDER) == 0)
        {
            const FT_GlyphSlot glyph = freeTypeFace->glyph;
            advance = std::max(0.2f, static_cast<float>(glyph->advance.x) / (64.0f * 36.0f));
            for (unsigned int y = 0; y < glyph->bitmap.rows && y + 5 < static_cast<unsigned int>(cell); ++y)
                for (unsigned int x = 0; x < glyph->bitmap.width && x + 3 < static_cast<unsigned int>(cell); ++x)
                    pixels[((y + 5) * cell + x + 3) * 4 + 3] = glyph->bitmap.buffer[y * glyph->bitmap.pitch + x];
        }
#endif
        glBindTexture(GL_TEXTURE_2D, fontAtlas);
        glTexSubImage2D(GL_TEXTURE_2D, 0, cellX, cellY, cell, cell, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        Glyph glyph{(cellX + 0.5f) / atlasSize, (cellY + 0.5f) / atlasSize, (cellX + cell - 0.5f) / atlasSize,
                    (cellY + cell - 0.5f) / atlasSize, advance};
        return glyphs.emplace(codepoint, glyph).first->second;
    }

    std::shared_ptr<const MaterialRenderProxy> errorMaterial;
    bool BindCompiledMaterial(const RenderItem &item, MaterialPass pass, MaterialGeometry geometry)
    {
        materialPrograms->SetFrameBindings(shadowDepths,lightDataTexture,clusterDataTexture,activeShadowCascade,
            {static_cast<float>(sceneWidth),static_cast<float>(sceneHeight)});
        if (!materialPrograms->Bind(*item.materialProxy,view,pass,geometry))
        {
            const auto &diagnostics=materialPrograms->GetDiagnostics();
            if (!diagnostics.empty()) lastShaderError=diagnostics.back().message;
            if (item.materialProxy==errorMaterial) return false;
            if (!errorMaterial)
            {
                const auto definition=MakeBuiltinPbrDefinition(); auto unlit=definition; unlit.renderState.shadingModel=MaterialShadingModel::Unlit;
                auto proxy=std::make_shared<MaterialRenderProxy>(); proxy->compiled=MaterialCompiler{}.Compile(unlit).material;
                proxy->state=unlit.renderState; proxy->state.twoSided=true;
                for (const auto &slot:proxy->compiled->layout.numericSlots)
                {
                    const auto p=std::find_if(definition.parameters.begin(),definition.parameters.end(),[&](const auto &p) { return p.name==slot.name; });
                    proxy->numericSlots.push_back(slot.name=="BaseColor" ? Vec4{1,0,1,1} : MaterialNumericSlot(p->defaultValue));
                }
                errorMaterial=proxy;
            }
            auto fallback=item; fallback.materialProxy=errorMaterial; return BindCompiledMaterial(fallback,pass,geometry);
        }
        materialPrograms->BindObject(item,view,geometry);
        if (item.materialProxy->state.twoSided) glDisable(GL_CULL_FACE);
        else { glEnable(GL_CULL_FACE); glCullFace(pass==MaterialPass::Shadow ? GL_FRONT : GL_BACK); }
        if (pass==MaterialPass::Base && item.materialProxy->state.blendMode==MaterialBlendMode::Alpha)
        { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }
        else { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
        return true;
    }
    struct LegacyReflection
    {
        std::vector<ShaderParameterInfo> parameters;
        std::unordered_map<std::string, GLint> locations;
    };
    std::unordered_map<std::string, LegacyReflection> legacyReflection;
    void UploadMaterialParameters(const std::shared_ptr<MaterialResource> &material)
    {
        if (!material || material->GetShaderPath().empty())
            return;
        auto found = legacyReflection.find(material->GetShaderPath());
        if (found == legacyReflection.end())
        {
            LegacyReflection reflection;
            reflection.parameters = ShaderPipeline::ReflectParameters(std::filesystem::u8path(material->GetShaderPath()));
            for (const auto &p : reflection.parameters)
                reflection.locations.emplace(p.name, GetUniformLocation(program, p.name.c_str()));
            found = legacyReflection.emplace(material->GetShaderPath(), std::move(reflection)).first;
        }
        for (const auto &parameter : found->second.parameters)
        {
            const auto value = material->GetParameters().find(parameter.name);
            if (value == material->GetParameters().end() || parameter.type == ShaderParameterType::Texture)
                continue;
            const GLint location = found->second.locations.at(parameter.name);
            if (parameter.type == ShaderParameterType::Float)
                Uniform1f(location, value->second.x);
            else if (parameter.type == ShaderParameterType::Vec2)
                Uniform2f(location, value->second.x, value->second.y);
            else if (parameter.type == ShaderParameterType::Vec3)
                Uniform3f(location, value->second.x, value->second.y, value->second.z);
            else if (parameter.type == ShaderParameterType::Vec4)
                Uniform4f(location, value->second.x, value->second.y, value->second.z, value->second.w);
        }
    }

    bool CreateAuxiliaryPrograms()
    {
        shadowProgram = CreateLinkedProgram(ShadowVertexShaderSource, ShadowFragmentShaderSource);
        skyProgram = CreateLinkedProgram(ToneVertexShaderSource, SkyFragmentShaderSource);
        toneProgram = CreateLinkedProgram(ToneVertexShaderSource, FinalToneFragmentShaderSource);
        presentProgram = CreateLinkedProgram(ToneVertexShaderSource, PresentFragmentShaderSource);
        hzbProgram = CreateLinkedProgram(ToneVertexShaderSource, HzbFragmentShaderSource);
        gtaoProgram = CreateLinkedProgram(ToneVertexShaderSource, GtaoFragmentShaderSource);
        gtaoFilterProgram = CreateLinkedProgram(ToneVertexShaderSource, GtaoFilterFragmentShaderSource);
        bloomProgram = CreateLinkedProgram(ToneVertexShaderSource, BloomFragmentShaderSource);
        taaProgram = CreateLinkedProgram(ToneVertexShaderSource, TaaFragmentShaderSource);
        if (computeCullingSupported)
        {
            gpuCullProgram = CreateComputeProgram(GpuCullComputeShaderSource);
            computeCullingSupported = gpuCullProgram != 0;
        }
        uiProgram = CreateLinkedProgram(UiVertexShaderSource, UiFragmentShaderSource);
        particleProgram = CreateLinkedProgram(ParticleVertexShaderSource, ParticleFragmentShaderSource);
        gpuParticleProgram = CreateLinkedProgram(GpuParticleVertexShaderSource, GpuParticleFragmentShaderSource);
        if (!shadowProgram || !skyProgram || !toneProgram || !presentProgram || !hzbProgram || !gtaoProgram || !gtaoFilterProgram ||
            !bloomProgram || !taaProgram || !uiProgram || !particleProgram || !gpuParticleProgram)
            return false;
        shadowMvpLocation = GetUniformLocation(shadowProgram, "uLightMvp");
        shadowLightViewProjectionLocation = GetUniformLocation(shadowProgram, "uLightViewProjection");
        shadowInstancedLocation = GetUniformLocation(shadowProgram, "uInstanced");
        shadowAlphaTextureLocation = GetUniformLocation(shadowProgram, "uShadowAlphaTexture");
        shadowHasAlphaTextureLocation = GetUniformLocation(shadowProgram, "uShadowHasAlphaTexture");
        shadowOpacityLocation = GetUniformLocation(shadowProgram, "uShadowOpacity");
        skyProjectionLocation = GetUniformLocation(skyProgram, "uProjection");
        skyViewLocation = GetUniformLocation(skyProgram, "uView");
        skyEnvironmentLocation = GetUniformLocation(skyProgram, "uEnvironmentSky");
        toneResolvedLocation = GetUniformLocation(toneProgram, "uResolved");
        presentSceneColorLocation = GetUniformLocation(presentProgram, "uSceneColor");
        exposureLocation = GetUniformLocation(toneProgram, "uExposure");
        inverseResolutionLocation = GetUniformLocation(toneProgram, "uInverseResolution");
        fxaaEnabledLocation = GetUniformLocation(toneProgram, "uFxaaEnabled");
        hzbSourceLocation = GetUniformLocation(hzbProgram, "uSource");
        hzbSourceTexelLocation = GetUniformLocation(hzbProgram, "uSourceTexel");
        gtaoDepthLocation = GetUniformLocation(gtaoProgram, "uDepth");
        gtaoInverseResolutionLocation = GetUniformLocation(gtaoProgram, "uInverseResolution");
        gtaoInverseProjectionLocation = GetUniformLocation(gtaoProgram, "uInverseProjection");
        gtaoFilterSourceLocation = GetUniformLocation(gtaoFilterProgram, "uSource");
        gtaoFilterDepthLocation = GetUniformLocation(gtaoFilterProgram, "uDepth");
        gtaoFilterInverseProjectionLocation = GetUniformLocation(gtaoFilterProgram, "uInverseProjection");
        bloomSourceLocation = GetUniformLocation(bloomProgram, "uSource");
        bloomSourceTexelLocation = GetUniformLocation(bloomProgram, "uSourceTexel");
        bloomExtractLocation = GetUniformLocation(bloomProgram, "uExtract");
        taaSceneLocation = GetUniformLocation(taaProgram, "uScene");
        taaAmbientLocation = GetUniformLocation(taaProgram, "uAmbient");
        taaVelocityLocation = GetUniformLocation(taaProgram, "uVelocity");
        taaHistoryLocation = GetUniformLocation(taaProgram, "uHistory");
        taaDepthLocation = GetUniformLocation(taaProgram, "uDepth");
        taaHistoryDepthLocation = GetUniformLocation(taaProgram, "uHistoryDepth");
        taaReprojectDepthLocation = GetUniformLocation(taaProgram, "uReprojectDepth");
        taaDepthRangeLocation = GetUniformLocation(taaProgram, "uDepthRange");
        taaGtaoLocation = GetUniformLocation(taaProgram, "uGtao");
        taaInverseResolutionLocation = GetUniformLocation(taaProgram, "uInverseResolution");
        taaHistoryValidLocation = GetUniformLocation(taaProgram, "uHistoryValid");
        taaEnabledLocation = GetUniformLocation(taaProgram, "uTaaEnabled");
        for (std::size_t index = 0; index < taaBloomLocations.size(); ++index)
            taaBloomLocations[index] = GetUniformLocation(taaProgram, ("uBloom" + std::to_string(index)).c_str());
        if (gpuCullProgram)
        {
            gpuCullPreviousViewProjectionLocation = GetUniformLocation(gpuCullProgram, "uPreviousViewProjection");
            gpuCullInputCountLocation = GetUniformLocation(gpuCullProgram, "uInputCount");
            gpuCullHzbValidLocation = GetUniformLocation(gpuCullProgram, "uHzbValid");
            gpuCullHzbCountLocation = GetUniformLocation(gpuCullProgram, "uHzbCount");
            for (std::size_t index = 0; index < gpuCullHzbLocations.size(); ++index)
                gpuCullHzbLocations[index] = GetUniformLocation(gpuCullProgram, ("uHzb[" + std::to_string(index) + "]").c_str());
        }
        GenVertexArrays(1, &fullScreenVao);
        BindVertexArray(fullScreenVao);
        GenBuffers(1, &particleInstanceBuffer);
        BindBuffer(GL_ARRAY_BUFFER, particleInstanceBuffer);
        constexpr GLsizei particleStride = sizeof(float) * 12;
        for (GLuint attribute = 0; attribute < 3; ++attribute)
        {
            VertexAttribPointer(attribute, 4, GL_FLOAT, GL_FALSE, particleStride,
                                reinterpret_cast<void *>(static_cast<std::uintptr_t>(attribute * sizeof(float) * 4)));
            EnableVertexAttribArray(attribute);
            VertexAttribDivisor(attribute, 1);
        }
        BindVertexArray(0);
        GenBuffers(1, &lightDataBuffer);
        glGenTextures(1, &lightDataTexture);
        GenBuffers(1, &clusterDataBuffer);
        glGenTextures(1, &clusterDataTexture);
        uiViewportLocation = GetUniformLocation(uiProgram, "uViewport");
        uiColorLocation = GetUniformLocation(uiProgram, "uColor");
        uiTextureLocation = GetUniformLocation(uiProgram, "uUiTexture");
        uiHasTextureLocation = GetUniformLocation(uiProgram, "uHasUiTexture");
        particleViewLocation = GetUniformLocation(particleProgram, "uParticleView");
        particleProjectionLocation = GetUniformLocation(particleProgram, "uParticleProjection");
        particlePositionLocation = GetUniformLocation(particleProgram, "uParticlePosition");
        particleSizeLocation = GetUniformLocation(particleProgram, "uParticleSize");
        particleColorLocation = GetUniformLocation(particleProgram, "uParticleColor");
        particleTextureLocation = GetUniformLocation(particleProgram, "uParticleTexture");
        particleHasTextureLocation = GetUniformLocation(particleProgram, "uHasParticleTexture");
        gpuParticleViewLocation = GetUniformLocation(gpuParticleProgram, "uParticleView");
        gpuParticleProjectionLocation = GetUniformLocation(gpuParticleProgram, "uParticleProjection");
        gpuEmitterPositionLocation = GetUniformLocation(gpuParticleProgram, "uEmitterPosition");
        gpuEmitterGravityLocation = GetUniformLocation(gpuParticleProgram, "uEmitterGravity");
        gpuEmitterStartColorLocation = GetUniformLocation(gpuParticleProgram, "uEmitterStartColor");
        gpuEmitterEndColorLocation = GetUniformLocation(gpuParticleProgram, "uEmitterEndColor");
        gpuEmitterParamsLocation = GetUniformLocation(gpuParticleProgram, "uEmitterParams");
        gpuEmitterFlipbookLocation = GetUniformLocation(gpuParticleProgram, "uEmitterFlipbook");
        gpuEmitterTimeLocation = GetUniformLocation(gpuParticleProgram, "uEmitterTime");
        gpuEmitterCapacityLocation = GetUniformLocation(gpuParticleProgram, "uEmitterCapacity");
        gpuEmitterFpsLocation = GetUniformLocation(gpuParticleProgram, "uEmitterFps");
        gpuParticleTextureLocation = GetUniformLocation(gpuParticleProgram, "uParticleTexture");
        gpuParticleHasTextureLocation = GetUniformLocation(gpuParticleProgram, "uHasParticleTexture");
        GenVertexArrays(1, &uiVao);
        BindVertexArray(uiVao);
        GenBuffers(1, &uiVertexBuffer);
        BindBuffer(GL_ARRAY_BUFFER, uiVertexBuffer);
        GenBuffers(1, &uiIndexBuffer);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, uiIndexBuffer);
        VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), reinterpret_cast<void *>(offsetof(UiVertex, position)));
        EnableVertexAttribArray(0);
        VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), reinterpret_cast<void *>(offsetof(UiVertex, uv)));
        EnableVertexAttribArray(1);
        BindVertexArray(0);
        return fullScreenVao != 0 && uiVao != 0 && CreateFontAtlas();
    }

    bool ReloadDefaultShadersFromFiles()
    {
        const auto vertex = ShaderPipeline::Preprocess(vertexShaderPath), fragment = ShaderPipeline::Preprocess(fragmentShaderPath);
        if (!vertex.success || !fragment.success)
        {
            lastShaderError = !vertex.success ? vertex.error : fragment.error;
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", lastShaderError.c_str());
            return false;
        }
        ShaderPipeline::WriteCache(vertex, "Saved/ShaderCache");
        ShaderPipeline::WriteCache(fragment, "Saved/ShaderCache");
        shaderDependencyWatchers.clear();
        for (const auto &dependency : vertex.dependencies)
            shaderDependencyWatchers[dependency.generic_string()].HasChanged(dependency);
        for (const auto &dependency : fragment.dependencies)
            shaderDependencyWatchers[dependency.generic_string()].HasChanged(dependency);
        const bool reloaded = CreateDefaultProgram(vertex.source.c_str(), fragment.source.c_str());
        SDL_Log(reloaded ? "Reloaded render shaders" : "Render shader reload failed; keeping the previous program");
        return reloaded;
    }

    void CheckShaderHotReload()
    {
        const bool vertexChanged = vertexShaderWatcher.HasChanged(vertexShaderPath);
        const bool fragmentChanged = fragmentShaderWatcher.HasChanged(fragmentShaderPath);
        bool dependencyChanged = false;
        for (auto &watcher : shaderDependencyWatchers)
            dependencyChanged = watcher.second.HasChanged(watcher.first) || dependencyChanged;
        if (vertexChanged || fragmentChanged || dependencyChanged)
            ReloadDefaultShadersFromFiles();
    }

    const GpuMesh *GetOrCreateMesh(const std::shared_ptr<MeshResource> &resource)
    {
        if (!resource || !resource->IsReady())
            return nullptr;
        const auto found = meshes.find(resource->GetPath());
        if (found != meshes.end())
            return &found->second;
        GpuMesh mesh;
        GenVertexArrays(1, &mesh.vao);
        BindVertexArray(mesh.vao);
        GenBuffers(1, &mesh.vertexBuffer);
        BindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
        const auto &vertices = resource->GetVertices();
        const auto &indices = resource->GetIndices();
        const std::size_t requiredBytes = vertices.size() * sizeof(MeshVertex) + indices.size() * sizeof(std::uint32_t);
        if (GetResidentBytes() + requiredBytes > gpuMemoryBudget)
            DestroyCachedResources();
        BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex)), vertices.data(), GL_STATIC_DRAW);
        GenBuffers(1, &mesh.indexBuffer);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
        BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(),
                   GL_STATIC_DRAW);
        VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, position)));
        EnableVertexAttribArray(0);
        VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, normal)));
        EnableVertexAttribArray(1);
        BindVertexArray(0);
        BindVertexArray(mesh.vao);
        VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, uv)));
        EnableVertexAttribArray(2);
        VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, tangent)));
        EnableVertexAttribArray(3);
        VertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, boneIndices)));
        EnableVertexAttribArray(4);
        VertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void *>(offsetof(MeshVertex, boneWeights)));
        EnableVertexAttribArray(5);
        // 已启用的顶点属性即使在 Shader 分支中未使用也可能被驱动抓取；普通绘制必须有合法的实例属性 0。
        const Mat4 identityInstance = Mat4::Identity();
        GenBuffers(1, &mesh.instanceBuffer);
        BindBuffer(GL_ARRAY_BUFFER, mesh.instanceBuffer);
        BufferData(GL_ARRAY_BUFFER, sizeof(Mat4), identityInstance.Data(), GL_DYNAMIC_DRAW);
        for (GLuint column = 0; column < 4; ++column)
        {
            VertexAttribPointer(8 + column, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4),
                                reinterpret_cast<void *>(static_cast<std::uintptr_t>(column * sizeof(float) * 4)));
            EnableVertexAttribArray(8 + column);
            VertexAttribDivisor(8 + column, 1);
        }
        GenBuffers(1, &mesh.previousInstanceBuffer);
        BindBuffer(GL_ARRAY_BUFFER, mesh.previousInstanceBuffer);
        BufferData(GL_ARRAY_BUFFER, sizeof(Mat4), identityInstance.Data(), GL_DYNAMIC_DRAW);
        for (GLuint column = 0; column < 4; ++column)
        {
            VertexAttribPointer(12 + column, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4),
                                reinterpret_cast<void *>(static_cast<std::uintptr_t>(column * sizeof(float) * 4)));
            EnableVertexAttribArray(12 + column);
            VertexAttribDivisor(12 + column, 1);
        }
        GenBuffers(1, &mesh.indirectBuffer);
        BindBuffer(0x8F3F, mesh.indirectBuffer);
        BufferData(0x8F3F, sizeof(std::uint32_t) * 5, nullptr, GL_DYNAMIC_DRAW);
        BindBuffer(0x8F3F, 0);
        GenBuffers(1, &mesh.cullInputBuffer);
        BindBuffer(0x90D2, mesh.cullInputBuffer);
        BufferData(0x90D2, 0, nullptr, GL_DYNAMIC_DRAW);
        BindBuffer(0x90D2, 0);
        BindVertexArray(0);
        mesh.indexCount = static_cast<GLsizei>(indices.size());
        mesh.byteSize = requiredBytes;
        return &meshes.emplace(resource->GetPath(), mesh).first->second;
    }

    GLuint GetOrCreateTexture(const std::shared_ptr<TextureResource> &resource, const bool srgb, const std::uint64_t version = 0)
    {
        if (!resource || !resource->IsReady())
            return 0;
        const std::string key = resource->GetPath() + (srgb ? "#srgb" : "#linear") + '#' + std::to_string(version);
        const auto found = textures.find(key);
        if (found != textures.end())
            return found->second;
        const std::size_t requiredBytes = resource->GetByteSize();
        if (GetResidentBytes() + requiredBytes > gpuMemoryBudget)
            DestroyCachedResources();
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        if (maximumAnisotropy > 1.0f)
            glTexParameterf(GL_TEXTURE_2D, 0x84FE, maximumAnisotropy);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (resource->GetCompression() != TextureResource::Compression::None)
        {
            GLenum format = 0;
            switch (resource->GetCompression())
            {
            case TextureResource::Compression::BC1:
                format = srgb ? 0x8C4D : 0x83F1;
                break;
            case TextureResource::Compression::BC2:
                format = srgb ? 0x8C4E : 0x83F2;
                break;
            case TextureResource::Compression::BC3:
                format = srgb ? 0x8C4F : 0x83F3;
                break;
            case TextureResource::Compression::BC4:
                format = 0x8DBB;
                break;
            case TextureResource::Compression::BC5:
                format = 0x8DBD;
                break;
            case TextureResource::Compression::BC6H:
                format = 0x8E8F;
                break;
            case TextureResource::Compression::BC7:
                format = srgb ? 0x8E8D : 0x8E8C;
                break;
            default:
                break;
            }
            const auto &blocks = resource->GetData();
            for (std::size_t mip = 0; mip < resource->GetMipLevels().size(); ++mip)
            {
                const auto &level = resource->GetMipLevels()[mip];
                CompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(mip), format, level.width, level.height, 0,
                                     static_cast<GLsizei>(level.size), blocks.data() + level.offset);
            }
        }
        else if (resource->IsHdr())
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, resource->GetWidth(), resource->GetHeight(), 0, GL_RGBA, GL_FLOAT,
                         resource->GetFloatData().data());
        else
            glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, resource->GetWidth(), resource->GetHeight(), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, resource->GetData().data());
        if (resource->GetCompression() == TextureResource::Compression::None || resource->GetMipLevels().size() <= 1)
            GenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        textures[key] = texture;
        textureBytes[key] = requiredBytes;
        return texture;
    }

    GLuint GetOrCreateUiTexture(const std::string &path)
    {
        if (path.empty())
            return 0;
        auto found = uiTextureResources.find(path);
        if (found == uiTextureResources.end())
        {
            auto resource = std::make_shared<TextureResource>();
            if (!resource->Load(path))
                return 0;
            found = uiTextureResources.emplace(path, std::move(resource)).first;
        }
        return GetOrCreateTexture(found->second, true);
    }

    void DrawUiGeometry(const std::vector<UiVertex> &vertices, const std::vector<std::uint32_t> &indices, const Vec4 &color,
                        const GLuint texture, const int width, const int height)
    {
        if (vertices.empty() || indices.empty())
            return;
        UseProgram(uiProgram);
        Uniform2f(uiViewportLocation, static_cast<float>(width), static_cast<float>(height));
        Uniform4f(uiColorLocation, color.x, color.y, color.z, color.w);
        Uniform1i(uiTextureLocation, 0);
        Uniform1i(uiHasTextureLocation, texture ? 1 : 0);
        ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        BindVertexArray(uiVao);
        BindBuffer(GL_ARRAY_BUFFER, uiVertexBuffer);
        BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(UiVertex)), vertices.data(), GL_DYNAMIC_DRAW);
        BindBuffer(GL_ELEMENT_ARRAY_BUFFER, uiIndexBuffer);
        BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(),
                   GL_DYNAMIC_DRAW);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
        BindVertexArray(0);
    }
};

SdlGlRenderDevice::SdlGlRenderDevice(const int width, const int height, std::string title)
    : m_impl(std::make_unique<Impl>()), m_width(width), m_height(height), m_title(std::move(title))
{
}
SdlGlRenderDevice::~SdlGlRenderDevice()
{
    Shutdown();
}

// 先创建窗口和上下文，再探测扩展能力；可选功能失败时降级，核心着色器失败则终止初始化。
bool SdlGlRenderDevice::Initialize()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0)
        return false;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    m_impl->window = SDL_CreateWindow(m_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_width, m_height,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!m_impl->window)
    {
        SDL_Quit();
        return false;
    }
    // 以窗口所在显示器的 DPI 推导缩放比例，供编辑器按像素缩放手写布局与字号。
    {
        float displayDpi = 0.0f;
        const int displayIndex = SDL_GetWindowDisplayIndex(m_impl->window);
        if (displayIndex >= 0 && SDL_GetDisplayDPI(displayIndex, &displayDpi, nullptr, nullptr) == 0 && displayDpi > 0.0f)
            m_dpiScale = std::clamp(displayDpi / 96.0f, 1.0f, 4.0f);
    }
    m_impl->context = SDL_GL_CreateContext(m_impl->window);
    if (!m_impl->context || !m_impl->LoadFunctions())
    {
        Shutdown();
        return false;
    }
    // Core Profile 不允许 glGetString(GL_EXTENSIONS)，由 SDL 使用 glGetStringi 查询。
    if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic"))
        glGetFloatv(0x84FF, &m_impl->maximumAnisotropy);
    const auto vertexSource = ShaderPipeline::Preprocess(m_impl->vertexShaderPath),
               fragmentSource = ShaderPipeline::Preprocess(m_impl->fragmentShaderPath);
    if (!vertexSource.success || !fragmentSource.success)
        SDL_Log("Default shader files unavailable; using embedded default render shaders.");
    const bool programCreated = vertexSource.success && fragmentSource.success
                                    ? m_impl->CreateDefaultProgram(vertexSource.source.c_str(), fragmentSource.source.c_str())
                                    : m_impl->CreateDefaultProgram();
    if (!programCreated || !m_impl->CreateAuxiliaryPrograms() || !m_impl->CreateShadowTarget())
    {
        Shutdown();
        return false;
    }
    m_impl->materialPrograms=std::make_unique<GlMaterialProgramCache>([this](const auto &texture,bool srgb,std::uint64_t version) {
        return m_impl->GetOrCreateTexture(texture,srgb,version);
    });
    if (!m_impl->materialPrograms->Initialize()) { Shutdown(); return false; }
    m_impl->GenQueries(2, m_impl->timerQueries);
    m_impl->vertexShaderWatcher.HasChanged(m_impl->vertexShaderPath);
    m_impl->fragmentShaderWatcher.HasChanged(m_impl->fragmentShaderPath);
    SDL_GL_SetSwapInterval(1);
    SDL_StopTextInput();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    int drawableWidth = 0, drawableHeight = 0;
    SDL_GL_GetDrawableSize(m_impl->window, &drawableWidth, &drawableHeight);
    if (drawableHeight > 0)
        m_aspect = static_cast<float>(drawableWidth) / static_cast<float>(drawableHeight);
    m_impl->sceneTargetEnabled = true;
    m_impl->presentSceneToWindow = true;
    if (!m_impl->CreateSceneTarget(std::max(1, drawableWidth), std::max(1, drawableHeight)))
    {
        Shutdown();
        return false;
    }
    return true;
}

// GPU 缓存必须先于 OpenGL Context 清空，否则驱动无法安全释放对应对象。
void SdlGlRenderDevice::Shutdown()
{
    if (!m_impl)
        return;
    if (m_impl->context)
    {
        m_impl->materialPrograms.reset();
        m_impl->DestroySceneTarget();
        m_impl->DestroyShadowTarget();
        if (m_impl->timerQueries[0])
        {
            m_impl->DeleteQueries(2, m_impl->timerQueries);
            m_impl->timerQueries[0] = m_impl->timerQueries[1] = 0;
        }
        for (const auto &pair : m_impl->meshes)
        {
            m_impl->DeleteBuffers(1, &pair.second.vertexBuffer);
            m_impl->DeleteBuffers(1, &pair.second.indexBuffer);
            if (pair.second.instanceBuffer)
                m_impl->DeleteBuffers(1, &pair.second.instanceBuffer);
            if (pair.second.previousInstanceBuffer)
                m_impl->DeleteBuffers(1, &pair.second.previousInstanceBuffer);
            if (pair.second.indirectBuffer)
                m_impl->DeleteBuffers(1, &pair.second.indirectBuffer);
            if (pair.second.cullInputBuffer)
                m_impl->DeleteBuffers(1, &pair.second.cullInputBuffer);
            m_impl->DeleteVertexArrays(1, &pair.second.vao);
        }
        m_impl->meshes.clear();
        m_impl->uiTextureResources.clear();
        for (const auto &pair : m_impl->textures)
            glDeleteTextures(1, &pair.second);
        m_impl->textures.clear();
        m_impl->textureBytes.clear();
        if (m_impl->fontAtlas)
        {
            glDeleteTextures(1, &m_impl->fontAtlas);
            m_impl->fontAtlas = 0;
        }
#ifdef TINYENGINE_HAS_FREETYPE
        if (m_impl->freeTypeFace)
        {
            FT_Done_Face(m_impl->freeTypeFace);
            m_impl->freeTypeFace = nullptr;
        }
        if (m_impl->freeType)
        {
            FT_Done_FreeType(m_impl->freeType);
            m_impl->freeType = nullptr;
        }
#endif
        if (m_impl->program)
        {
            m_impl->DeleteProgram(m_impl->program);
            m_impl->program = 0;
        }
        if (m_impl->shadowProgram)
        {
            m_impl->DeleteProgram(m_impl->shadowProgram);
            m_impl->shadowProgram = 0;
        }
        if (m_impl->skyProgram)
        {
            m_impl->DeleteProgram(m_impl->skyProgram);
            m_impl->skyProgram = 0;
        }
        if (m_impl->toneProgram)
        {
            m_impl->DeleteProgram(m_impl->toneProgram);
            m_impl->toneProgram = 0;
        }
        if (m_impl->presentProgram)
        {
            m_impl->DeleteProgram(m_impl->presentProgram);
            m_impl->presentProgram = 0;
        }
        for (GLuint *program :
             {&m_impl->hzbProgram, &m_impl->gtaoProgram, &m_impl->gtaoFilterProgram, &m_impl->bloomProgram, &m_impl->taaProgram})
        {
            if (*program)
            {
                m_impl->DeleteProgram(*program);
                *program = 0;
            }
        }
        if (m_impl->gpuCullProgram)
        {
            m_impl->DeleteProgram(m_impl->gpuCullProgram);
            m_impl->gpuCullProgram = 0;
        }
        if (m_impl->uiProgram)
        {
            m_impl->DeleteProgram(m_impl->uiProgram);
            m_impl->uiProgram = 0;
        }
        if (m_impl->particleProgram)
        {
            m_impl->DeleteProgram(m_impl->particleProgram);
            m_impl->particleProgram = 0;
        }
        if (m_impl->gpuParticleProgram)
        {
            m_impl->DeleteProgram(m_impl->gpuParticleProgram);
            m_impl->gpuParticleProgram = 0;
        }
        if (m_impl->fullScreenVao)
        {
            m_impl->DeleteVertexArrays(1, &m_impl->fullScreenVao);
            m_impl->fullScreenVao = 0;
        }
        if (m_impl->particleInstanceBuffer)
        {
            m_impl->DeleteBuffers(1, &m_impl->particleInstanceBuffer);
            m_impl->particleInstanceBuffer = 0;
        }
        for (GLuint *buffer : {&m_impl->lightDataBuffer, &m_impl->clusterDataBuffer})
        {
            if (*buffer)
            {
                m_impl->DeleteBuffers(1, buffer);
                *buffer = 0;
            }
        }
        for (GLuint *texture : {&m_impl->lightDataTexture, &m_impl->clusterDataTexture})
        {
            if (*texture)
            {
                glDeleteTextures(1, texture);
                *texture = 0;
            }
        }
        if (m_impl->uiVertexBuffer)
        {
            m_impl->DeleteBuffers(1, &m_impl->uiVertexBuffer);
            m_impl->uiVertexBuffer = 0;
        }
        if (m_impl->uiIndexBuffer)
        {
            m_impl->DeleteBuffers(1, &m_impl->uiIndexBuffer);
            m_impl->uiIndexBuffer = 0;
        }
        if (m_impl->uiVao)
        {
            m_impl->DeleteVertexArrays(1, &m_impl->uiVao);
            m_impl->uiVao = 0;
        }
        SDL_GL_DeleteContext(m_impl->context);
        m_impl->context = nullptr;
    }
    if (m_impl->controller)
    {
        SDL_GameControllerClose(m_impl->controller);
        m_impl->controller = nullptr;
    }
    if (m_impl->window)
    {
        SDL_DestroyWindow(m_impl->window);
        m_impl->window = nullptr;
    }
    SDL_Quit();
}

bool SdlGlRenderDevice::PumpEvents()
{
    m_impl->CheckShaderHotReload();
    m_impl->input.mouseX = m_impl->input.mouseY = 0.0f;
    m_impl->input.textInput.clear();
    m_impl->input.uiBackspace = false;
    bool mousePressed = false;
    std::array<bool, SDL_NUM_SCANCODES> pressedThisFrame{};
    int pressX = 0, pressY = 0;
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (m_eventCallback)
            m_eventCallback(&event);
        if (event.type == SDL_QUIT)
        {
            if (m_closeRequestCallback)
                m_closeRequestCallback();
            else
                return false;
        }
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
            // 高 DPI 窗口的可绘制尺寸与逻辑尺寸不同；视口必须用前者，否则画面只填满左下子区域。
            int drawableWidth = event.window.data1, drawableHeight = event.window.data2;
            if (m_impl->window)
                SDL_GL_GetDrawableSize(m_impl->window, &drawableWidth, &drawableHeight);
            glViewport(0, 0, drawableWidth, drawableHeight);
            if (event.window.data2 > 0)
                m_aspect = static_cast<float>(event.window.data1) / static_cast<float>(event.window.data2);
        }
        if (event.type == SDL_MOUSEMOTION)
        {
            m_impl->input.mouseX += static_cast<float>(event.motion.xrel);
            m_impl->input.mouseY += static_cast<float>(event.motion.yrel);
        }
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            mousePressed = true;
            pressX = event.button.x;
            pressY = event.button.y;
        }
        if (event.type == SDL_TEXTINPUT)
            m_impl->input.textInput += event.text.text;
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode >= 0 && event.key.keysym.scancode < SDL_NUM_SCANCODES)
            pressedThisFrame[static_cast<std::size_t>(event.key.keysym.scancode)] = true;
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE && event.key.repeat == 0)
            m_impl->input.uiBackspace = true;
        if (event.type == SDL_CONTROLLERDEVICEADDED && !m_impl->controller)
            m_impl->controller = SDL_GameControllerOpen(event.cdevice.which);
        if (event.type == SDL_CONTROLLERDEVICEREMOVED && m_impl->controller &&
            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(m_impl->controller)) == event.cdevice.which)
        {
            SDL_GameControllerClose(m_impl->controller);
            m_impl->controller = nullptr;
        }
    }
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    // 同一帧内按下又松开的短按也要交给游戏处理，持续按住仍以 SDL 当前状态为准。
    const auto down = [keys, &pressedThisFrame](SDL_Scancode code) {
        return keys[code] != 0 || pressedThisFrame[static_cast<std::size_t>(code)];
    };
    const auto axis = [&down](SDL_Scancode negative, SDL_Scancode positive) {
        return (down(positive) ? 1.0f : 0.0f) - (down(negative) ? 1.0f : 0.0f);
    };
    m_impl->input.moveX = axis(SDL_SCANCODE_A, SDL_SCANCODE_D);
    m_impl->input.moveY = axis(SDL_SCANCODE_F, SDL_SCANCODE_R);
    m_impl->input.moveZ = axis(SDL_SCANCODE_S, SDL_SCANCODE_W);
    m_impl->input.rotateX = axis(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    m_impl->input.rotateY = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
    m_impl->input.rotateZ = axis(SDL_SCANCODE_Q, SDL_SCANCODE_E);
    m_impl->input.scale = axis(SDL_SCANCODE_X, SDL_SCANCODE_Z);
    m_impl->input.sprint = down(SDL_SCANCODE_LSHIFT) || down(SDL_SCANCODE_RSHIFT);
    m_impl->input.jump = down(SDL_SCANCODE_SPACE);
    m_impl->input.primaryAction = mousePressed || (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    int cursorX = 0, cursorY = 0;
    SDL_GetMouseState(&cursorX, &cursorY);
    if (mousePressed)
    {
        cursorX = pressX;
        cursorY = pressY;
    }
    int windowWidth = 0, windowHeight = 0;
    SDL_GetWindowSize(m_impl->window, &windowWidth, &windowHeight);
    const Vec2 uiViewport = GetViewportSize();
    m_impl->input.cursorX = static_cast<float>(cursorX) * uiViewport.x / static_cast<float>(std::max(1, windowWidth));
    m_impl->input.cursorY = static_cast<float>(cursorY) * uiViewport.y / static_cast<float>(std::max(1, windowHeight));
    // 编辑器通过 SetEscapeQuits(false) 关闭平台层 ESC→退出，改由 ImGui 侧处理 ESC（清空选择/关闭弹窗）。
    m_impl->input.quitRequested = (m_escapeQuits && down(SDL_SCANCODE_ESCAPE));
    if (!m_impl->controller)
        for (int index = 0; index < SDL_NumJoysticks(); ++index)
            if (SDL_IsGameController(index))
            {
                m_impl->controller = SDL_GameControllerOpen(index);
                if (m_impl->controller)
                    break;
            }
    bool controllerNext = false, controllerAccept = false;
    if (m_impl->controller)
    {
        const auto stick = [this](const SDL_GameControllerAxis axisValue) {
            constexpr float deadZone = 8000.0f;
            const float value = static_cast<float>(SDL_GameControllerGetAxis(m_impl->controller, axisValue));
            if (std::abs(value) < deadZone)
                return 0.0f;
            return std::clamp((value - std::copysign(deadZone, value)) / (32767.0f - deadZone), -1.0f, 1.0f);
        };
        const float gamepadMoveX = stick(SDL_CONTROLLER_AXIS_LEFTX), gamepadMoveZ = -stick(SDL_CONTROLLER_AXIS_LEFTY);
        const float gamepadLookX = stick(SDL_CONTROLLER_AXIS_RIGHTX), gamepadLookY = stick(SDL_CONTROLLER_AXIS_RIGHTY);
        if (std::abs(gamepadMoveX) > std::abs(m_impl->input.moveX))
            m_impl->input.moveX = gamepadMoveX;
        if (std::abs(gamepadMoveZ) > std::abs(m_impl->input.moveZ))
            m_impl->input.moveZ = gamepadMoveZ;
        m_impl->input.rotateY += gamepadLookX * 2.0f;
        m_impl->input.rotateX += gamepadLookY * 2.0f;
        m_impl->input.primaryAction =
            m_impl->input.primaryAction || SDL_GameControllerGetButton(m_impl->controller, SDL_CONTROLLER_BUTTON_A);
        m_impl->input.sprint = m_impl->input.sprint || SDL_GameControllerGetButton(m_impl->controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        m_impl->input.jump = m_impl->input.jump || SDL_GameControllerGetButton(m_impl->controller, SDL_CONTROLLER_BUTTON_A);
        controllerNext = SDL_GameControllerGetButton(m_impl->controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        controllerAccept = SDL_GameControllerGetButton(m_impl->controller, SDL_CONTROLLER_BUTTON_A);
    }
    const bool uiNext = down(SDL_SCANCODE_TAB) || controllerNext,
               uiAccept = down(SDL_SCANCODE_RETURN) || down(SDL_SCANCODE_SPACE) || controllerAccept;
    m_impl->input.uiNavigateNext = uiNext && !m_impl->previousUiNext;
    m_impl->input.uiAccept = uiAccept && !m_impl->previousUiAccept;
    m_impl->previousUiNext = uiNext;
    m_impl->previousUiAccept = uiAccept;
    return !m_impl->input.quitRequested;
}

const InputState &SdlGlRenderDevice::GetInputState() const
{
    return m_impl->input;
}
float SdlGlRenderDevice::GetAspectRatio() const
{
    return m_impl && m_impl->sceneTargetEnabled && m_impl->sceneHeight > 0
               ? static_cast<float>(m_impl->sceneWidth) / static_cast<float>(m_impl->sceneHeight)
               : m_aspect;
}
Vec2 SdlGlRenderDevice::GetViewportSize() const
{
    if (m_impl && m_impl->sceneTargetEnabled)
        return {static_cast<float>(m_impl->sceneWidth), static_cast<float>(m_impl->sceneHeight)};
    int width = 0, height = 0;
    if (m_impl && m_impl->window)
        SDL_GL_GetDrawableSize(m_impl->window, &width, &height);
    return {static_cast<float>(width), static_cast<float>(height)};
}
void SdlGlRenderDevice::SetEscapeQuits(const bool enabled)
{
    m_escapeQuits = enabled;
}
float SdlGlRenderDevice::GetDpiScale() const
{
    return m_dpiScale;
}

void SdlGlRenderDevice::BeginShadowPass(const FrameView &view, const std::size_t cascadeIndex)
{
    m_impl->view = view;
    m_impl->activeShadowCascade = std::min(cascadeIndex, FrameView::MaxShadowCascades - 1);
    // 三个阴影级联属于同一帧，GL_TIME_ELAPSED 查询不能嵌套开启。
    if (!m_impl->timerActive)
    {
        const int previous = (m_impl->timerIndex + 1) % 2;
        if (m_impl->timerIssued[previous])
        {
            GLint available = GL_FALSE;
            m_impl->GetQueryObjectiv(m_impl->timerQueries[previous], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available == GL_TRUE)
            {
                GLuint64 nanoseconds = 0;
                m_impl->GetQueryObjectui64v(m_impl->timerQueries[previous], GL_QUERY_RESULT, &nanoseconds);
                m_impl->lastGpuFrameMilliseconds = static_cast<float>(nanoseconds) / 1000000.0f;
            }
        }
        m_impl->BeginQuery(GL_TIME_ELAPSED, m_impl->timerQueries[m_impl->timerIndex]);
        m_impl->timerActive = true;
    }
    m_impl->BindFramebuffer(GL_FRAMEBUFFER, m_impl->shadowFramebuffers[m_impl->activeShadowCascade]);
    glViewport(0, 0, m_impl->shadowResolution, m_impl->shadowResolution);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    m_impl->UseProgram(m_impl->shadowProgram);
    m_impl->Uniform1i(m_impl->shadowAlphaTextureLocation, 0);
}

void SdlGlRenderDevice::DrawShadow(const RenderItem &item)
{
    if (item.materialProxy && item.materialProxy->legacyShaderPath.empty())
    {
        const auto geometry=item.skinMatrices.empty() ? MaterialGeometry::Static : MaterialGeometry::Skinned;
        if (!m_impl->BindCompiledMaterial(item,MaterialPass::Shadow,geometry)) return;
        const auto *compiledMesh=m_impl->GetOrCreateMesh(item.mesh);
        if (!compiledMesh) return;
        m_impl->BindVertexArray(compiledMesh->vao);
        glDrawElements(GL_TRIANGLES,compiledMesh->indexCount,GL_UNSIGNED_INT,nullptr);
        m_impl->BindVertexArray(0);
        return;
    }
    m_impl->UseProgram(m_impl->shadowProgram);
    glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
    const auto *mesh = m_impl->GetOrCreateMesh(item.mesh);
    if (!mesh)
        return;
    const Mat4 lightMvp = m_impl->view.shadowViewProjections[m_impl->activeShadowCascade] * item.model;
    m_impl->UniformMatrix4fv(m_impl->shadowMvpLocation, 1, GL_FALSE, lightMvp.Data());
    m_impl->Uniform1i(m_impl->shadowInstancedLocation, 0);
    const GLuint alphaTexture = m_impl->GetOrCreateTexture(item.texture, true);
    m_impl->ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, alphaTexture);
    m_impl->Uniform1i(m_impl->shadowHasAlphaTextureLocation, alphaTexture ? 1 : 0);
    const float opacity = item.material ? item.material->GetBaseColor().w : 1.0f;
    m_impl->Uniform1f(m_impl->shadowOpacityLocation, opacity);
    m_impl->BindVertexArray(mesh->vao);
    glDrawElements(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT, nullptr);
    m_impl->BindVertexArray(0);
}

void SdlGlRenderDevice::DrawShadowInstanced(const std::vector<RenderItem> &items)
{
    if (items.empty())
        return;
    if (items.front().materialProxy && items.front().materialProxy->legacyShaderPath.empty())
    {
        if (!m_impl->BindCompiledMaterial(items.front(),MaterialPass::Shadow,MaterialGeometry::Instanced)) return;
        auto *mesh=const_cast<Impl::GpuMesh *>(m_impl->GetOrCreateMesh(items.front().mesh)); if (!mesh) return;
        std::vector<Mat4> models; for (const auto &item:items) models.push_back(item.model);
        m_impl->BindVertexArray(mesh->vao); m_impl->BindBuffer(GL_ARRAY_BUFFER,mesh->instanceBuffer);
        m_impl->BufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(models.size()*sizeof(Mat4)),models.data(),GL_DYNAMIC_DRAW);
        m_impl->DrawElementsInstanced(GL_TRIANGLES,mesh->indexCount,GL_UNSIGNED_INT,nullptr,static_cast<GLsizei>(models.size()));
        m_impl->BindVertexArray(0); return;
    }
    m_impl->UseProgram(m_impl->shadowProgram); glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
    auto *mesh = const_cast<Impl::GpuMesh *>(m_impl->GetOrCreateMesh(items.front().mesh));
    if (!mesh)
        return;
    std::vector<Mat4> models;
    models.reserve(items.size());
    for (const auto &item : items)
        models.push_back(item.model);
    m_impl->BindVertexArray(mesh->vao);
    m_impl->BindBuffer(GL_ARRAY_BUFFER, mesh->instanceBuffer);
    m_impl->BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(models.size() * sizeof(Mat4)), models.data(), GL_DYNAMIC_DRAW);
    m_impl->Uniform1i(m_impl->shadowInstancedLocation, 1);
    m_impl->UniformMatrix4fv(m_impl->shadowLightViewProjectionLocation, 1, GL_FALSE,
                             m_impl->view.shadowViewProjections[m_impl->activeShadowCascade].Data());
    const GLuint alphaTexture = m_impl->GetOrCreateTexture(items.front().texture, true);
    m_impl->ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, alphaTexture);
    m_impl->Uniform1i(m_impl->shadowHasAlphaTextureLocation, alphaTexture ? 1 : 0);
    m_impl->Uniform1f(m_impl->shadowOpacityLocation, items.front().material ? items.front().material->GetBaseColor().w : 1.0f);
    m_impl->DrawElementsInstanced(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(models.size()));
    m_impl->BindVertexArray(0);
}

void SdlGlRenderDevice::EndShadowPass()
{
    glDisable(GL_POLYGON_OFFSET_FILL);
    glCullFace(GL_BACK);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    m_impl->BindFramebuffer(GL_FRAMEBUFFER, 0);
}

// 保存本帧公共常量并准备离屏颜色、深度和运动矢量目标，后处理统一从这些目标读取。
void SdlGlRenderDevice::BeginFrame(const FrameView &view)
{
    if (view.resetTemporalHistory)
        m_impl->historyValid = false;
    m_impl->view = view;
    if (m_impl->sceneTargetEnabled && m_impl->presentSceneToWindow)
    {
        int width = 0, height = 0;
        SDL_GL_GetDrawableSize(m_impl->window, &width, &height);
        if (width > 0 && height > 0 && (width != m_impl->sceneWidth || height != m_impl->sceneHeight))
            m_impl->CreateSceneTarget(width, height);
    }
    if (m_impl->sceneTargetEnabled && m_impl->sceneFramebuffer)
    {
        m_impl->BindFramebuffer(GL_FRAMEBUFFER, m_impl->sceneFramebuffer);
        glViewport(0, 0, m_impl->sceneWidth, m_impl->sceneHeight);
    }
    else
    {
        m_impl->BindFramebuffer(GL_FRAMEBUFFER, 0);
        int width = 0, height = 0;
        SDL_GL_GetDrawableSize(m_impl->window, &width, &height);
        glViewport(0, 0, width, height);
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_impl->UseProgram(m_impl->program);
    const Mat4 viewProjection = view.projection * view.view;
    m_impl->UniformMatrix4fv(m_impl->viewProjectionLocation, 1, GL_FALSE, viewProjection.Data());
    m_impl->UniformMatrix4fv(m_impl->previousViewProjectionLocation, 1, GL_FALSE, view.previousViewProjection.Data());
    m_impl->UniformMatrix4fv(m_impl->viewLocation, 1, GL_FALSE, view.view.Data());
    for (std::size_t cascade = 0; cascade < FrameView::MaxShadowCascades; ++cascade)
    {
        m_impl->UniformMatrix4fv(m_impl->shadowViewProjectionLocations[cascade], 1, GL_FALSE, view.shadowViewProjections[cascade].Data());
        m_impl->Uniform1f(m_impl->shadowCascadeSplitLocations[cascade], view.shadowCascadeSplits[cascade]);
        m_impl->Uniform1i(m_impl->shadowMapLocations[cascade], static_cast<GLint>(8 + cascade));
        m_impl->ActiveTexture(GL_TEXTURE0 + 8 + static_cast<GLenum>(cascade));
        glBindTexture(GL_TEXTURE_2D, m_impl->shadowDepths[cascade]);
    }
    m_impl->Uniform1i(m_impl->lightCountLocation, view.lightCount);
    std::vector<float> lightData;
    lightData.reserve(static_cast<std::size_t>(view.lightCount) * 16);
    for (int index = 0; index < view.lightCount && index < static_cast<int>(FrameView::MaxLights); ++index)
    {
        const auto &light = view.lights[index];
        lightData.insert(lightData.end(),
                         {light.position.x, light.position.y, light.position.z, static_cast<float>(light.type), light.direction.x,
                          light.direction.y, light.direction.z, light.range, light.color.x, light.color.y, light.color.z, light.intensity,
                          light.innerConeCosine, light.outerConeCosine, 0.0f, 0.0f});
    }
    m_impl->BindBuffer(0x8C2A, m_impl->lightDataBuffer);
    m_impl->BufferData(0x8C2A, static_cast<GLsizeiptr>(lightData.size() * sizeof(float)), lightData.data(), GL_DYNAMIC_DRAW);
    m_impl->ActiveTexture(GL_TEXTURE14);
    glBindTexture(0x8C2A, m_impl->lightDataTexture);
    m_impl->TexBuffer(0x8C2A, 0x8814, m_impl->lightDataBuffer);
    m_impl->Uniform1i(m_impl->lightDataLocation, 14);
    const auto &clusters = view.clusteredLights;
    std::vector<std::uint32_t> clusterData;
    clusterData.reserve(clusters.offsets.size() * 2 + clusters.lightIndices.size());
    for (std::size_t index = 0; index < clusters.offsets.size(); ++index)
    {
        clusterData.push_back(clusters.offsets[index]);
        clusterData.push_back(clusters.counts[index]);
    }
    clusterData.insert(clusterData.end(), clusters.lightIndices.begin(), clusters.lightIndices.end());
    m_impl->BindBuffer(0x8C2A, m_impl->clusterDataBuffer);
    m_impl->BufferData(0x8C2A, static_cast<GLsizeiptr>(clusterData.size() * sizeof(std::uint32_t)), clusterData.data(), GL_DYNAMIC_DRAW);
    m_impl->ActiveTexture(GL_TEXTURE15);
    glBindTexture(0x8C2A, m_impl->clusterDataTexture);
    m_impl->TexBuffer(0x8C2A, 0x8236, m_impl->clusterDataBuffer);
    m_impl->Uniform1i(m_impl->clusterDataLocation, 15);
    m_impl->Uniform3i(m_impl->clusterDimensionsLocation, static_cast<GLint>(clusters.tilesX), static_cast<GLint>(clusters.tilesY),
                      static_cast<GLint>(clusters.slicesZ));
    m_impl->Uniform2f(m_impl->clusterDepthRangeLocation, clusters.nearPlane, clusters.farPlane);
    const Vec2 viewport = GetViewportSize();
    m_impl->Uniform2f(m_impl->clusterViewportLocation, viewport.x, viewport.y);
    m_impl->Uniform3f(m_impl->cameraPositionLocation, view.cameraPosition.x, view.cameraPosition.y, view.cameraPosition.z);
    m_impl->Uniform1i(m_impl->textureLocation, 0);
    m_impl->ActiveTexture(GL_TEXTURE0);
}

// 天空在几何体之前写入 HDR 目标；随后网格的深度测试和颜色写入会自然覆盖它。
void SdlGlRenderDevice::DrawSkybox()
{
    if (!m_impl || !m_impl->view.environmentSky || !m_impl->view.environmentSky->IsReady())
        return;
    const GLuint skyTexture = m_impl->GetOrCreateTexture(m_impl->view.environmentSky, false);
    if (!skyTexture)
        return;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    m_impl->UseProgram(m_impl->skyProgram);
    m_impl->UniformMatrix4fv(m_impl->skyProjectionLocation, 1, GL_FALSE, m_impl->view.projection.Data());
    m_impl->UniformMatrix4fv(m_impl->skyViewLocation, 1, GL_FALSE, m_impl->view.view.Data());
    m_impl->ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, skyTexture);
    m_impl->Uniform1i(m_impl->skyEnvironmentLocation, 0);
    m_impl->BindVertexArray(m_impl->fullScreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    m_impl->BindVertexArray(0);
    m_impl->ActiveTexture(GL_TEXTURE0);
    m_impl->UseProgram(m_impl->program);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void SdlGlRenderDevice::Draw(const RenderItem &item)
{
    if (item.materialProxy && item.materialProxy->legacyShaderPath.empty())
    {
        const auto geometry=item.skinMatrices.empty() ? MaterialGeometry::Static : MaterialGeometry::Skinned;
        if (!m_impl->BindCompiledMaterial(item,MaterialPass::Base,geometry)) return;
        const auto *compiledMesh=m_impl->GetOrCreateMesh(item.mesh);
        if (!compiledMesh) return;
        m_impl->BindVertexArray(compiledMesh->vao);
        glDrawElements(GL_TRIANGLES,compiledMesh->indexCount,GL_UNSIGNED_INT,nullptr);
        m_impl->BindVertexArray(0);
        return;
    }
    m_impl->UseProgram(m_impl->program);
    glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    if (item.transparent)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    else
    {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    const auto *mesh = m_impl->GetOrCreateMesh(item.mesh);
    if (!mesh)
        return;
    const Mat4 mvp = m_impl->view.projection * m_impl->view.view * item.model;
    const Vec4 color = item.material ? item.material->GetBaseColor() : Vec4{0.3f, 0.7f, 1.0f, 1.0f};
    const float metallic = item.material ? item.material->GetMetallic() : 0.0f;
    const float roughness = item.material ? item.material->GetRoughness() : 0.7f;
    const Vec3 emissive = item.material ? item.material->GetEmissive() : Vec3{};
    m_impl->UniformMatrix4fv(m_impl->mvpLocation, 1, GL_FALSE, mvp.Data());
    m_impl->Uniform1i(m_impl->instancedLocation, 0);
    std::array<Mat4, 64> identityBones;
    for (auto &bone : identityBones)
        bone = Mat4::Identity();
    for (std::size_t index = 0; index < item.skinMatrices.size() && index < identityBones.size(); ++index)
        identityBones[index] = item.skinMatrices[index];
    m_impl->UniformMatrix4fv(m_impl->bonesLocation, static_cast<GLsizei>(identityBones.size()), GL_FALSE, identityBones[0].Data());
    m_impl->UniformMatrix4fv(m_impl->modelLocation, 1, GL_FALSE, item.model.Data());
    m_impl->UniformMatrix4fv(m_impl->previousModelLocation, 1, GL_FALSE, item.previousModel.Data());
    m_impl->Uniform4f(m_impl->baseColorLocation, color.x, color.y, color.z, color.w);
    m_impl->Uniform1f(m_impl->metallicLocation, metallic);
    m_impl->Uniform1f(m_impl->roughnessLocation, roughness);
    m_impl->Uniform3f(m_impl->emissiveLocation, emissive.x, emissive.y, emissive.z);
    m_impl->UploadMaterialParameters(item.material);
    auto bindTexture = [&](const std::shared_ptr<TextureResource> &resource, const int unit, const bool srgb, const GLint samplerLocation,
                           const GLint enabledLocation) {
        const GLuint texture = m_impl->GetOrCreateTexture(resource, srgb);
        m_impl->ActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
        m_impl->Uniform1i(samplerLocation, unit);
        m_impl->Uniform1i(enabledLocation, texture != 0 ? 1 : 0);
    };
    bindTexture(item.texture, 0, true, m_impl->textureLocation, m_impl->hasTextureLocation);
    bindTexture(item.normalTexture, 2, false, m_impl->normalTextureLocation, m_impl->hasNormalTextureLocation);
    bindTexture(item.metallicTexture, 3, false, m_impl->metallicTextureLocation, m_impl->hasMetallicTextureLocation);
    bindTexture(item.roughnessTexture, 4, false, m_impl->roughnessTextureLocation, m_impl->hasRoughnessTextureLocation);
    bindTexture(item.aoTexture, 5, false, m_impl->aoTextureLocation, m_impl->hasAoTextureLocation);
    bindTexture(item.emissiveTexture, 6, true, m_impl->emissiveTextureLocation, m_impl->hasEmissiveTextureLocation);
    bindTexture(item.packedTexture, 7, false, m_impl->packedTextureLocation, m_impl->hasPackedTextureLocation);
    m_impl->Uniform1i(m_impl->packedLayoutRmaLocation, item.material && item.material->GetPackedTextureLayout() == "RMA" ? 1 : 0);
    const auto irradiance = m_impl->view.environmentIrradiance ? m_impl->view.environmentIrradiance : m_impl->view.environmentTexture;
    const auto prefilter = m_impl->view.environmentPrefilter ? m_impl->view.environmentPrefilter : m_impl->view.environmentTexture;
    bindTexture(irradiance, 11, false, m_impl->environmentIrradianceLocation, -1);
    bindTexture(prefilter, 12, false, m_impl->environmentPrefilterLocation, -1);
    bindTexture(m_impl->view.environmentBrdfLut, 13, false, m_impl->environmentBrdfLocation, -1);
    m_impl->Uniform1i(m_impl->hasEnvironmentLocation, irradiance && prefilter && m_impl->view.environmentBrdfLut && irradiance->IsReady() &&
                                                              prefilter->IsReady() && m_impl->view.environmentBrdfLut->IsReady()
                                                          ? 1
                                                          : 0);
    m_impl->ActiveTexture(GL_TEXTURE0);
    m_impl->BindVertexArray(mesh->vao);
    if (m_impl->indirectDrawSupported)
    {
        const std::uint32_t command[5] = {static_cast<std::uint32_t>(mesh->indexCount), 1, 0, 0, 0};
        m_impl->BindBuffer(0x8F3F, mesh->indirectBuffer);
        m_impl->BufferData(0x8F3F, sizeof(command), command, GL_STREAM_DRAW);
        m_impl->DrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr);
        m_impl->BindBuffer(0x8F3F, 0);
    }
    else
        glDrawElements(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT, nullptr);
    m_impl->BindVertexArray(0);
}

// 只有网格与材质布局兼容的项目才可合批；实例矩阵和上一帧矩阵同时上传以生成运动矢量。
void SdlGlRenderDevice::DrawInstanced(const std::vector<RenderItem> &items)
{
    if (items.empty())
        return;
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (items.front().materialProxy && items.front().materialProxy->legacyShaderPath.empty())
        if (!m_impl->BindCompiledMaterial(items.front(),MaterialPass::Base,MaterialGeometry::Instanced)) return;
    auto *mesh = const_cast<Impl::GpuMesh *>(m_impl->GetOrCreateMesh(items.front().mesh));
    if (!mesh)
        return;
    std::vector<Mat4> models;
    std::vector<Mat4> previousModels;
    models.reserve(items.size());
    previousModels.reserve(items.size());
    for (const auto &item : items)
    {
        models.push_back(item.model);
        previousModels.push_back(item.previousModel);
    }
    m_impl->BindVertexArray(mesh->vao);
    bool gpuCulled = false;
    if (m_impl->computeCullingSupported && m_impl->gpuCullProgram)
    {
        struct CullInput
        {
            Mat4 model;
            Mat4 previousModel;
            float boundsMin[4];
            float boundsMax[4];
        };
        std::vector<CullInput> inputs;
        inputs.reserve(items.size());
        for (const auto &item : items)
        {
            const Vec3 minimum = item.worldBounds.minimum, maximum = item.worldBounds.maximum;
            inputs.push_back({item.model, item.previousModel, {minimum.x, minimum.y, minimum.z, 0}, {maximum.x, maximum.y, maximum.z, 0}});
        }
        const std::uint32_t command[5] = {static_cast<std::uint32_t>(mesh->indexCount), 0, 0, 0, 0};
        m_impl->BindBuffer(0x90D2, mesh->cullInputBuffer);
        m_impl->BufferData(0x90D2, static_cast<GLsizeiptr>(inputs.size() * sizeof(CullInput)), inputs.data(), GL_STREAM_DRAW);
        m_impl->BindBuffer(0x90D2, mesh->instanceBuffer);
        m_impl->BufferData(0x90D2, static_cast<GLsizeiptr>(models.size() * sizeof(Mat4)), nullptr, GL_STREAM_DRAW);
        m_impl->BindBuffer(0x90D2, mesh->previousInstanceBuffer);
        m_impl->BufferData(0x90D2, static_cast<GLsizeiptr>(previousModels.size() * sizeof(Mat4)), nullptr, GL_STREAM_DRAW);
        m_impl->BindBuffer(0x8F3F, mesh->indirectBuffer);
        m_impl->BufferData(0x8F3F, sizeof(command), command, GL_STREAM_DRAW);
        m_impl->BindBufferBase(0x90D2, 0, mesh->cullInputBuffer);
        m_impl->BindBufferBase(0x90D2, 1, mesh->instanceBuffer);
        m_impl->BindBufferBase(0x90D2, 2, mesh->previousInstanceBuffer);
        m_impl->BindBufferBase(0x90D2, 3, mesh->indirectBuffer);
        m_impl->UseProgram(m_impl->gpuCullProgram);
        m_impl->UniformMatrix4fv(m_impl->gpuCullPreviousViewProjectionLocation, 1, GL_FALSE, m_impl->view.previousViewProjection.Data());
        m_impl->Uniform1ui(m_impl->gpuCullInputCountLocation, static_cast<GLuint>(inputs.size()));
        m_impl->Uniform1i(m_impl->gpuCullHzbValidLocation, m_impl->historyValid && m_impl->view.occlusionHistoryValid ? 1 : 0);
        const int hzbCount = static_cast<int>(std::min<std::size_t>(m_impl->hzbTextures.size(), m_impl->gpuCullHzbLocations.size()));
        m_impl->Uniform1i(m_impl->gpuCullHzbCountLocation, hzbCount);
        for (int index = 0; index < hzbCount; ++index)
        {
            m_impl->ActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(index));
            glBindTexture(GL_TEXTURE_2D, m_impl->hzbTextures[index]);
            m_impl->Uniform1i(m_impl->gpuCullHzbLocations[index], index);
        }
        m_impl->DispatchCompute(static_cast<GLuint>((inputs.size() + 63) / 64), 1, 1);
        m_impl->MemoryBarrier(0x00000001 | 0x00000040 | 0x00002000);
        m_impl->UseProgram(m_impl->program);
        for (std::size_t cascade = 0; cascade < FrameView::MaxShadowCascades; ++cascade)
        {
            m_impl->ActiveTexture(GL_TEXTURE0 + 8 + static_cast<GLenum>(cascade));
            glBindTexture(GL_TEXTURE_2D, m_impl->shadowDepths[cascade]);
        }
        gpuCulled = true;
    }
    else
    {
        m_impl->BindBuffer(GL_ARRAY_BUFFER, mesh->instanceBuffer);
        m_impl->BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(models.size() * sizeof(Mat4)), models.data(), GL_DYNAMIC_DRAW);
        m_impl->BindBuffer(GL_ARRAY_BUFFER, mesh->previousInstanceBuffer);
        m_impl->BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(previousModels.size() * sizeof(Mat4)), previousModels.data(),
                           GL_DYNAMIC_DRAW);
    }
    if (items.front().materialProxy && items.front().materialProxy->legacyShaderPath.empty())
    {
        // Compute culling changes program and texture bindings; restore the complete material/frame bindings.
        if (!m_impl->BindCompiledMaterial(items.front(),MaterialPass::Base,MaterialGeometry::Instanced)) { m_impl->BindVertexArray(0); return; }
        m_impl->BindVertexArray(mesh->vao);
        if (gpuCulled) { m_impl->BindBuffer(0x8F3F,mesh->indirectBuffer); m_impl->DrawElementsIndirect(GL_TRIANGLES,GL_UNSIGNED_INT,nullptr); m_impl->BindBuffer(0x8F3F,0); }
        else m_impl->DrawElementsInstanced(GL_TRIANGLES,mesh->indexCount,GL_UNSIGNED_INT,nullptr,static_cast<GLsizei>(models.size()));
        m_impl->BindVertexArray(0); return;
    }
    m_impl->UseProgram(m_impl->program); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
    m_impl->Uniform1i(m_impl->instancedLocation, 1);
    m_impl->UniformMatrix4fv(m_impl->modelLocation, 1, GL_FALSE, Mat4::Identity().Data());
    m_impl->UniformMatrix4fv(m_impl->previousModelLocation, 1, GL_FALSE, Mat4::Identity().Data());
    std::array<Mat4, 64> identityBones;
    for (auto &bone : identityBones)
        bone = Mat4::Identity();
    m_impl->UniformMatrix4fv(m_impl->bonesLocation, static_cast<GLsizei>(identityBones.size()), GL_FALSE, identityBones[0].Data());
    const auto &first = items.front();
    const Vec4 color = first.material ? first.material->GetBaseColor() : Vec4{0.3f, 0.7f, 1.0f, 1.0f};
    const Vec3 emissive = first.material ? first.material->GetEmissive() : Vec3{};
    m_impl->Uniform4f(m_impl->baseColorLocation, color.x, color.y, color.z, color.w);
    m_impl->Uniform1f(m_impl->metallicLocation, first.material ? first.material->GetMetallic() : 0.0f);
    m_impl->Uniform1f(m_impl->roughnessLocation, first.material ? first.material->GetRoughness() : 0.7f);
    m_impl->Uniform3f(m_impl->emissiveLocation, emissive.x, emissive.y, emissive.z);
    m_impl->UploadMaterialParameters(first.material);
    const auto bindTexture = [&](const std::shared_ptr<TextureResource> &resource, const int unit, const bool srgb,
                                 const GLint samplerLocation, const GLint enabledLocation) {
        const GLuint texture = m_impl->GetOrCreateTexture(resource, srgb);
        m_impl->ActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
        m_impl->Uniform1i(samplerLocation, unit);
        m_impl->Uniform1i(enabledLocation, texture != 0 ? 1 : 0);
    };
    bindTexture(first.texture, 0, true, m_impl->textureLocation, m_impl->hasTextureLocation);
    bindTexture(first.normalTexture, 2, false, m_impl->normalTextureLocation, m_impl->hasNormalTextureLocation);
    bindTexture(first.metallicTexture, 3, false, m_impl->metallicTextureLocation, m_impl->hasMetallicTextureLocation);
    bindTexture(first.roughnessTexture, 4, false, m_impl->roughnessTextureLocation, m_impl->hasRoughnessTextureLocation);
    bindTexture(first.aoTexture, 5, false, m_impl->aoTextureLocation, m_impl->hasAoTextureLocation);
    bindTexture(first.emissiveTexture, 6, true, m_impl->emissiveTextureLocation, m_impl->hasEmissiveTextureLocation);
    bindTexture(first.packedTexture, 7, false, m_impl->packedTextureLocation, m_impl->hasPackedTextureLocation);
    m_impl->Uniform1i(m_impl->packedLayoutRmaLocation, first.material && first.material->GetPackedTextureLayout() == "RMA" ? 1 : 0);
    const auto irradiance = m_impl->view.environmentIrradiance ? m_impl->view.environmentIrradiance : m_impl->view.environmentTexture;
    const auto prefilter = m_impl->view.environmentPrefilter ? m_impl->view.environmentPrefilter : m_impl->view.environmentTexture;
    bindTexture(irradiance, 11, false, m_impl->environmentIrradianceLocation, -1);
    bindTexture(prefilter, 12, false, m_impl->environmentPrefilterLocation, -1);
    bindTexture(m_impl->view.environmentBrdfLut, 13, false, m_impl->environmentBrdfLocation, -1);
    m_impl->Uniform1i(m_impl->hasEnvironmentLocation, irradiance && prefilter && m_impl->view.environmentBrdfLut && irradiance->IsReady() &&
                                                              prefilter->IsReady() && m_impl->view.environmentBrdfLut->IsReady()
                                                          ? 1
                                                          : 0);
    if (m_impl->indirectDrawSupported)
    {
        if (!gpuCulled)
        {
            const std::uint32_t command[5] = {static_cast<std::uint32_t>(mesh->indexCount), static_cast<std::uint32_t>(models.size()), 0, 0,
                                              0};
            m_impl->BindBuffer(0x8F3F, mesh->indirectBuffer);
            m_impl->BufferData(0x8F3F, sizeof(command), command, GL_STREAM_DRAW);
        }
        else
            m_impl->BindBuffer(0x8F3F, mesh->indirectBuffer);
        m_impl->DrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr);
        m_impl->BindBuffer(0x8F3F, 0);
    }
    else
        m_impl->DrawElementsInstanced(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(models.size()));
    m_impl->BindVertexArray(0);
    m_impl->ActiveTexture(GL_TEXTURE0);
}

void SdlGlRenderDevice::DrawParticles(const std::vector<Particle> &particles)
{
    if (!m_impl || particles.empty())
        return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_impl->UseProgram(m_impl->particleProgram);
    m_impl->UniformMatrix4fv(m_impl->particleViewLocation, 1, GL_FALSE, m_impl->view.view.Data());
    m_impl->UniformMatrix4fv(m_impl->particleProjectionLocation, 1, GL_FALSE, m_impl->view.projection.Data());
    struct GpuParticleInstance
    {
        float positionSize[4];
        float color[4];
        float uvRect[4];
    };
    std::unordered_map<std::string, std::vector<GpuParticleInstance>> batches;
    for (const auto &particle : particles)
    {
        const float columns = static_cast<float>(std::max<std::uint16_t>(1, particle.flipbookColumns));
        const float rows = static_cast<float>(std::max<std::uint16_t>(1, particle.flipbookRows));
        const float column = static_cast<float>(particle.flipbookFrame % std::max<std::uint16_t>(1, particle.flipbookColumns));
        const float row = static_cast<float>(particle.flipbookFrame / std::max<std::uint16_t>(1, particle.flipbookColumns));
        batches[particle.texturePath].push_back(
            {{particle.position.x, particle.position.y, particle.position.z, std::max(0.001f, particle.size)},
             {particle.color.x, particle.color.y, particle.color.z, particle.color.w},
             {column / columns, row / rows, (column + 1.0f) / columns, (row + 1.0f) / rows}});
    }
    m_impl->BindVertexArray(m_impl->fullScreenVao);
    for (const auto &batch : batches)
    {
        const GLuint texture = batch.first.empty() ? 0 : m_impl->GetOrCreateUiTexture(batch.first);
        m_impl->ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        m_impl->Uniform1i(m_impl->particleTextureLocation, 0);
        m_impl->Uniform1i(m_impl->particleHasTextureLocation, texture ? 1 : 0);
        m_impl->BindBuffer(GL_ARRAY_BUFFER, m_impl->particleInstanceBuffer);
        m_impl->BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch.second.size() * sizeof(GpuParticleInstance)), batch.second.data(),
                           GL_STREAM_DRAW);
        m_impl->DrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(batch.second.size()));
    }
    m_impl->BindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void SdlGlRenderDevice::DrawGpuParticles(const std::vector<GpuParticleEmitter> &emitters)
{
    if (!m_impl || emitters.empty())
        return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_impl->UseProgram(m_impl->gpuParticleProgram);
    m_impl->UniformMatrix4fv(m_impl->gpuParticleViewLocation, 1, GL_FALSE, m_impl->view.view.Data());
    m_impl->UniformMatrix4fv(m_impl->gpuParticleProjectionLocation, 1, GL_FALSE, m_impl->view.projection.Data());
    m_impl->BindVertexArray(m_impl->fullScreenVao);
    for (const auto &emitter : emitters)
    {
        const auto &settings = emitter.settings;
        if (settings.maximumParticles == 0 || settings.emissionRate <= 0.0f)
            continue;
        m_impl->Uniform3f(m_impl->gpuEmitterPositionLocation, emitter.position.x, emitter.position.y, emitter.position.z);
        m_impl->Uniform3f(m_impl->gpuEmitterGravityLocation, settings.gravity.x, settings.gravity.y, settings.gravity.z);
        m_impl->Uniform4f(m_impl->gpuEmitterStartColorLocation, settings.startColor.x, settings.startColor.y, settings.startColor.z,
                          settings.startColor.w);
        m_impl->Uniform4f(m_impl->gpuEmitterEndColorLocation, settings.endColor.x, settings.endColor.y, settings.endColor.z,
                          settings.endColor.w);
        m_impl->Uniform4f(m_impl->gpuEmitterParamsLocation, settings.emissionRate, settings.lifetime, settings.speed, settings.spread);
        m_impl->Uniform4f(m_impl->gpuEmitterFlipbookLocation, static_cast<float>(std::max<std::uint16_t>(1, settings.flipbookColumns)),
                          static_cast<float>(std::max<std::uint16_t>(1, settings.flipbookRows)), settings.startSize, settings.endSize);
        m_impl->Uniform1f(m_impl->gpuEmitterTimeLocation, emitter.elapsedTime);
        m_impl->Uniform1f(m_impl->gpuEmitterCapacityLocation, static_cast<float>(settings.maximumParticles));
        m_impl->Uniform1f(m_impl->gpuEmitterFpsLocation, settings.flipbookFramesPerSecond * (settings.flipbookLooping ? 1.0f : -1.0f));
        const GLuint texture = settings.texturePath.empty() ? 0 : m_impl->GetOrCreateUiTexture(settings.texturePath);
        m_impl->ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        m_impl->Uniform1i(m_impl->gpuParticleTextureLocation, 0);
        m_impl->Uniform1i(m_impl->gpuParticleHasTextureLocation, texture ? 1 : 0);
        m_impl->DrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(std::min<std::size_t>(settings.maximumParticles, 1'000'000)));
    }
    m_impl->BindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// 后处理显式 ping-pong 颜色目标，禁止同一通道同时采样和写入同一纹理造成未定义反馈。
void SdlGlRenderDevice::ExecutePostProcess(const PostProcessPass pass)
{
    if (!m_impl || !m_impl->sceneTargetEnabled || !m_impl->postFramebuffer)
        return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    m_impl->BindVertexArray(m_impl->fullScreenVao);
    const auto attach = [&](const GLuint texture, const int width, const int height, const GLuint framebuffer = 0) {
        m_impl->BindFramebuffer(GL_FRAMEBUFFER, framebuffer ? framebuffer : m_impl->postFramebuffer);
        m_impl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        const GLenum attachment = GL_COLOR_ATTACHMENT0;
        m_impl->DrawBuffers(1, &attachment);
        glViewport(0, 0, width, height);
    };
    const auto bindTexture = [&](const int unit, const GLuint texture, const GLint location) {
        m_impl->ActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, texture);
        m_impl->Uniform1i(location, unit);
    };

    if (pass == PostProcessPass::HierarchicalDepth)
    {
        m_impl->UseProgram(m_impl->hzbProgram);
        int sourceWidth = m_impl->sceneWidth, sourceHeight = m_impl->sceneHeight;
        GLuint source = m_impl->sceneDepth;
        for (std::size_t level = 0; level < m_impl->hzbTextures.size(); ++level)
        {
            const int width = std::max(1, (sourceWidth + 1) / 2), height = std::max(1, (sourceHeight + 1) / 2);
            attach(m_impl->hzbTextures[level], width, height);
            bindTexture(0, source, m_impl->hzbSourceLocation);
            m_impl->Uniform2f(m_impl->hzbSourceTexelLocation, 1.0f / std::max(1, sourceWidth), 1.0f / std::max(1, sourceHeight));
            glDrawArrays(GL_TRIANGLES, 0, 3);
            source = m_impl->hzbTextures[level];
            sourceWidth = width;
            sourceHeight = height;
        }
    }
    else if (pass == PostProcessPass::Gtao)
    {
        const int width = std::max(1, m_impl->sceneWidth / 2), height = std::max(1, m_impl->sceneHeight / 2);
        attach(m_impl->gtaoTexture, width, height);
        if (!m_impl->gtaoEnabled)
        {
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        else
        {
            attach(m_impl->gtaoRawTexture, width, height);
            m_impl->UseProgram(m_impl->gtaoProgram);
            bindTexture(0, m_impl->sceneDepth, m_impl->gtaoDepthLocation);
            m_impl->Uniform2f(m_impl->gtaoInverseResolutionLocation, 1.0f / std::max(1, m_impl->sceneWidth),
                              1.0f / std::max(1, m_impl->sceneHeight));
            const Mat4 inverseProjection = InversePerspective(m_impl->view.projection);
            m_impl->UniformMatrix4fv(m_impl->gtaoInverseProjectionLocation, 1, GL_FALSE, inverseProjection.Data());
            glDrawArrays(GL_TRIANGLES, 0, 3);
            attach(m_impl->gtaoTexture, width, height);
            m_impl->UseProgram(m_impl->gtaoFilterProgram);
            bindTexture(0, m_impl->gtaoRawTexture, m_impl->gtaoFilterSourceLocation);
            bindTexture(1, m_impl->sceneDepth, m_impl->gtaoFilterDepthLocation);
            m_impl->UniformMatrix4fv(m_impl->gtaoFilterInverseProjectionLocation, 1, GL_FALSE, inverseProjection.Data());
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }
    else if (pass == PostProcessPass::Bloom)
    {
        int sourceWidth = m_impl->sceneWidth, sourceHeight = m_impl->sceneHeight;
        GLuint source = m_impl->sceneHdrColor;
        m_impl->UseProgram(m_impl->bloomProgram);
        for (std::size_t level = 0; level < m_impl->bloomTextures.size(); ++level)
        {
            const int width = std::max(1, sourceWidth / 2), height = std::max(1, sourceHeight / 2);
            attach(m_impl->bloomTextures[level], width, height);
            if (!m_impl->bloomEnabled)
            {
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            else
            {
                bindTexture(0, source, m_impl->bloomSourceLocation);
                m_impl->Uniform2f(m_impl->bloomSourceTexelLocation, 1.0f / std::max(1, sourceWidth), 1.0f / std::max(1, sourceHeight));
                m_impl->Uniform1i(m_impl->bloomExtractLocation, level == 0 ? 1 : 0);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            source = m_impl->bloomTextures[level];
            sourceWidth = width;
            sourceHeight = height;
        }
    }
    else if (pass == PostProcessPass::TemporalResolve)
    {
        const int writeIndex = 1 - m_impl->temporalTextureIndex;
        attach(m_impl->taaTextures[writeIndex], m_impl->sceneWidth, m_impl->sceneHeight);
        m_impl->FramebufferTexture2D(GL_FRAMEBUFFER, 0x8CE1, GL_TEXTURE_2D, m_impl->taaDepthTextures[writeIndex], 0);
        const GLenum temporalAttachments[] = {GL_COLOR_ATTACHMENT0, 0x8CE1};
        m_impl->DrawBuffers(2, temporalAttachments);
        m_impl->UseProgram(m_impl->taaProgram);
        bindTexture(0, m_impl->sceneHdrColor, m_impl->taaSceneLocation);
        bindTexture(1, m_impl->sceneVelocity, m_impl->taaVelocityLocation);
        bindTexture(2, m_impl->taaTextures[m_impl->temporalTextureIndex], m_impl->taaHistoryLocation);
        bindTexture(3, m_impl->gtaoTexture, m_impl->taaGtaoLocation);
        bindTexture(11, m_impl->sceneAmbient, m_impl->taaAmbientLocation);
        bindTexture(9, m_impl->sceneDepth, m_impl->taaDepthLocation);
        bindTexture(10, m_impl->taaDepthTextures[m_impl->temporalTextureIndex], m_impl->taaHistoryDepthLocation);
        const Mat4 reprojectDepth = m_impl->view.previousViewProjection * InverseCameraViewProjection(m_impl->view);
        m_impl->UniformMatrix4fv(m_impl->taaReprojectDepthLocation, 1, GL_FALSE, reprojectDepth.Data());
        m_impl->Uniform2f(m_impl->taaDepthRangeLocation, m_impl->view.clusteredLights.nearPlane, m_impl->view.clusteredLights.farPlane);
        for (std::size_t index = 0; index < m_impl->bloomTextures.size(); ++index)
            bindTexture(static_cast<int>(4 + index), m_impl->bloomTextures[index], m_impl->taaBloomLocations[index]);
        m_impl->Uniform2f(m_impl->taaInverseResolutionLocation, 1.0f / std::max(1, m_impl->sceneWidth),
                          1.0f / std::max(1, m_impl->sceneHeight));
        m_impl->Uniform1i(m_impl->taaHistoryValidLocation, m_impl->historyValid ? 1 : 0);
        m_impl->Uniform1i(m_impl->taaEnabledLocation, m_impl->taaEnabled ? 1 : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        m_impl->FramebufferTexture2D(GL_FRAMEBUFFER, 0x8CE1, GL_TEXTURE_2D, 0, 0);
        const GLenum colorAttachment = GL_COLOR_ATTACHMENT0;
        m_impl->DrawBuffers(1, &colorAttachment);
        m_impl->temporalTextureIndex = writeIndex;
        m_impl->historyValid = true;
    }
    else if (pass == PostProcessPass::ToneMap)
    {
        attach(m_impl->sceneColor, m_impl->sceneWidth, m_impl->sceneHeight, m_impl->toneFramebuffer);
        m_impl->UseProgram(m_impl->toneProgram);
        bindTexture(0, m_impl->taaTextures[m_impl->temporalTextureIndex], m_impl->toneResolvedLocation);
        m_impl->Uniform1f(m_impl->exposureLocation, m_impl->exposure);
        m_impl->Uniform2f(m_impl->inverseResolutionLocation, 1.0f / std::max(1, m_impl->sceneWidth),
                          1.0f / std::max(1, m_impl->sceneHeight));
        m_impl->Uniform1i(m_impl->fxaaEnabledLocation, m_impl->fxaaEnabled ? 1 : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    m_impl->BindVertexArray(0);
    m_impl->ActiveTexture(GL_TEXTURE0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

void SdlGlRenderDevice::EndFrame()
{
    if (m_impl->timerActive)
    {
        m_impl->EndQuery(GL_TIME_ELAPSED);
        m_impl->timerIssued[m_impl->timerIndex] = true;
        m_impl->timerIndex = (m_impl->timerIndex + 1) % 2;
        m_impl->timerActive = false;
    }
    if (!m_impl->sceneTargetEnabled)
        return;
    m_impl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    int width = 0, height = 0;
    SDL_GL_GetDrawableSize(m_impl->window, &width, &height);
    glViewport(0, 0, width, height);
    glDepthMask(GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.025f, 0.03f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (m_impl->presentSceneToWindow)
    {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        m_impl->UseProgram(m_impl->presentProgram);
        m_impl->ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_impl->sceneColor);
        m_impl->Uniform1i(m_impl->presentSceneColorLocation, 0);
        m_impl->BindVertexArray(m_impl->fullScreenVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        m_impl->BindVertexArray(0);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
    }
}
void SdlGlRenderDevice::DrawUi(const std::vector<UIDrawCommand> &commands)
{
    if (!m_impl || !m_impl->window)
        return;
    int width = 0, height = 0;
    if (m_impl->sceneTargetEnabled)
    {
        width = m_impl->sceneWidth;
        height = m_impl->sceneHeight;
    }
    else
        SDL_GL_GetDrawableSize(m_impl->window, &width, &height);
    glDisable(GL_DEPTH_TEST);
    // UI 坐标向下递增，三角形朝向与 3D 剔除约定相反，因此绘制时关闭背面剔除。
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    auto appendQuad = [](std::vector<Impl::UiVertex> &vertices, std::vector<std::uint32_t> &indices, const UIRect &rect, const float u0,
                         const float v0, const float u1, const float v1) {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        vertices.push_back({{rect.x, rect.y}, {u0, v0}});
        vertices.push_back({{rect.x + rect.width, rect.y}, {u1, v0}});
        vertices.push_back({{rect.x + rect.width, rect.y + rect.height}, {u1, v1}});
        vertices.push_back({{rect.x, rect.y + rect.height}, {u0, v1}});
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    for (const auto &command : commands)
    {
        if (command.rect.width <= 0.0f || command.rect.height <= 0.0f)
            continue;
        const UIRect clip = command.clipRect.width > 0.0f && command.clipRect.height > 0.0f ? command.clipRect : command.rect;
        glScissor(static_cast<GLint>(clip.x), height - static_cast<GLint>(clip.y + clip.height), static_cast<GLsizei>(clip.width),
                  static_cast<GLsizei>(clip.height));
        if (command.type != UIWidgetType::Label)
        {
            std::vector<Impl::UiVertex> vertices;
            std::vector<std::uint32_t> indices;
            appendQuad(vertices, indices, command.rect, 0.0f, 0.0f, 1.0f, 1.0f);
            const GLuint image = command.type == UIWidgetType::Image ? m_impl->GetOrCreateUiTexture(command.imagePath) : 0;
            m_impl->DrawUiGeometry(vertices, indices, command.color, image, width, height);
        }
        if (!command.text.empty() && command.type != UIWidgetType::Image && command.type != UIWidgetType::Panel &&
            command.type != UIWidgetType::ProgressBar)
        {
            struct TextBatch
            {
                Vec4 color;
                std::vector<Impl::UiVertex> vertices;
                std::vector<std::uint32_t> indices;
            };
            std::vector<TextBatch> batches;
            const float glyphSize = std::max(6.0f, command.fontSize);
            const TextLayoutResult layout =
                TextLayout::Shape(command.text, std::max(0.0f, command.rect.width), {{1.0f, 1.0f, 1.0f, 1.0f}, glyphSize});
            float cursorX = command.rect.x;
            if (command.type == UIWidgetType::Button)
                cursorX += std::max(0.0f, (command.rect.width - layout.width) * 0.5f);
            const float cursorY = command.rect.y + std::max(0.0f, (command.rect.height - layout.height) * 0.5f);
            for (const auto &shaped : layout.glyphs)
            {
                if (shaped.codepoint != ' ')
                {
                    const auto &glyph = m_impl->GetGlyph(shaped.codepoint);
                    const Vec4 base = command.type == UIWidgetType::Button ? Vec4{1.0f, 1.0f, 1.0f, command.color.w} : command.color;
                    const Vec4 color{base.x * shaped.style.color.x, base.y * shaped.style.color.y, base.z * shaped.style.color.z,
                                     base.w * shaped.style.color.w};
                    auto batch = std::find_if(batches.begin(), batches.end(), [&](const TextBatch &candidate) {
                        return candidate.color.x == color.x && candidate.color.y == color.y && candidate.color.z == color.z &&
                               candidate.color.w == color.w;
                    });
                    if (batch == batches.end())
                    {
                        batches.push_back({color});
                        batch = std::prev(batches.end());
                    }
                    appendQuad(batch->vertices, batch->indices,
                               {cursorX + shaped.x, cursorY + shaped.y, std::max(shaped.advance, shaped.style.size * glyph.advance),
                                shaped.style.size * 1.2f},
                               glyph.u0, glyph.v0, glyph.u1, glyph.v1);
                }
            }
            for (const auto &batch : batches)
                m_impl->DrawUiGeometry(batch.vertices, batch.indices, batch.color, m_impl->fontAtlas, width, height);
        }
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}
void SdlGlRenderDevice::Present()
{
    if (m_impl && m_impl->window)
        SDL_GL_SwapWindow(m_impl->window);
}

std::size_t SdlGlRenderDevice::GetResidentGpuBytes() const
{
    return m_impl ? m_impl->GetResidentBytes() : 0;
}

void SdlGlRenderDevice::SetGpuMemoryBudget(const std::size_t bytes)
{
    if (m_impl)
        m_impl->gpuMemoryBudget = std::max<std::size_t>(bytes, 1024 * 1024);
}

// 只淘汰当前帧未使用且无外部引用的缓存对象，避免预算收缩导致正在绘制的资源失效。
void SdlGlRenderDevice::TrimGpuResources()
{
    if (m_impl && m_impl->context)
        m_impl->DestroyCachedResources();
}
void *SdlGlRenderDevice::GetNativeWindow() const
{
    return m_impl ? m_impl->window : nullptr;
}
void *SdlGlRenderDevice::GetGlContext() const
{
    return m_impl ? m_impl->context : nullptr;
}
void SdlGlRenderDevice::SetEventCallback(std::function<void(const void *)> callback)
{
    m_eventCallback = std::move(callback);
}

void SdlGlRenderDevice::SetTextInputEnabled(const bool enabled)
{
    if (!m_impl || !m_impl->window || (SDL_IsTextInputActive() == SDL_TRUE) == enabled)
        return;
    if (enabled)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
}

void SdlGlRenderDevice::SetCloseRequestCallback(std::function<void()> callback)
{
    m_closeRequestCallback = std::move(callback);
}
bool SdlGlRenderDevice::EnableSceneRenderTarget(const bool enabled, const int width, const int height)
{
    if (!m_impl || !m_impl->context)
        return false;
    m_impl->sceneTargetEnabled = enabled;
    m_impl->presentSceneToWindow = !enabled;
    if (!enabled)
    {
        m_impl->DestroySceneTarget();
        return true;
    }
    return m_impl->CreateSceneTarget(width, height);
}
bool SdlGlRenderDevice::ResizeSceneRenderTarget(const int width, const int height)
{
    if (!m_impl || !m_impl->sceneTargetEnabled)
        return false;
    if (m_impl->sceneWidth == width && m_impl->sceneHeight == height)
        return true;
    return m_impl->CreateSceneTarget(width, height);
}
std::uintptr_t SdlGlRenderDevice::GetSceneColorTexture() const
{
    return m_impl ? static_cast<std::uintptr_t>(m_impl->sceneColor) : 0;
}
std::uintptr_t SdlGlRenderDevice::GetUiTexture(const std::string &path)
{
    return m_impl ? static_cast<std::uintptr_t>(m_impl->GetOrCreateUiTexture(path)) : 0;
}
int SdlGlRenderDevice::GetSceneTargetWidth() const
{
    return m_impl ? m_impl->sceneWidth : 0;
}
int SdlGlRenderDevice::GetSceneTargetHeight() const
{
    return m_impl ? m_impl->sceneHeight : 0;
}
bool SdlGlRenderDevice::ReloadShaders()
{
    if (!m_impl || !m_impl->ReloadDefaultShadersFromFiles()) return false;
    m_impl->legacyReflection.clear();
    m_impl->materialPrograms->SetTemplates(ShaderPipeline::LoadMaterialTemplates());
    return true;
}
bool SdlGlRenderDevice::ValidateShader(const std::string &path)
{
    if (!m_impl)
        return false;
    const auto source = ShaderPipeline::Preprocess(path);
    if (!source.success)
    {
        m_impl->lastShaderError = source.error;
        return false;
    }
    std::string extension = std::filesystem::path(path).extension().string();
    const GLenum type = extension == ".vert" ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    const GLuint shader = m_impl->Compile(type, source.source.c_str());
    if (!shader)
        return false;
    m_impl->DeleteShader(shader);
    m_impl->lastShaderError.clear();
    return true;
}
bool SdlGlRenderDevice::ValidateMaterial(const CompiledMaterial &material,std::vector<MaterialDiagnostic> &diagnostics)
{
    if (!m_impl || !m_impl->materialPrograms) return false;
    const auto before=m_impl->materialPrograms->GetDiagnostics().size();
    bool valid=true;
    for (const auto pass:{MaterialPass::Base,MaterialPass::Shadow})
        for (const auto geometry:{MaterialGeometry::Static,MaterialGeometry::Skinned,MaterialGeometry::Instanced})
            valid=m_impl->materialPrograms->GetOrCreate(material,pass,geometry)!=nullptr && valid;
    const auto &all=m_impl->materialPrograms->GetDiagnostics();
    diagnostics.insert(diagnostics.end(),all.begin()+before,all.end());
    if (!valid && diagnostics.empty()) diagnostics.push_back({MaterialDiagnosticSeverity::Error,{},0,-1,"Material GPU validation failed (cached failure)"});
    return valid;
}
void SdlGlRenderDevice::SetExposure(const float exposure)
{
    if (m_impl)
        m_impl->exposure = std::clamp(exposure, 0.05f, 16.0f);
}
float SdlGlRenderDevice::GetExposure() const
{
    return m_impl ? m_impl->exposure : 1.0f;
}
const std::string &SdlGlRenderDevice::GetLastShaderError() const
{
    static const std::string empty;
    return m_impl ? m_impl->lastShaderError : empty;
}
void SdlGlRenderDevice::SetFxaaEnabled(const bool enabled)
{
    if (m_impl)
        m_impl->fxaaEnabled = enabled;
}
bool SdlGlRenderDevice::IsFxaaEnabled() const
{
    return m_impl && m_impl->fxaaEnabled;
}
void SdlGlRenderDevice::SetBloomEnabled(const bool enabled)
{
    if (m_impl)
        m_impl->bloomEnabled = enabled;
}
bool SdlGlRenderDevice::IsBloomEnabled() const
{
    return m_impl && m_impl->bloomEnabled;
}
void SdlGlRenderDevice::SetSsaoEnabled(const bool enabled)
{
    if (m_impl)
        m_impl->gtaoEnabled = enabled;
}
bool SdlGlRenderDevice::IsSsaoEnabled() const
{
    return m_impl && m_impl->gtaoEnabled;
}
void SdlGlRenderDevice::SetGtaoEnabled(const bool enabled)
{
    SetSsaoEnabled(enabled);
}
bool SdlGlRenderDevice::IsGtaoEnabled() const
{
    return IsSsaoEnabled();
}
void SdlGlRenderDevice::SetTaaEnabled(const bool enabled)
{
    if (m_impl)
    {
        m_impl->taaEnabled = enabled;
        if (!enabled)
            m_impl->historyValid = false;
    }
}
bool SdlGlRenderDevice::IsTaaEnabled() const
{
    return m_impl && m_impl->taaEnabled;
}
float SdlGlRenderDevice::GetLastGpuFrameMilliseconds() const
{
    return m_impl ? m_impl->lastGpuFrameMilliseconds : 0.0f;
}
std::size_t SdlGlRenderDevice::GetHzbLevelCount() const
{
    return m_impl ? m_impl->hzbTextures.size() : 0;
}
bool SdlGlRenderDevice::IsIndirectDrawSupported() const
{
    return m_impl && m_impl->indirectDrawSupported;
}
bool SdlGlRenderDevice::IsGpuDrivenCullingSupported() const
{
    return m_impl && m_impl->computeCullingSupported;
}

bool SdlGlRenderDevice::RumbleController(const float lowFrequency, const float highFrequency, const std::uint32_t durationMilliseconds)
{
    if (!m_impl || !m_impl->controller)
        return false;
    const auto toMotor = [](const float value) {
        return static_cast<Uint16>(std::clamp(value, 0.0f, 1.0f) * static_cast<float>(std::numeric_limits<Uint16>::max()));
    };
    return SDL_GameControllerRumble(m_impl->controller, toMotor(lowFrequency), toMotor(highFrequency), durationMilliseconds) == 0;
}

bool SdlGlRenderDevice::IsControllerConnected() const
{
    return m_impl && m_impl->controller && SDL_GameControllerGetAttached(m_impl->controller) == SDL_TRUE;
}
#ifdef TINYENGINE_TEST_HOOKS
std::vector<Vec4> SdlGlRenderDevice::ReadHdrAttachmentForTests(std::size_t attachment) const
{
    if (!m_impl || !m_impl->sceneFramebuffer || attachment > 2) return {};
    GLint previousFramebuffer = 0, previousReadBuffer = 0;
    glGetIntegerv(0x8CA6, &previousFramebuffer);
    glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
    m_impl->BindFramebuffer(GL_FRAMEBUFFER, m_impl->sceneFramebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(attachment));
    std::vector<Vec4> pixels(static_cast<std::size_t>(m_impl->sceneWidth) * m_impl->sceneHeight);
    glReadPixels(0,0,m_impl->sceneWidth,m_impl->sceneHeight,GL_RGBA,GL_FLOAT,pixels.data());
    m_impl->BindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glReadBuffer(static_cast<GLenum>(previousReadBuffer));
    return pixels;
}
std::size_t SdlGlRenderDevice::GetMaterialProgramCompileCountForTests(MaterialPass pass) const
{
    return m_impl && m_impl->materialPrograms ? m_impl->materialPrograms->GetCompileCount(pass) : 0;
}
#endif
} // namespace MiniEngine

#ifdef TINYENGINE_TEST_HOOKS
namespace MiniEngine { const std::vector<MaterialDiagnostic> &SdlGlRenderDevice::GetMaterialDiagnosticsForTests() const { return m_impl->materialPrograms->GetDiagnostics(); } }
#endif
#ifdef TINYENGINE_TEST_HOOKS
namespace MiniEngine {
float SdlGlRenderDevice::ReadShadowDepthForTests(int x,int y,std::size_t cascade) const {
    GLint old=0; glGetIntegerv(GL_FRAMEBUFFER_BINDING,&old);
    m_impl->BindFramebuffer(GL_FRAMEBUFFER,m_impl->shadowFramebuffers[std::min<std::size_t>(cascade,2)]);
    float depth=1; glReadPixels(x,y,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&depth);
    m_impl->BindFramebuffer(GL_FRAMEBUFFER,static_cast<GLuint>(old)); return depth;
} }
#endif
