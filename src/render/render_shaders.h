#pragma once

namespace tinyengine::RenderShaders
{
constexpr const char* kShadowVertexShaderSource = R"GLSL(
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

constexpr const char* kShadowFragmentShaderSource = R"GLSL(
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

constexpr const char* kToneVertexShaderSource = R"GLSL(
            #version 330 core
            out vec2 vUv;
            void main()
            {
                vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                vUv = position;
                gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
            }
        )GLSL";

constexpr const char* kSkyFragmentShaderSource = R"GLSL(
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

constexpr const char* kHzbFragmentShaderSource = R"GLSL(
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

constexpr const char* kGtaoFragmentShaderSource = R"GLSL(
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
constexpr const char* kGtaoFilterFragmentShaderSource = R"GLSL(
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

constexpr const char* kBloomFragmentShaderSource = R"GLSL(
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

constexpr const char* kTaaFragmentShaderSource = R"GLSL(
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

constexpr const char* kFinalToneFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uResolved;
            uniform float uExposure;
            uniform int uViewportRenderMode;
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
                outColor = vec4(pow(uViewportRenderMode == 0 ? AcesFilm(color * uExposure) : max(color,vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
            }
        )GLSL";

constexpr const char* kPresentFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uSceneColor;
            out vec4 outColor;
            void main() { outColor = texture(uSceneColor, vUv); }
        )GLSL";

// 复古像素化：块状采样 + 色阶量化 + 细扫描线。
// 刻意只读取 LDR 最终画面，避免与 TAA/Bloom 的 HDR 历史缓冲耦合。
constexpr const char* kRetroFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uSceneColor;
            uniform vec2 uResolution;
            uniform float uBlockSize;
            uniform float uColorLevels;
            out vec4 outColor;
            void main()
            {
                float block = max(1.0, uBlockSize);
                // 对齐到块中心采样：块内所有像素取同一个源点，得到硬边像素块。
                vec2 blocks = max(vec2(1.0), floor(uResolution / block + 0.5));
                vec2 snapped = (floor(vUv * blocks) + 0.5) / blocks;
                vec3 color = texture(uSceneColor, snapped).rgb;
                // 色阶量化：四舍五入而不是截断，避免整体压暗一档。
                float levels = max(2.0, uColorLevels);
                color = floor(color * levels + 0.5) / levels;
                // 扫描线只压暗每个像素块的上半行，保留块状结构不被糊掉。
                float row = mod(floor(gl_FragCoord.y), block);
                color *= (row < block * 0.5) ? 1.0 : 0.86;
                outColor = vec4(color, 1.0);
            }
        )GLSL";

constexpr const char* kGpuCullComputeShaderSource = R"GLSL(
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

constexpr const char* kUiVertexShaderSource = R"GLSL(
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

constexpr const char* kUiFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform vec4 uColor;
            uniform sampler2D uUiTexture;
            uniform int uHasUiTexture;
            out vec4 outColor;
            void main() { outColor = uHasUiTexture != 0 ? texture(uUiTexture, vUv) * uColor : uColor; }
        )GLSL";

constexpr const char* kParticleVertexShaderSource = R"GLSL(
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

constexpr const char* kParticleFragmentShaderSource = R"GLSL(
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

constexpr const char* kGpuParticleVertexShaderSource = R"GLSL(
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

constexpr const char* kGpuParticleFragmentShaderSource = R"GLSL(
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
} // namespace tinyengine::RenderShaders
