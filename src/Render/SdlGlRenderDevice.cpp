#include "MiniEngine/Render/SdlGlRenderDevice.hpp"

#include "MiniEngine/Platform/Platform.hpp"
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
template <typename T> bool LoadGl(T &target, const char *name)
{
    target = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return target != nullptr;
}

constexpr const char *VertexShaderSource = R"GLSL(
            #version 330 core
            layout(location = 0) in vec3 aPosition;
            layout(location = 1) in vec3 aNormal;
            layout(location = 2) in vec2 aUv;
            layout(location = 4) in vec4 aBoneIndices;
            layout(location = 5) in vec4 aBoneWeights;
            uniform mat4 uBones[64];
            uniform mat4 uMvp;
            uniform mat4 uModel;
            uniform mat4 uLightViewProjection;
            out vec3 vNormal;
            out vec3 vWorldPosition;
            out vec2 vUv;
            out vec4 vLightSpace;
            void main()
            {
                float totalWeight = dot(aBoneWeights, vec4(1.0));
                mat4 skin = mat4(1.0);
                if (totalWeight > 0.0001) skin = uBones[int(aBoneIndices.x)] * aBoneWeights.x + uBones[int(aBoneIndices.y)] * aBoneWeights.y + uBones[int(aBoneIndices.z)] * aBoneWeights.z + uBones[int(aBoneIndices.w)] * aBoneWeights.w;
                gl_Position = uMvp * skin * vec4(aPosition, 1.0);
                vNormal = normalize(mat3(uModel) * aNormal);
                vWorldPosition = vec3(uModel * vec4(aPosition, 1.0));
                vLightSpace = uLightViewProjection * vec4(vWorldPosition, 1.0);
                vUv = aUv;
            }
        )GLSL";

constexpr const char *FragmentShaderSource = R"GLSL(
            #version 330 core
            in vec3 vNormal;
            in vec3 vWorldPosition;
            in vec2 vUv;
            in vec4 vLightSpace;
            uniform vec4 uBaseColor;
            uniform float uMetallic;
            uniform float uRoughness;
            uniform vec3 uEmissive;
            uniform vec3 uCameraPosition;
            uniform vec3 uLightDirection;
            uniform vec3 uLightColor;
            uniform float uLightIntensity;
            uniform sampler2D uTexture;
            uniform int uHasTexture;
            uniform sampler2D uShadowMap;
            out vec4 outColor;
            float DistributionGGX(vec3 normal, vec3 halfway, float roughness)
            {
                float a = roughness * roughness; float a2 = a * a;
                float nh = max(dot(normal, halfway), 0.0); float nh2 = nh * nh;
                float denominator = nh2 * (a2 - 1.0) + 1.0;
                return a2 / max(3.14159265 * denominator * denominator, 0.0001);
            }
            float GeometrySchlickGGX(float nv, float roughness)
            {
                float r = roughness + 1.0; float k = r * r / 8.0;
                return nv / max(nv * (1.0 - k) + k, 0.0001);
            }
            vec3 FresnelSchlick(float cosine, vec3 f0)
            {
                return f0 + (1.0 - f0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
            }
            float ShadowFactor(vec3 normal, vec3 light)
            {
                vec3 projected = vLightSpace.xyz / max(vLightSpace.w, 0.0001);
                projected = projected * 0.5 + 0.5;
                if (projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 || projected.y > 1.0) return 0.0;
                float bias = max(0.0025 * (1.0 - dot(normal, light)), 0.0005);
                vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0)); float shadow = 0.0;
                for (int x = -1; x <= 1; ++x) for (int y = -1; y <= 1; ++y)
                    shadow += projected.z - bias > texture(uShadowMap, projected.xy + vec2(x, y) * texel).r ? 1.0 : 0.0;
                return shadow / 9.0;
            }
            void main()
            {
                vec4 surface = uHasTexture != 0 ? texture(uTexture, vUv) : vec4(1.0);
                vec3 albedo = uBaseColor.rgb * surface.rgb;
                vec3 normal = normalize(vNormal); vec3 view = normalize(uCameraPosition - vWorldPosition);
                vec3 light = normalize(-uLightDirection); vec3 halfway = normalize(view + light);
                float nl = max(dot(normal, light), 0.0); float nv = max(dot(normal, view), 0.0);
                vec3 f0 = mix(vec3(0.04), albedo, uMetallic);
                vec3 fresnel = FresnelSchlick(max(dot(halfway, view), 0.0), f0);
                float distribution = DistributionGGX(normal, halfway, uRoughness);
                float geometry = GeometrySchlickGGX(nv, uRoughness) * GeometrySchlickGGX(nl, uRoughness);
                vec3 specular = distribution * geometry * fresnel / max(4.0 * nv * nl, 0.0001);
                vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - uMetallic) * albedo / 3.14159265;
                float shadow = ShadowFactor(normal, light);
                vec3 direct = (diffuse + specular) * uLightColor * uLightIntensity * nl * (1.0 - shadow);
                vec3 color = albedo * 0.04 + direct + uEmissive;
                outColor = vec4(color, uBaseColor.a * surface.a);
            }
        )GLSL";

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

constexpr const char *ToneFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uHdrTexture;
            uniform sampler2D uDepthTexture;
            uniform sampler2D uHistoryTexture;
            uniform float uExposure;
            uniform vec2 uInverseResolution;
            uniform int uFxaaEnabled;
            uniform int uBloomEnabled;
            uniform int uSsaoEnabled;
            uniform int uTaaEnabled;
            uniform int uHistoryValid;
            out vec4 outColor;
            vec3 AcesFilm(vec3 color)
            {
                return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
            }
            vec3 SceneColor(vec2 uv)
            {
                vec4 scene = texture(uHdrTexture, uv);
                vec3 sky = mix(vec3(0.035, 0.055, 0.09), vec3(0.22, 0.42, 0.72), clamp(uv.y, 0.0, 1.0));
                return mix(sky, scene.rgb, scene.a);
            }
            void main()
            {
                vec3 hdr = SceneColor(vUv);
                if (uTaaEnabled != 0 && uHistoryValid != 0)
                {
                    vec3 history = texture(uHistoryTexture, vUv).rgb;
                    vec3 minimumColor = min(hdr, min(SceneColor(vUv + vec2(uInverseResolution.x, 0.0)), SceneColor(vUv - vec2(uInverseResolution.x, 0.0))));
                    vec3 maximumColor = max(hdr, max(SceneColor(vUv + vec2(uInverseResolution.x, 0.0)), SceneColor(vUv - vec2(uInverseResolution.x, 0.0))));
                    hdr = mix(hdr, clamp(history, minimumColor, maximumColor), 0.88);
                }
                if (uSsaoEnabled != 0)
                {
                    float centerDepth = texture(uDepthTexture, vUv).r;
                    float occlusion = 0.0;
                    for (int x = -2; x <= 2; ++x)
                        for (int y = -2; y <= 2; ++y)
                        {
                            float sampleDepth = texture(uDepthTexture, vUv + vec2(x, y) * uInverseResolution * 2.0).r;
                            occlusion += sampleDepth + 0.0008 < centerDepth ? 1.0 : 0.0;
                        }
                    hdr *= 1.0 - occlusion / 25.0 * 0.42;
                }
                if (uBloomEnabled != 0)
                {
                    vec3 bloom = vec3(0.0);
                    float weights[5] = float[5](0.22, 0.19, 0.12, 0.07, 0.035);
                    for (int offset = -4; offset <= 4; ++offset)
                    {
                        vec3 horizontal = SceneColor(vUv + vec2(offset, 0.0) * uInverseResolution * 2.0);
                        vec3 vertical = SceneColor(vUv + vec2(0.0, offset) * uInverseResolution * 2.0);
                        bloom += (max(horizontal - vec3(1.0), vec3(0.0)) + max(vertical - vec3(1.0), vec3(0.0))) * weights[abs(offset)];
                    }
                    hdr += bloom * 0.28;
                }
                if (uFxaaEnabled != 0)
                {
                    vec3 northWest = SceneColor(vUv + vec2(-1.0, -1.0) * uInverseResolution);
                    vec3 northEast = SceneColor(vUv + vec2(1.0, -1.0) * uInverseResolution);
                    vec3 southWest = SceneColor(vUv + vec2(-1.0, 1.0) * uInverseResolution);
                    vec3 southEast = SceneColor(vUv + vec2(1.0, 1.0) * uInverseResolution);
                    vec3 lumaWeights = vec3(0.299, 0.587, 0.114);
                    float centerLuma = dot(hdr, lumaWeights), minimumLuma = min(centerLuma, min(min(dot(northWest, lumaWeights), dot(northEast, lumaWeights)), min(dot(southWest, lumaWeights), dot(southEast, lumaWeights))));
                    float maximumLuma = max(centerLuma, max(max(dot(northWest, lumaWeights), dot(northEast, lumaWeights)), max(dot(southWest, lumaWeights), dot(southEast, lumaWeights))));
                    if (maximumLuma - minimumLuma > max(0.0312, maximumLuma * 0.125)) hdr = (northWest + northEast + southWest + southEast + hdr * 2.0) / 6.0;
                }
                hdr *= uExposure;
                outColor = vec4(pow(AcesFilm(hdr), vec3(1.0 / 2.2)), 1.0);
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
            uniform mat4 uParticleView;
            uniform mat4 uParticleProjection;
            uniform vec3 uParticlePosition;
            uniform float uParticleSize;
            out vec2 vParticleUv;
            void main()
            {
                vec2 corners[6] = vec2[6](vec2(-1,-1), vec2(1,-1), vec2(1,1), vec2(-1,-1), vec2(1,1), vec2(-1,1));
                vec2 corner = corners[gl_VertexID];
                mat3 cameraRotation = transpose(mat3(uParticleView));
                vec3 world = uParticlePosition + (cameraRotation[0] * corner.x + cameraRotation[1] * corner.y) * uParticleSize;
                gl_Position = uParticleProjection * uParticleView * vec4(world, 1.0);
                vParticleUv = corner * 0.5 + 0.5;
            }
        )GLSL";

constexpr const char *ParticleFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vParticleUv;
            uniform vec4 uParticleColor;
            out vec4 outColor;
            void main()
            {
                float distanceToCenter = length(vParticleUv - vec2(0.5));
                if (distanceToCenter > 0.5) discard;
                outColor = vec4(uParticleColor.rgb, uParticleColor.a * smoothstep(0.5, 0.35, distanceToCenter));
            }
        )GLSL";
} // namespace

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
    using Uniform2fProc = void(APIENTRY *)(GLint, GLfloat, GLfloat);
    using Uniform1fProc = void(APIENTRY *)(GLint, GLfloat);
    using Uniform1iProc = void(APIENTRY *)(GLint, GLint);
    using ActiveTextureProc = void(APIENTRY *)(GLenum);
    using GenerateMipmapProc = void(APIENTRY *)(GLenum);
    using CompressedTexImage2DProc = void(APIENTRY *)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *);
    using GenFramebuffersProc = void(APIENTRY *)(GLsizei, GLuint *);
    using BindFramebufferProc = void(APIENTRY *)(GLenum, GLuint);
    using DeleteFramebuffersProc = void(APIENTRY *)(GLsizei, const GLuint *);
    using FramebufferTexture2DProc = void(APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
    using CheckFramebufferStatusProc = GLenum(APIENTRY *)(GLenum);
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
    GLuint program = 0, shadowProgram = 0, toneProgram = 0, uiProgram = 0, particleProgram = 0, fullScreenVao = 0;
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
    GLuint sceneFramebuffer = 0, toneFramebuffer = 0, sceneHdrColor = 0, sceneColor = 0, sceneDepth = 0, historyHdrColor = 0;
    std::array<GLuint, FrameView::MaxShadowCascades> shadowFramebuffers{}, shadowDepths{};
    int shadowResolution = 2048;
    std::size_t activeShadowCascade = 0;
    float exposure = 1.0f;
    bool fxaaEnabled = true;
    bool bloomEnabled = true, ssaoEnabled = true, taaEnabled = true, historyValid = false;
    float maximumAnisotropy = 1.0f;
    GLuint timerQueries[2]{};
    bool timerIssued[2]{};
    int timerIndex = 0;
    bool timerActive = false;
    float lastGpuFrameMilliseconds = 0.0f;
    int sceneWidth = 0, sceneHeight = 0;
    bool sceneTargetEnabled = false;
    bool previousUiNext = false, previousUiAccept = false;
    SDL_GameController *controller = nullptr;
    GLint mvpLocation = -1, modelLocation = -1, viewProjectionLocation = -1, viewLocation = -1, instancedLocation = -1,
          baseColorLocation = -1;
    std::array<GLint, FrameView::MaxShadowCascades> shadowViewProjectionLocations{}, shadowCascadeSplitLocations{}, shadowMapLocations{};
    GLint bonesLocation = -1;
    GLint metallicLocation = -1, roughnessLocation = -1, emissiveLocation = -1, cameraPositionLocation = -1;
    GLint lightDirectionLocation = -1, lightColorLocation = -1, lightIntensityLocation = -1;
    GLint textureLocation = -1, hasTextureLocation = -1;
    GLint lightCountLocation = -1;
    std::array<GLint, FrameView::MaxLights> lightDirectionsLocations{}, lightColorsLocations{}, lightIntensitiesLocations{};
    GLint normalTextureLocation = -1, hasNormalTextureLocation = -1, metallicTextureLocation = -1, hasMetallicTextureLocation = -1;
    GLint roughnessTextureLocation = -1, hasRoughnessTextureLocation = -1, aoTextureLocation = -1, hasAoTextureLocation = -1;
    GLint emissiveTextureLocation = -1, hasEmissiveTextureLocation = -1;
    GLint packedTextureLocation = -1, hasPackedTextureLocation = -1, packedLayoutRmaLocation = -1;
    GLint environmentIrradianceLocation = -1, environmentPrefilterLocation = -1, environmentBrdfLocation = -1, hasEnvironmentLocation = -1;
    GLint shadowMvpLocation = -1, shadowLightViewProjectionLocation = -1, shadowInstancedLocation = -1, shadowAlphaTextureLocation = -1,
          shadowHasAlphaTextureLocation = -1, shadowOpacityLocation = -1;
    GLint toneTextureLocation = -1, toneDepthTextureLocation = -1, toneHistoryTextureLocation = -1, exposureLocation = -1,
          inverseResolutionLocation = -1, fxaaEnabledLocation = -1, bloomEnabledLocation = -1, ssaoEnabledLocation = -1,
          taaEnabledLocation = -1, historyValidLocation = -1;
    GLint uiViewportLocation = -1, uiColorLocation = -1, uiTextureLocation = -1, uiHasTextureLocation = -1;
    GLint particleViewLocation = -1, particleProjectionLocation = -1, particlePositionLocation = -1, particleSizeLocation = -1,
          particleColorLocation = -1;
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
    Uniform2fProc Uniform2f = nullptr;
    Uniform1fProc Uniform1f = nullptr;
    Uniform1iProc Uniform1i = nullptr;
    ActiveTextureProc ActiveTexture = nullptr;
    GenerateMipmapProc GenerateMipmap = nullptr;
    CompressedTexImage2DProc CompressedTexImage2D = nullptr;
    GenFramebuffersProc GenFramebuffers = nullptr;
    BindFramebufferProc BindFramebuffer = nullptr;
    DeleteFramebuffersProc DeleteFramebuffers = nullptr;
    FramebufferTexture2DProc FramebufferTexture2D = nullptr;
    CheckFramebufferStatusProc CheckFramebufferStatus = nullptr;
    GenQueriesProc GenQueries = nullptr;
    DeleteQueriesProc DeleteQueries = nullptr;
    BeginQueryProc BeginQuery = nullptr;
    EndQueryProc EndQuery = nullptr;
    GetQueryObjectivProc GetQueryObjectiv = nullptr;
    GetQueryObjectui64vProc GetQueryObjectui64v = nullptr;

    struct GpuMesh
    {
        GLuint vao = 0, vertexBuffer = 0, indexBuffer = 0, instanceBuffer = 0;
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
        return LoadGl(GenVertexArrays, "glGenVertexArrays") && LoadGl(BindVertexArray, "glBindVertexArray") &&
               LoadGl(DeleteVertexArrays, "glDeleteVertexArrays") && LoadGl(GenBuffers, "glGenBuffers") &&
               LoadGl(BindBuffer, "glBindBuffer") && LoadGl(BufferData, "glBufferData") && LoadGl(DeleteBuffers, "glDeleteBuffers") &&
               LoadGl(VertexAttribPointer, "glVertexAttribPointer") && LoadGl(EnableVertexAttribArray, "glEnableVertexAttribArray") &&
               LoadGl(VertexAttribDivisor, "glVertexAttribDivisor") && LoadGl(DrawElementsInstanced, "glDrawElementsInstanced") &&
               LoadGl(CreateShader, "glCreateShader") && LoadGl(ShaderSource, "glShaderSource") &&
               LoadGl(CompileShader, "glCompileShader") && LoadGl(GetShaderiv, "glGetShaderiv") &&
               LoadGl(GetShaderInfoLog, "glGetShaderInfoLog") && LoadGl(DeleteShader, "glDeleteShader") &&
               LoadGl(CreateProgram, "glCreateProgram") && LoadGl(AttachShader, "glAttachShader") && LoadGl(LinkProgram, "glLinkProgram") &&
               LoadGl(GetProgramiv, "glGetProgramiv") && LoadGl(GetProgramInfoLog, "glGetProgramInfoLog") &&
               LoadGl(DeleteProgram, "glDeleteProgram") && LoadGl(UseProgram, "glUseProgram") &&
               LoadGl(GetUniformLocation, "glGetUniformLocation") && LoadGl(UniformMatrix4fv, "glUniformMatrix4fv") &&
               LoadGl(Uniform4f, "glUniform4f") && LoadGl(Uniform3f, "glUniform3f") && LoadGl(Uniform2f, "glUniform2f") &&
               LoadGl(Uniform1f, "glUniform1f") && LoadGl(Uniform1i, "glUniform1i") && LoadGl(ActiveTexture, "glActiveTexture") &&
               LoadGl(GenerateMipmap, "glGenerateMipmap") && LoadGl(CompressedTexImage2D, "glCompressedTexImage2D") &&
               LoadGl(GenFramebuffers, "glGenFramebuffers") && LoadGl(BindFramebuffer, "glBindFramebuffer") &&
               LoadGl(DeleteFramebuffers, "glDeleteFramebuffers") && LoadGl(FramebufferTexture2D, "glFramebufferTexture2D") &&
               LoadGl(CheckFramebufferStatus, "glCheckFramebufferStatus") && LoadGl(GenQueries, "glGenQueries") &&
               LoadGl(DeleteQueries, "glDeleteQueries") && LoadGl(BeginQuery, "glBeginQuery") && LoadGl(EndQuery, "glEndQuery") &&
               LoadGl(GetQueryObjectiv, "glGetQueryObjectiv") && LoadGl(GetQueryObjectui64v, "glGetQueryObjectui64v");
    }

    void DestroySceneTarget()
    {
        if (sceneHdrColor)
            glDeleteTextures(1, &sceneHdrColor);
        if (sceneColor)
            glDeleteTextures(1, &sceneColor);
        if (sceneDepth)
            glDeleteTextures(1, &sceneDepth);
        if (historyHdrColor)
            glDeleteTextures(1, &historyHdrColor);
        if (sceneFramebuffer)
            DeleteFramebuffers(1, &sceneFramebuffer);
        if (toneFramebuffer)
            DeleteFramebuffers(1, &toneFramebuffer);
        sceneHdrColor = sceneColor = sceneDepth = historyHdrColor = sceneFramebuffer = toneFramebuffer = 0;
        historyValid = false;
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
        glGenTextures(1, &historyHdrColor);
        glBindTexture(GL_TEXTURE_2D, historyHdrColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
        viewProjectionLocation = GetUniformLocation(program, "uViewProjection");
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
        lightDirectionLocation = GetUniformLocation(program, "uLightDirection");
        lightColorLocation = GetUniformLocation(program, "uLightColor");
        lightIntensityLocation = GetUniformLocation(program, "uLightIntensity");
        lightCountLocation = GetUniformLocation(program, "uLightCount");
        for (std::size_t index = 0; index < FrameView::MaxLights; ++index)
        {
            const std::string suffix = "[" + std::to_string(index) + "]";
            lightDirectionsLocations[index] = GetUniformLocation(program, ("uLightDirections" + suffix).c_str());
            lightColorsLocations[index] = GetUniformLocation(program, ("uLightColors" + suffix).c_str());
            lightIntensitiesLocations[index] = GetUniformLocation(program, ("uLightIntensities" + suffix).c_str());
        }
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

    void UploadMaterialParameters(const std::shared_ptr<MaterialResource> &material)
    {
        if (!material || material->GetShaderPath().empty())
            return;
        const auto reflected = ShaderPipeline::ReflectParameters(material->GetShaderPath());
        for (const auto &parameter : reflected)
        {
            const auto value = material->GetParameters().find(parameter.name);
            if (value == material->GetParameters().end() || parameter.type == ShaderParameterType::Texture)
                continue;
            const GLint location = GetUniformLocation(program, parameter.name.c_str());
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
        toneProgram = CreateLinkedProgram(ToneVertexShaderSource, ToneFragmentShaderSource);
        uiProgram = CreateLinkedProgram(UiVertexShaderSource, UiFragmentShaderSource);
        particleProgram = CreateLinkedProgram(ParticleVertexShaderSource, ParticleFragmentShaderSource);
        if (!shadowProgram || !toneProgram || !uiProgram || !particleProgram)
            return false;
        shadowMvpLocation = GetUniformLocation(shadowProgram, "uLightMvp");
        shadowLightViewProjectionLocation = GetUniformLocation(shadowProgram, "uLightViewProjection");
        shadowInstancedLocation = GetUniformLocation(shadowProgram, "uInstanced");
        shadowAlphaTextureLocation = GetUniformLocation(shadowProgram, "uShadowAlphaTexture");
        shadowHasAlphaTextureLocation = GetUniformLocation(shadowProgram, "uShadowHasAlphaTexture");
        shadowOpacityLocation = GetUniformLocation(shadowProgram, "uShadowOpacity");
        toneTextureLocation = GetUniformLocation(toneProgram, "uHdrTexture");
        toneDepthTextureLocation = GetUniformLocation(toneProgram, "uDepthTexture");
        toneHistoryTextureLocation = GetUniformLocation(toneProgram, "uHistoryTexture");
        exposureLocation = GetUniformLocation(toneProgram, "uExposure");
        inverseResolutionLocation = GetUniformLocation(toneProgram, "uInverseResolution");
        fxaaEnabledLocation = GetUniformLocation(toneProgram, "uFxaaEnabled");
        bloomEnabledLocation = GetUniformLocation(toneProgram, "uBloomEnabled");
        ssaoEnabledLocation = GetUniformLocation(toneProgram, "uSsaoEnabled");
        taaEnabledLocation = GetUniformLocation(toneProgram, "uTaaEnabled");
        historyValidLocation = GetUniformLocation(toneProgram, "uHistoryValid");
        GenVertexArrays(1, &fullScreenVao);
        uiViewportLocation = GetUniformLocation(uiProgram, "uViewport");
        uiColorLocation = GetUniformLocation(uiProgram, "uColor");
        uiTextureLocation = GetUniformLocation(uiProgram, "uUiTexture");
        uiHasTextureLocation = GetUniformLocation(uiProgram, "uHasUiTexture");
        particleViewLocation = GetUniformLocation(particleProgram, "uParticleView");
        particleProjectionLocation = GetUniformLocation(particleProgram, "uParticleProjection");
        particlePositionLocation = GetUniformLocation(particleProgram, "uParticlePosition");
        particleSizeLocation = GetUniformLocation(particleProgram, "uParticleSize");
        particleColorLocation = GetUniformLocation(particleProgram, "uParticleColor");
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
        GenBuffers(1, &mesh.instanceBuffer);
        BindBuffer(GL_ARRAY_BUFFER, mesh.instanceBuffer);
        BufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        for (GLuint column = 0; column < 4; ++column)
        {
            VertexAttribPointer(8 + column, 4, GL_FLOAT, GL_FALSE, sizeof(Mat4),
                                reinterpret_cast<void *>(static_cast<std::uintptr_t>(column * sizeof(float) * 4)));
            EnableVertexAttribArray(8 + column);
            VertexAttribDivisor(8 + column, 1);
        }
        BindVertexArray(0);
        mesh.indexCount = static_cast<GLsizei>(indices.size());
        mesh.byteSize = requiredBytes;
        return &meshes.emplace(resource->GetPath(), mesh).first->second;
    }

    GLuint GetOrCreateTexture(const std::shared_ptr<TextureResource> &resource, const bool srgb)
    {
        if (!resource || !resource->IsReady())
            return 0;
        const std::string key = resource->GetPath() + (srgb ? "#srgb" : "#linear");
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
                format = srgb || resource->IsSrgbEncoded() ? 0x8C4D : 0x83F1;
                break;
            case TextureResource::Compression::BC2:
                format = srgb || resource->IsSrgbEncoded() ? 0x8C4E : 0x83F2;
                break;
            case TextureResource::Compression::BC3:
                format = srgb || resource->IsSrgbEncoded() ? 0x8C4F : 0x83F3;
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
                format = srgb || resource->IsSrgbEncoded() ? 0x8E8D : 0x8E8C;
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

bool SdlGlRenderDevice::Initialize()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0)
        return false;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    m_impl->window = SDL_CreateWindow(m_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_width, m_height,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!m_impl->window)
    {
        SDL_Quit();
        return false;
    }
    m_impl->context = SDL_GL_CreateContext(m_impl->window);
    if (!m_impl->context || !m_impl->LoadFunctions())
    {
        Shutdown();
        return false;
    }
    const char *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    if (extensions && std::strstr(extensions, "GL_EXT_texture_filter_anisotropic"))
        glGetFloatv(0x84FF, &m_impl->maximumAnisotropy);
    const auto vertexSource = ShaderPipeline::Preprocess(m_impl->vertexShaderPath),
               fragmentSource = ShaderPipeline::Preprocess(m_impl->fragmentShaderPath);
    const bool programCreated = vertexSource.success && fragmentSource.success
                                    ? m_impl->CreateDefaultProgram(vertexSource.source.c_str(), fragmentSource.source.c_str())
                                    : m_impl->CreateDefaultProgram();
    if (!programCreated || !m_impl->CreateAuxiliaryPrograms() || !m_impl->CreateShadowTarget())
    {
        Shutdown();
        return false;
    }
    m_impl->GenQueries(2, m_impl->timerQueries);
    m_impl->vertexShaderWatcher.HasChanged(m_impl->vertexShaderPath);
    m_impl->fragmentShaderWatcher.HasChanged(m_impl->fragmentShaderPath);
    SDL_GL_SetSwapInterval(1);
    SDL_StartTextInput();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    int drawableWidth = 0, drawableHeight = 0;
    SDL_GL_GetDrawableSize(m_impl->window, &drawableWidth, &drawableHeight);
    if (drawableHeight > 0)
        m_aspect = static_cast<float>(drawableWidth) / static_cast<float>(drawableHeight);
    return true;
}

void SdlGlRenderDevice::Shutdown()
{
    if (!m_impl)
        return;
    if (m_impl->context)
    {
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
        if (m_impl->toneProgram)
        {
            m_impl->DeleteProgram(m_impl->toneProgram);
            m_impl->toneProgram = 0;
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
        if (m_impl->fullScreenVao)
        {
            m_impl->DeleteVertexArrays(1, &m_impl->fullScreenVao);
            m_impl->fullScreenVao = 0;
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
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (m_eventCallback)
            m_eventCallback(&event);
        if (event.type == SDL_QUIT)
            return false;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        {
            glViewport(0, 0, event.window.data1, event.window.data2);
            if (event.window.data2 > 0)
                m_aspect = static_cast<float>(event.window.data1) / static_cast<float>(event.window.data2);
        }
        if (event.type == SDL_MOUSEMOTION)
        {
            m_impl->input.mouseX += static_cast<float>(event.motion.xrel);
            m_impl->input.mouseY += static_cast<float>(event.motion.yrel);
        }
        if (event.type == SDL_TEXTINPUT)
            m_impl->input.textInput += event.text.text;
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
    const auto axis = [keys](SDL_Scancode negative, SDL_Scancode positive) {
        return (keys[positive] ? 1.0f : 0.0f) - (keys[negative] ? 1.0f : 0.0f);
    };
    m_impl->input.moveX = axis(SDL_SCANCODE_A, SDL_SCANCODE_D);
    m_impl->input.moveY = axis(SDL_SCANCODE_F, SDL_SCANCODE_R);
    m_impl->input.moveZ = axis(SDL_SCANCODE_S, SDL_SCANCODE_W);
    m_impl->input.rotateX = axis(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    m_impl->input.rotateY = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
    m_impl->input.rotateZ = axis(SDL_SCANCODE_Q, SDL_SCANCODE_E);
    m_impl->input.scale = axis(SDL_SCANCODE_X, SDL_SCANCODE_Z);
    m_impl->input.sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    m_impl->input.jump = keys[SDL_SCANCODE_SPACE] != 0;
    m_impl->input.primaryAction = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    int cursorX = 0, cursorY = 0;
    SDL_GetMouseState(&cursorX, &cursorY);
    m_impl->input.cursorX = static_cast<float>(cursorX);
    m_impl->input.cursorY = static_cast<float>(cursorY);
    m_impl->input.quitRequested = keys[SDL_SCANCODE_ESCAPE] != 0;
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
    const bool uiNext = keys[SDL_SCANCODE_TAB] != 0 || controllerNext,
               uiAccept = keys[SDL_SCANCODE_RETURN] != 0 || keys[SDL_SCANCODE_SPACE] != 0 || controllerAccept;
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

void SdlGlRenderDevice::BeginShadowPass(const FrameView &view, const std::size_t cascadeIndex)
{
    m_impl->view = view;
    m_impl->activeShadowCascade = std::min(cascadeIndex, FrameView::MaxShadowCascades - 1);
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

void SdlGlRenderDevice::BeginFrame(const FrameView &view)
{
    m_impl->view = view;
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
    m_impl->UniformMatrix4fv(m_impl->viewLocation, 1, GL_FALSE, view.view.Data());
    for (std::size_t cascade = 0; cascade < FrameView::MaxShadowCascades; ++cascade)
    {
        m_impl->UniformMatrix4fv(m_impl->shadowViewProjectionLocations[cascade], 1, GL_FALSE, view.shadowViewProjections[cascade].Data());
        m_impl->Uniform1f(m_impl->shadowCascadeSplitLocations[cascade], view.shadowCascadeSplits[cascade]);
        m_impl->Uniform1i(m_impl->shadowMapLocations[cascade], static_cast<GLint>(8 + cascade));
        m_impl->ActiveTexture(GL_TEXTURE0 + 8 + static_cast<GLenum>(cascade));
        glBindTexture(GL_TEXTURE_2D, m_impl->shadowDepths[cascade]);
    }
    m_impl->Uniform3f(m_impl->lightDirectionLocation, view.lightDirection.x, view.lightDirection.y, view.lightDirection.z);
    m_impl->Uniform3f(m_impl->lightColorLocation, view.lightColor.x, view.lightColor.y, view.lightColor.z);
    m_impl->Uniform1f(m_impl->lightIntensityLocation, view.lightIntensity);
    m_impl->Uniform1i(m_impl->lightCountLocation, view.lightCount);
    for (int index = 0; index < view.lightCount && index < static_cast<int>(FrameView::MaxLights); ++index)
    {
        m_impl->Uniform3f(m_impl->lightDirectionsLocations[index], view.lightDirections[index].x, view.lightDirections[index].y,
                          view.lightDirections[index].z);
        m_impl->Uniform3f(m_impl->lightColorsLocations[index], view.lightColors[index].x, view.lightColors[index].y,
                          view.lightColors[index].z);
        m_impl->Uniform1f(m_impl->lightIntensitiesLocations[index], view.lightIntensities[index]);
    }
    m_impl->Uniform3f(m_impl->cameraPositionLocation, view.cameraPosition.x, view.cameraPosition.y, view.cameraPosition.z);
    m_impl->Uniform1i(m_impl->textureLocation, 0);
    m_impl->ActiveTexture(GL_TEXTURE0);
}

void SdlGlRenderDevice::Draw(const RenderItem &item)
{
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
    bindTexture(item.packedTexture, 13, false, m_impl->packedTextureLocation, m_impl->hasPackedTextureLocation);
    m_impl->Uniform1i(m_impl->packedLayoutRmaLocation, item.material && item.material->GetPackedTextureLayout() == "RMA" ? 1 : 0);
    const auto irradiance = m_impl->view.environmentIrradiance ? m_impl->view.environmentIrradiance : m_impl->view.environmentTexture;
    const auto prefilter = m_impl->view.environmentPrefilter ? m_impl->view.environmentPrefilter : m_impl->view.environmentTexture;
    bindTexture(irradiance, 7, false, m_impl->environmentIrradianceLocation, -1);
    bindTexture(prefilter, 11, false, m_impl->environmentPrefilterLocation, -1);
    bindTexture(m_impl->view.environmentBrdfLut, 12, false, m_impl->environmentBrdfLocation, -1);
    m_impl->Uniform1i(m_impl->hasEnvironmentLocation, irradiance && prefilter && m_impl->view.environmentBrdfLut && irradiance->IsReady() &&
                                                              prefilter->IsReady() && m_impl->view.environmentBrdfLut->IsReady()
                                                          ? 1
                                                          : 0);
    m_impl->ActiveTexture(GL_TEXTURE0);
    m_impl->BindVertexArray(mesh->vao);
    glDrawElements(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT, nullptr);
    m_impl->BindVertexArray(0);
}

void SdlGlRenderDevice::DrawInstanced(const std::vector<RenderItem> &items)
{
    if (items.empty())
        return;
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
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
    m_impl->Uniform1i(m_impl->instancedLocation, 1);
    m_impl->UniformMatrix4fv(m_impl->modelLocation, 1, GL_FALSE, Mat4::Identity().Data());
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
    bindTexture(first.packedTexture, 13, false, m_impl->packedTextureLocation, m_impl->hasPackedTextureLocation);
    m_impl->Uniform1i(m_impl->packedLayoutRmaLocation, first.material && first.material->GetPackedTextureLayout() == "RMA" ? 1 : 0);
    const auto irradiance = m_impl->view.environmentIrradiance ? m_impl->view.environmentIrradiance : m_impl->view.environmentTexture;
    const auto prefilter = m_impl->view.environmentPrefilter ? m_impl->view.environmentPrefilter : m_impl->view.environmentTexture;
    bindTexture(irradiance, 7, false, m_impl->environmentIrradianceLocation, -1);
    bindTexture(prefilter, 11, false, m_impl->environmentPrefilterLocation, -1);
    bindTexture(m_impl->view.environmentBrdfLut, 12, false, m_impl->environmentBrdfLocation, -1);
    m_impl->Uniform1i(m_impl->hasEnvironmentLocation, irradiance && prefilter && m_impl->view.environmentBrdfLut && irradiance->IsReady() &&
                                                              prefilter->IsReady() && m_impl->view.environmentBrdfLut->IsReady()
                                                          ? 1
                                                          : 0);
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
    m_impl->BindVertexArray(m_impl->fullScreenVao);
    for (const auto &particle : particles)
    {
        m_impl->Uniform3f(m_impl->particlePositionLocation, particle.position.x, particle.position.y, particle.position.z);
        m_impl->Uniform1f(m_impl->particleSizeLocation, std::max(0.001f, particle.size));
        m_impl->Uniform4f(m_impl->particleColorLocation, particle.color.x, particle.color.y, particle.color.z, particle.color.w);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    m_impl->BindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
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
    m_impl->BindFramebuffer(GL_FRAMEBUFFER, m_impl->toneFramebuffer);
    glViewport(0, 0, m_impl->sceneWidth, m_impl->sceneHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    m_impl->UseProgram(m_impl->toneProgram);
    m_impl->ActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_impl->sceneHdrColor);
    m_impl->Uniform1i(m_impl->toneTextureLocation, 2);
    m_impl->ActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_impl->sceneDepth);
    m_impl->Uniform1i(m_impl->toneDepthTextureLocation, 3);
    m_impl->ActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_impl->historyHdrColor);
    m_impl->Uniform1i(m_impl->toneHistoryTextureLocation, 4);
    m_impl->Uniform1f(m_impl->exposureLocation, m_impl->exposure);
    m_impl->Uniform2f(m_impl->inverseResolutionLocation, 1.0f / std::max(1, m_impl->sceneWidth), 1.0f / std::max(1, m_impl->sceneHeight));
    m_impl->Uniform1i(m_impl->fxaaEnabledLocation, m_impl->fxaaEnabled ? 1 : 0);
    m_impl->Uniform1i(m_impl->bloomEnabledLocation, m_impl->bloomEnabled ? 1 : 0);
    m_impl->Uniform1i(m_impl->ssaoEnabledLocation, m_impl->ssaoEnabled ? 1 : 0);
    m_impl->Uniform1i(m_impl->taaEnabledLocation, m_impl->taaEnabled ? 1 : 0);
    m_impl->Uniform1i(m_impl->historyValidLocation, m_impl->historyValid ? 1 : 0);
    m_impl->BindVertexArray(m_impl->fullScreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    m_impl->BindVertexArray(0);
    m_impl->BindFramebuffer(GL_FRAMEBUFFER, m_impl->sceneFramebuffer);
    m_impl->ActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_impl->historyHdrColor);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, m_impl->sceneWidth, m_impl->sceneHeight);
    m_impl->historyValid = true;
    m_impl->ActiveTexture(GL_TEXTURE0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    m_impl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    int width = 0, height = 0;
    SDL_GL_GetDrawableSize(m_impl->window, &width, &height);
    glViewport(0, 0, width, height);
    glDepthMask(GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.025f, 0.03f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
bool SdlGlRenderDevice::EnableSceneRenderTarget(const bool enabled, const int width, const int height)
{
    if (!m_impl || !m_impl->context)
        return false;
    m_impl->sceneTargetEnabled = enabled;
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
    return m_impl && m_impl->ReloadDefaultShadersFromFiles();
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
        m_impl->ssaoEnabled = enabled;
}
bool SdlGlRenderDevice::IsSsaoEnabled() const
{
    return m_impl && m_impl->ssaoEnabled;
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
} // namespace MiniEngine
