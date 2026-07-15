#include "MiniEngine/Render/SdlGlRenderDevice.hpp"

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Render/ShaderPipeline.hpp"

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
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

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
            uniform mat4 uMvp;
            uniform mat4 uModel;
            uniform mat4 uLightViewProjection;
            out vec3 vNormal;
            out vec3 vWorldPosition;
            out vec2 vUv;
            out vec4 vLightSpace;
            void main()
            {
                gl_Position = uMvp * vec4(aPosition, 1.0);
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
            uniform mat4 uLightMvp;
            void main() { gl_Position = uLightMvp * vec4(aPosition, 1.0); }
        )GLSL";

constexpr const char *ShadowFragmentShaderSource = R"GLSL(
            #version 330 core
            void main() {}
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
            uniform float uExposure;
            uniform vec2 uInverseResolution;
            uniform int uFxaaEnabled;
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
    GLuint program = 0, shadowProgram = 0, toneProgram = 0, uiProgram = 0, fullScreenVao = 0;
    GLuint uiVao = 0, uiVertexBuffer = 0, uiIndexBuffer = 0, fontAtlas = 0;
    GLuint sceneFramebuffer = 0, toneFramebuffer = 0, sceneHdrColor = 0, sceneColor = 0, sceneDepth = 0;
    GLuint shadowFramebuffer = 0, shadowDepth = 0;
    int shadowResolution = 2048;
    float exposure = 1.0f;
    bool fxaaEnabled = true;
    GLuint timerQueries[2]{};
    bool timerIssued[2]{};
    int timerIndex = 0;
    bool timerActive = false;
    float lastGpuFrameMilliseconds = 0.0f;
    int sceneWidth = 0, sceneHeight = 0;
    bool sceneTargetEnabled = false;
    bool previousUiNext = false, previousUiAccept = false;
    SDL_GameController *controller = nullptr;
    GLint mvpLocation = -1, modelLocation = -1, lightViewProjectionLocation = -1, baseColorLocation = -1;
    GLint metallicLocation = -1, roughnessLocation = -1, emissiveLocation = -1, cameraPositionLocation = -1;
    GLint lightDirectionLocation = -1, lightColorLocation = -1, lightIntensityLocation = -1;
    GLint textureLocation = -1, hasTextureLocation = -1, shadowMapLocation = -1;
    GLint lightCountLocation = -1;
    std::array<GLint, FrameView::MaxLights> lightDirectionsLocations{}, lightColorsLocations{}, lightIntensitiesLocations{};
    GLint normalTextureLocation = -1, hasNormalTextureLocation = -1, metallicTextureLocation = -1, hasMetallicTextureLocation = -1;
    GLint roughnessTextureLocation = -1, hasRoughnessTextureLocation = -1, aoTextureLocation = -1, hasAoTextureLocation = -1;
    GLint emissiveTextureLocation = -1, hasEmissiveTextureLocation = -1;
    GLint environmentTextureLocation = -1, hasEnvironmentLocation = -1;
    GLint shadowMvpLocation = -1, toneTextureLocation = -1, exposureLocation = -1, inverseResolutionLocation = -1, fxaaEnabledLocation = -1;
    GLint uiViewportLocation = -1, uiColorLocation = -1, uiTextureLocation = -1, uiHasTextureLocation = -1;
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
        GLuint vao = 0, vertexBuffer = 0, indexBuffer = 0;
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
               LoadGl(CreateShader, "glCreateShader") && LoadGl(ShaderSource, "glShaderSource") &&
               LoadGl(CompileShader, "glCompileShader") && LoadGl(GetShaderiv, "glGetShaderiv") &&
               LoadGl(GetShaderInfoLog, "glGetShaderInfoLog") && LoadGl(DeleteShader, "glDeleteShader") &&
               LoadGl(CreateProgram, "glCreateProgram") && LoadGl(AttachShader, "glAttachShader") && LoadGl(LinkProgram, "glLinkProgram") &&
               LoadGl(GetProgramiv, "glGetProgramiv") && LoadGl(GetProgramInfoLog, "glGetProgramInfoLog") &&
               LoadGl(DeleteProgram, "glDeleteProgram") && LoadGl(UseProgram, "glUseProgram") &&
               LoadGl(GetUniformLocation, "glGetUniformLocation") && LoadGl(UniformMatrix4fv, "glUniformMatrix4fv") &&
               LoadGl(Uniform4f, "glUniform4f") && LoadGl(Uniform3f, "glUniform3f") && LoadGl(Uniform2f, "glUniform2f") &&
               LoadGl(Uniform1f, "glUniform1f") && LoadGl(Uniform1i, "glUniform1i") && LoadGl(ActiveTexture, "glActiveTexture") &&
               LoadGl(GenerateMipmap, "glGenerateMipmap") && LoadGl(GenFramebuffers, "glGenFramebuffers") &&
               LoadGl(BindFramebuffer, "glBindFramebuffer") && LoadGl(DeleteFramebuffers, "glDeleteFramebuffers") &&
               LoadGl(FramebufferTexture2D, "glFramebufferTexture2D") && LoadGl(CheckFramebufferStatus, "glCheckFramebufferStatus") &&
               LoadGl(GenQueries, "glGenQueries") && LoadGl(DeleteQueries, "glDeleteQueries") && LoadGl(BeginQuery, "glBeginQuery") &&
               LoadGl(EndQuery, "glEndQuery") && LoadGl(GetQueryObjectiv, "glGetQueryObjectiv") &&
               LoadGl(GetQueryObjectui64v, "glGetQueryObjectui64v");
    }

    void DestroySceneTarget()
    {
        if (sceneHdrColor)
            glDeleteTextures(1, &sceneHdrColor);
        if (sceneColor)
            glDeleteTextures(1, &sceneColor);
        if (sceneDepth)
            glDeleteTextures(1, &sceneDepth);
        if (sceneFramebuffer)
            DeleteFramebuffers(1, &sceneFramebuffer);
        if (toneFramebuffer)
            DeleteFramebuffers(1, &toneFramebuffer);
        sceneHdrColor = sceneColor = sceneDepth = sceneFramebuffer = toneFramebuffer = 0;
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
        if (shadowDepth)
            glDeleteTextures(1, &shadowDepth);
        if (shadowFramebuffer)
            DeleteFramebuffers(1, &shadowFramebuffer);
        shadowDepth = shadowFramebuffer = 0;
    }

    bool CreateShadowTarget()
    {
        DestroyShadowTarget();
        GenFramebuffers(1, &shadowFramebuffer);
        BindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer);
        glGenTextures(1, &shadowDepth);
        glBindTexture(GL_TEXTURE_2D, shadowDepth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowResolution, shadowResolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const GLfloat border[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepth, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        const bool complete = CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
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
        modelLocation = GetUniformLocation(program, "uModel");
        lightViewProjectionLocation = GetUniformLocation(program, "uLightViewProjection");
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
        shadowMapLocation = GetUniformLocation(program, "uShadowMap");
        environmentTextureLocation = GetUniformLocation(program, "uEnvironmentMap");
        hasEnvironmentLocation = GetUniformLocation(program, "uHasEnvironment");
        return true;
    }

    bool CreateFontAtlas()
    {
        constexpr int width = 512, height = 256, cell = 32;
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
        for (std::size_t index = 0; index < static_cast<std::size_t>(width) * height; ++index)
            pixels[index * 4] = pixels[index * 4 + 1] = pixels[index * 4 + 2] = 255;
#ifdef _WIN32
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = width;
        bitmapInfo.bmiHeader.biHeight = -height;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        void *bitmapPixels = nullptr;
        HDC device = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = device ? CreateDIBSection(device, &bitmapInfo, DIB_RGB_COLORS, &bitmapPixels, nullptr, 0) : nullptr;
        HFONT font = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas");
        if (device && bitmap && bitmapPixels && font)
        {
            const HGDIOBJ oldBitmap = SelectObject(device, bitmap), oldFont = SelectObject(device, font);
            std::fill_n(static_cast<std::uint8_t *>(bitmapPixels), static_cast<std::size_t>(width) * height * 4,
                        static_cast<std::uint8_t>(0));
            SetBkColor(device, RGB(0, 0, 0));
            SetTextColor(device, RGB(255, 255, 255));
            SetBkMode(device, OPAQUE);
            for (int character = 32; character < 127; ++character)
            {
                const wchar_t text = static_cast<wchar_t>(character);
                const int column = character % 16, row = character / 16;
                TextOutW(device, column * cell + 4, row * cell + 3, &text, 1);
            }
            const auto *source = static_cast<const std::uint8_t *>(bitmapPixels);
            for (std::size_t index = 0; index < static_cast<std::size_t>(width) * height; ++index)
            {
                const std::uint8_t alpha = std::max(source[index * 4], std::max(source[index * 4 + 1], source[index * 4 + 2]));
                pixels[index * 4] = pixels[index * 4 + 1] = pixels[index * 4 + 2] = 255;
                pixels[index * 4 + 3] = alpha;
            }
            SelectObject(device, oldFont);
            SelectObject(device, oldBitmap);
        }
        if (font)
            DeleteObject(font);
        if (bitmap)
            DeleteObject(bitmap);
        if (device)
            DeleteDC(device);
#else
        for (int character = 32; character < 127; ++character)
            for (int y = 6; y < 26; ++y)
                for (int x = 6; x < 20; ++x)
                    if (x == 6 || x == 19 || y == 6 || y == 25)
                        pixels[((character / 16 * cell + y) * width + character % 16 * cell + x) * 4 + 3] = 255;
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

    bool CreateAuxiliaryPrograms()
    {
        shadowProgram = CreateLinkedProgram(ShadowVertexShaderSource, ShadowFragmentShaderSource);
        toneProgram = CreateLinkedProgram(ToneVertexShaderSource, ToneFragmentShaderSource);
        uiProgram = CreateLinkedProgram(UiVertexShaderSource, UiFragmentShaderSource);
        if (!shadowProgram || !toneProgram || !uiProgram)
            return false;
        shadowMvpLocation = GetUniformLocation(shadowProgram, "uLightMvp");
        toneTextureLocation = GetUniformLocation(toneProgram, "uHdrTexture");
        exposureLocation = GetUniformLocation(toneProgram, "uExposure");
        inverseResolutionLocation = GetUniformLocation(toneProgram, "uInverseResolution");
        fxaaEnabledLocation = GetUniformLocation(toneProgram, "uFxaaEnabled");
        GenVertexArrays(1, &fullScreenVao);
        uiViewportLocation = GetUniformLocation(uiProgram, "uViewport");
        uiColorLocation = GetUniformLocation(uiProgram, "uColor");
        uiTextureLocation = GetUniformLocation(uiProgram, "uUiTexture");
        uiHasTextureLocation = GetUniformLocation(uiProgram, "uHasUiTexture");
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
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (resource->IsHdr())
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, resource->GetWidth(), resource->GetHeight(), 0, GL_RGBA, GL_FLOAT,
                         resource->GetFloatData().data());
        else
            glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, resource->GetWidth(), resource->GetHeight(), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, resource->GetData().data());
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

void SdlGlRenderDevice::BeginShadowPass(const FrameView &view)
{
    m_impl->view = view;
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
    m_impl->BindFramebuffer(GL_FRAMEBUFFER, m_impl->shadowFramebuffer);
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
}

void SdlGlRenderDevice::DrawShadow(const RenderItem &item)
{
    const auto *mesh = m_impl->GetOrCreateMesh(item.mesh);
    if (!mesh)
        return;
    const Mat4 lightMvp = m_impl->view.lightViewProjection * item.model;
    m_impl->UniformMatrix4fv(m_impl->shadowMvpLocation, 1, GL_FALSE, lightMvp.Data());
    m_impl->BindVertexArray(mesh->vao);
    glDrawElements(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT, nullptr);
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
    m_impl->UniformMatrix4fv(m_impl->lightViewProjectionLocation, 1, GL_FALSE, view.lightViewProjection.Data());
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
    m_impl->Uniform1i(m_impl->shadowMapLocation, 1);
    m_impl->ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_impl->shadowDepth);
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
    m_impl->UniformMatrix4fv(m_impl->modelLocation, 1, GL_FALSE, item.model.Data());
    m_impl->Uniform4f(m_impl->baseColorLocation, color.x, color.y, color.z, color.w);
    m_impl->Uniform1f(m_impl->metallicLocation, metallic);
    m_impl->Uniform1f(m_impl->roughnessLocation, roughness);
    m_impl->Uniform3f(m_impl->emissiveLocation, emissive.x, emissive.y, emissive.z);
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
    bindTexture(m_impl->view.environmentTexture, 7, false, m_impl->environmentTextureLocation, m_impl->hasEnvironmentLocation);
    m_impl->ActiveTexture(GL_TEXTURE0);
    m_impl->BindVertexArray(mesh->vao);
    glDrawElements(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT, nullptr);
    m_impl->BindVertexArray(0);
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
    m_impl->Uniform1f(m_impl->exposureLocation, m_impl->exposure);
    m_impl->Uniform2f(m_impl->inverseResolutionLocation, 1.0f / std::max(1, m_impl->sceneWidth), 1.0f / std::max(1, m_impl->sceneHeight));
    m_impl->Uniform1i(m_impl->fxaaEnabledLocation, m_impl->fxaaEnabled ? 1 : 0);
    m_impl->BindVertexArray(m_impl->fullScreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    m_impl->BindVertexArray(0);
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
        if (command.type == UIWidgetType::Panel || command.type == UIWidgetType::Button || command.type == UIWidgetType::Image)
        {
            std::vector<Impl::UiVertex> vertices;
            std::vector<std::uint32_t> indices;
            appendQuad(vertices, indices, command.rect, 0.0f, 0.0f, 1.0f, 1.0f);
            const GLuint image = command.type == UIWidgetType::Image ? m_impl->GetOrCreateUiTexture(command.imagePath) : 0;
            m_impl->DrawUiGeometry(vertices, indices, command.color, image, width, height);
        }
        if (!command.text.empty() && (command.type == UIWidgetType::Label || command.type == UIWidgetType::Button))
        {
            std::vector<Impl::UiVertex> vertices;
            std::vector<std::uint32_t> indices;
            const float glyphSize = std::max(6.0f, command.fontSize), advance = glyphSize * 0.62f;
            float cursorX = command.rect.x;
            if (command.type == UIWidgetType::Button)
                cursorX += std::max(0.0f, (command.rect.width - advance * static_cast<float>(command.text.size())) * 0.5f);
            float cursorY = command.rect.y + std::max(0.0f, (command.rect.height - glyphSize) * 0.5f);
            for (const unsigned char byte : command.text)
            {
                if (byte == '\n')
                {
                    cursorX = command.rect.x;
                    cursorY += glyphSize;
                    continue;
                }
                const int character = byte >= 32 && byte < 127 ? byte : '?';
                if (character != ' ')
                {
                    const int column = character % 16, row = character / 16;
                    const float u0 = (column * 32.0f + 0.5f) / 512.0f, v0 = (row * 32.0f + 0.5f) / 256.0f;
                    const float u1 = ((column + 1) * 32.0f - 0.5f) / 512.0f, v1 = ((row + 1) * 32.0f - 0.5f) / 256.0f;
                    appendQuad(vertices, indices, {cursorX, cursorY, glyphSize, glyphSize}, u0, v0, u1, v1);
                }
                cursorX += advance;
            }
            const Vec4 textColor = command.type == UIWidgetType::Button ? Vec4{1.0f, 1.0f, 1.0f, command.color.w} : command.color;
            m_impl->DrawUiGeometry(vertices, indices, textColor, m_impl->fontAtlas, width, height);
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
float SdlGlRenderDevice::GetLastGpuFrameMilliseconds() const
{
    return m_impl ? m_impl->lastGpuFrameMilliseconds : 0.0f;
}
} // namespace MiniEngine
