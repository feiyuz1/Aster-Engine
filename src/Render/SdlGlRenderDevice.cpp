#include "MiniEngine/Render/SdlGlRenderDevice.hpp"

#include "MiniEngine/Platform/Platform.hpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>

namespace MiniEngine
{
    namespace
    {
        template<typename T>
        bool LoadGl(T& target, const char* name)
        {
            target = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
            return target != nullptr;
        }

        constexpr const char* VertexShaderSource = R"GLSL(
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

        constexpr const char* FragmentShaderSource = R"GLSL(
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

        constexpr const char* ShadowVertexShaderSource = R"GLSL(
            #version 330 core
            layout(location = 0) in vec3 aPosition;
            uniform mat4 uLightMvp;
            void main() { gl_Position = uLightMvp * vec4(aPosition, 1.0); }
        )GLSL";

        constexpr const char* ShadowFragmentShaderSource = R"GLSL(
            #version 330 core
            void main() {}
        )GLSL";

        constexpr const char* ToneVertexShaderSource = R"GLSL(
            #version 330 core
            out vec2 vUv;
            void main()
            {
                vec2 position = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                vUv = position;
                gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
            }
        )GLSL";

        constexpr const char* ToneFragmentShaderSource = R"GLSL(
            #version 330 core
            in vec2 vUv;
            uniform sampler2D uHdrTexture;
            uniform float uExposure;
            out vec4 outColor;
            vec3 AcesFilm(vec3 color)
            {
                return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
            }
            void main()
            {
                vec3 hdr = texture(uHdrTexture, vUv).rgb * uExposure;
                outColor = vec4(pow(AcesFilm(hdr), vec3(1.0 / 2.2)), 1.0);
            }
        )GLSL";
    }

    struct SdlGlRenderDevice::Impl
    {
        using GenVertexArraysProc = void (APIENTRY*)(GLsizei, GLuint*);
        using BindVertexArrayProc = void (APIENTRY*)(GLuint);
        using DeleteVertexArraysProc = void (APIENTRY*)(GLsizei, const GLuint*);
        using GenBuffersProc = void (APIENTRY*)(GLsizei, GLuint*);
        using BindBufferProc = void (APIENTRY*)(GLenum, GLuint);
        using BufferDataProc = void (APIENTRY*)(GLenum, GLsizeiptr, const void*, GLenum);
        using DeleteBuffersProc = void (APIENTRY*)(GLsizei, const GLuint*);
        using VertexAttribPointerProc = void (APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
        using EnableVertexAttribArrayProc = void (APIENTRY*)(GLuint);
        using CreateShaderProc = GLuint (APIENTRY*)(GLenum);
        using ShaderSourceProc = void (APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
        using CompileShaderProc = void (APIENTRY*)(GLuint);
        using GetShaderivProc = void (APIENTRY*)(GLuint, GLenum, GLint*);
        using GetShaderInfoLogProc = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
        using DeleteShaderProc = void (APIENTRY*)(GLuint);
        using CreateProgramProc = GLuint (APIENTRY*)();
        using AttachShaderProc = void (APIENTRY*)(GLuint, GLuint);
        using LinkProgramProc = void (APIENTRY*)(GLuint);
        using GetProgramivProc = void (APIENTRY*)(GLuint, GLenum, GLint*);
        using GetProgramInfoLogProc = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
        using DeleteProgramProc = void (APIENTRY*)(GLuint);
        using UseProgramProc = void (APIENTRY*)(GLuint);
        using GetUniformLocationProc = GLint (APIENTRY*)(GLuint, const GLchar*);
        using UniformMatrix4fvProc = void (APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
        using Uniform4fProc = void (APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
        using Uniform3fProc = void (APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat);
        using Uniform1fProc = void (APIENTRY*)(GLint, GLfloat);
        using Uniform1iProc = void (APIENTRY*)(GLint, GLint);
        using ActiveTextureProc = void (APIENTRY*)(GLenum);
        using GenFramebuffersProc = void (APIENTRY*)(GLsizei, GLuint*);
        using BindFramebufferProc = void (APIENTRY*)(GLenum, GLuint);
        using DeleteFramebuffersProc = void (APIENTRY*)(GLsizei, const GLuint*);
        using FramebufferTexture2DProc = void (APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
        using CheckFramebufferStatusProc = GLenum (APIENTRY*)(GLenum);

        SDL_Window* window = nullptr;
        SDL_GLContext context = nullptr;
        InputState input;
        FrameView view;
        GLuint program = 0, shadowProgram = 0, toneProgram = 0, fullScreenVao = 0;
        GLuint sceneFramebuffer = 0, toneFramebuffer = 0, sceneHdrColor = 0, sceneColor = 0, sceneDepth = 0;
        GLuint shadowFramebuffer = 0, shadowDepth = 0;
        int shadowResolution = 2048;
        float exposure = 1.0f;
        int sceneWidth = 0, sceneHeight = 0;
        bool sceneTargetEnabled = false;
        GLint mvpLocation = -1, modelLocation = -1, lightViewProjectionLocation = -1, baseColorLocation = -1;
        GLint metallicLocation = -1, roughnessLocation = -1, emissiveLocation = -1, cameraPositionLocation = -1;
        GLint lightDirectionLocation = -1, lightColorLocation = -1, lightIntensityLocation = -1;
        GLint textureLocation = -1, hasTextureLocation = -1, shadowMapLocation = -1;
        GLint shadowMvpLocation = -1, toneTextureLocation = -1, exposureLocation = -1;
        FileWatcher vertexShaderWatcher, fragmentShaderWatcher;
        std::filesystem::path vertexShaderPath{"assets/shaders/default.vert"}, fragmentShaderPath{"assets/shaders/default.frag"};

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
        Uniform1fProc Uniform1f = nullptr;
        Uniform1iProc Uniform1i = nullptr;
        ActiveTextureProc ActiveTexture = nullptr;
        GenFramebuffersProc GenFramebuffers = nullptr;
        BindFramebufferProc BindFramebuffer = nullptr;
        DeleteFramebuffersProc DeleteFramebuffers = nullptr;
        FramebufferTexture2DProc FramebufferTexture2D = nullptr;
        CheckFramebufferStatusProc CheckFramebufferStatus = nullptr;

        struct GpuMesh { GLuint vao = 0, vertexBuffer = 0, indexBuffer = 0; GLsizei indexCount = 0; };
        std::unordered_map<std::string, GpuMesh> meshes;
        std::unordered_map<std::string, GLuint> textures;

        bool LoadFunctions()
        {
            return LoadGl(GenVertexArrays, "glGenVertexArrays") && LoadGl(BindVertexArray, "glBindVertexArray") &&
                LoadGl(DeleteVertexArrays, "glDeleteVertexArrays") && LoadGl(GenBuffers, "glGenBuffers") &&
                LoadGl(BindBuffer, "glBindBuffer") && LoadGl(BufferData, "glBufferData") &&
                LoadGl(DeleteBuffers, "glDeleteBuffers") && LoadGl(VertexAttribPointer, "glVertexAttribPointer") &&
                LoadGl(EnableVertexAttribArray, "glEnableVertexAttribArray") && LoadGl(CreateShader, "glCreateShader") &&
                LoadGl(ShaderSource, "glShaderSource") && LoadGl(CompileShader, "glCompileShader") &&
                LoadGl(GetShaderiv, "glGetShaderiv") && LoadGl(GetShaderInfoLog, "glGetShaderInfoLog") &&
                LoadGl(DeleteShader, "glDeleteShader") && LoadGl(CreateProgram, "glCreateProgram") &&
                LoadGl(AttachShader, "glAttachShader") && LoadGl(LinkProgram, "glLinkProgram") &&
                LoadGl(GetProgramiv, "glGetProgramiv") && LoadGl(GetProgramInfoLog, "glGetProgramInfoLog") &&
                LoadGl(DeleteProgram, "glDeleteProgram") && LoadGl(UseProgram, "glUseProgram") &&
                LoadGl(GetUniformLocation, "glGetUniformLocation") && LoadGl(UniformMatrix4fv, "glUniformMatrix4fv") &&
                LoadGl(Uniform4f, "glUniform4f") && LoadGl(Uniform3f, "glUniform3f") && LoadGl(Uniform1f, "glUniform1f") &&
                LoadGl(Uniform1i, "glUniform1i") && LoadGl(ActiveTexture, "glActiveTexture") &&
                LoadGl(GenFramebuffers, "glGenFramebuffers") && LoadGl(BindFramebuffer, "glBindFramebuffer") &&
                LoadGl(DeleteFramebuffers, "glDeleteFramebuffers") && LoadGl(FramebufferTexture2D, "glFramebufferTexture2D") &&
                LoadGl(CheckFramebufferStatus, "glCheckFramebufferStatus");
        }

        void DestroySceneTarget()
        {
            if (sceneHdrColor) glDeleteTextures(1, &sceneHdrColor);
            if (sceneColor) glDeleteTextures(1, &sceneColor);
            if (sceneDepth) glDeleteTextures(1, &sceneDepth);
            if (sceneFramebuffer) DeleteFramebuffers(1, &sceneFramebuffer);
            if (toneFramebuffer) DeleteFramebuffers(1, &toneFramebuffer);
            sceneHdrColor = sceneColor = sceneDepth = sceneFramebuffer = toneFramebuffer = 0; sceneWidth = sceneHeight = 0;
        }

        bool CreateSceneTarget(const int width, const int height)
        {
            if (width < 1 || height < 1) return false;
            DestroySceneTarget();
            GenFramebuffers(1, &sceneFramebuffer); BindFramebuffer(GL_FRAMEBUFFER, sceneFramebuffer);
            glGenTextures(1, &sceneHdrColor); glBindTexture(GL_TEXTURE_2D, sceneHdrColor);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneHdrColor, 0);
            glGenTextures(1, &sceneDepth); glBindTexture(GL_TEXTURE_2D, sceneDepth);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepth, 0);
            bool complete = CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            GenFramebuffers(1, &toneFramebuffer); BindFramebuffer(GL_FRAMEBUFFER, toneFramebuffer);
            glGenTextures(1, &sceneColor); glBindTexture(GL_TEXTURE_2D, sceneColor);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);
            complete = complete && CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            BindFramebuffer(GL_FRAMEBUFFER, 0); glBindTexture(GL_TEXTURE_2D, 0);
            if (!complete) { DestroySceneTarget(); return false; }
            sceneWidth = width; sceneHeight = height; return true;
        }

        void DestroyShadowTarget()
        {
            if (shadowDepth) glDeleteTextures(1, &shadowDepth);
            if (shadowFramebuffer) DeleteFramebuffers(1, &shadowFramebuffer);
            shadowDepth = shadowFramebuffer = 0;
        }

        bool CreateShadowTarget()
        {
            DestroyShadowTarget();
            GenFramebuffers(1, &shadowFramebuffer); BindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer);
            glGenTextures(1, &shadowDepth); glBindTexture(GL_TEXTURE_2D, shadowDepth);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowResolution, shadowResolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            const GLfloat border[] = {1.0f, 1.0f, 1.0f, 1.0f}; glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
            FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepth, 0);
            glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
            const bool complete = CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            BindFramebuffer(GL_FRAMEBUFFER, 0); glBindTexture(GL_TEXTURE_2D, 0);
            if (!complete) DestroyShadowTarget();
            return complete;
        }

        GLuint Compile(const GLenum type, const char* source)
        {
            const GLuint shader = CreateShader(type);
            ShaderSource(shader, 1, &source, nullptr);
            CompileShader(shader);
            GLint success = GL_FALSE;
            GetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (success == GL_TRUE) return shader;
            GLchar log[1024]{}; GetShaderInfoLog(shader, sizeof(log), nullptr, log);
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Shader compilation failed: %s", log);
            DeleteShader(shader);
            return 0;
        }

        GLuint CreateLinkedProgram(const char* vertexSource, const char* fragmentSource)
        {
            const GLuint vertex = Compile(GL_VERTEX_SHADER, vertexSource);
            const GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource);
            if (!vertex || !fragment) { if (vertex) DeleteShader(vertex); if (fragment) DeleteShader(fragment); return 0; }
            const GLuint candidate = CreateProgram(); AttachShader(candidate, vertex); AttachShader(candidate, fragment); LinkProgram(candidate);
            DeleteShader(vertex); DeleteShader(fragment);
            GLint success = GL_FALSE; GetProgramiv(candidate, GL_LINK_STATUS, &success);
            if (success != GL_TRUE)
            {
                GLchar log[1024]{}; GetProgramInfoLog(candidate, sizeof(log), nullptr, log);
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Program link failed: %s", log);
                DeleteProgram(candidate); return 0;
            }
            return candidate;
        }

        bool CreateDefaultProgram(const char* vertexSource = VertexShaderSource, const char* fragmentSource = FragmentShaderSource)
        {
            const GLuint candidate = CreateLinkedProgram(vertexSource, fragmentSource); if (!candidate) return false;
            if (program) DeleteProgram(program); program = candidate;
            mvpLocation = GetUniformLocation(program, "uMvp"); modelLocation = GetUniformLocation(program, "uModel");
            lightViewProjectionLocation = GetUniformLocation(program, "uLightViewProjection");
            baseColorLocation = GetUniformLocation(program, "uBaseColor"); lightDirectionLocation = GetUniformLocation(program, "uLightDirection");
            lightColorLocation = GetUniformLocation(program, "uLightColor"); lightIntensityLocation = GetUniformLocation(program, "uLightIntensity");
            metallicLocation = GetUniformLocation(program, "uMetallic"); roughnessLocation = GetUniformLocation(program, "uRoughness");
            emissiveLocation = GetUniformLocation(program, "uEmissive"); cameraPositionLocation = GetUniformLocation(program, "uCameraPosition");
            textureLocation = GetUniformLocation(program, "uTexture"); hasTextureLocation = GetUniformLocation(program, "uHasTexture");
            shadowMapLocation = GetUniformLocation(program, "uShadowMap");
            return true;
        }

        bool CreateAuxiliaryPrograms()
        {
            shadowProgram = CreateLinkedProgram(ShadowVertexShaderSource, ShadowFragmentShaderSource);
            toneProgram = CreateLinkedProgram(ToneVertexShaderSource, ToneFragmentShaderSource);
            if (!shadowProgram || !toneProgram) return false;
            shadowMvpLocation = GetUniformLocation(shadowProgram, "uLightMvp");
            toneTextureLocation = GetUniformLocation(toneProgram, "uHdrTexture"); exposureLocation = GetUniformLocation(toneProgram, "uExposure");
            GenVertexArrays(1, &fullScreenVao);
            return fullScreenVao != 0;
        }

        bool ReloadDefaultShadersFromFiles()
        {
            const std::string vertex = PlatformFile::ReadAllText(vertexShaderPath), fragment = PlatformFile::ReadAllText(fragmentShaderPath);
            if (vertex.empty() || fragment.empty()) return false;
            const bool reloaded = CreateDefaultProgram(vertex.c_str(), fragment.c_str());
            SDL_Log(reloaded ? "Reloaded render shaders" : "Render shader reload failed; keeping the previous program");
            return reloaded;
        }

        void CheckShaderHotReload()
        {
            const bool vertexChanged = vertexShaderWatcher.HasChanged(vertexShaderPath);
            const bool fragmentChanged = fragmentShaderWatcher.HasChanged(fragmentShaderPath);
            if (vertexChanged || fragmentChanged) ReloadDefaultShadersFromFiles();
        }

        const GpuMesh* GetOrCreateMesh(const std::shared_ptr<MeshResource>& resource)
        {
            if (!resource || !resource->IsReady()) return nullptr;
            const auto found = meshes.find(resource->GetPath());
            if (found != meshes.end()) return &found->second;
            GpuMesh mesh;
            GenVertexArrays(1, &mesh.vao); BindVertexArray(mesh.vao);
            GenBuffers(1, &mesh.vertexBuffer); BindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
            const auto& vertices = resource->GetVertices();
            BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(MeshVertex)), vertices.data(), GL_STATIC_DRAW);
            GenBuffers(1, &mesh.indexBuffer); BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
            const auto& indices = resource->GetIndices();
            BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STATIC_DRAW);
            VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, position)));
            EnableVertexAttribArray(0);
            VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, normal)));
            EnableVertexAttribArray(1); BindVertexArray(0);
            BindVertexArray(mesh.vao);
            VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), reinterpret_cast<void*>(offsetof(MeshVertex, uv)));
            EnableVertexAttribArray(2); BindVertexArray(0);
            mesh.indexCount = static_cast<GLsizei>(indices.size());
            return &meshes.emplace(resource->GetPath(), mesh).first->second;
        }

        GLuint GetOrCreateTexture(const std::shared_ptr<TextureResource>& resource)
        {
            if (!resource || !resource->IsReady()) return 0;
            const auto found = textures.find(resource->GetPath());
            if (found != textures.end()) return found->second;
            GLuint texture = 0; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, resource->GetWidth(), resource->GetHeight(), 0, GL_RGBA, GL_UNSIGNED_BYTE, resource->GetData().data());
            glBindTexture(GL_TEXTURE_2D, 0); textures[resource->GetPath()] = texture; return texture;
        }
    };

    SdlGlRenderDevice::SdlGlRenderDevice(const int width, const int height, std::string title)
        : m_impl(std::make_unique<Impl>()), m_width(width), m_height(height), m_title(std::move(title)) {}
    SdlGlRenderDevice::~SdlGlRenderDevice() { Shutdown(); }

    bool SdlGlRenderDevice::Initialize()
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) return false;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1); SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        m_impl->window = SDL_CreateWindow(m_title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_width, m_height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
        if (!m_impl->window) { SDL_Quit(); return false; }
        m_impl->context = SDL_GL_CreateContext(m_impl->window);
        if (!m_impl->context || !m_impl->LoadFunctions()) { Shutdown(); return false; }
        const std::string vertexSource = PlatformFile::ReadAllText(m_impl->vertexShaderPath), fragmentSource = PlatformFile::ReadAllText(m_impl->fragmentShaderPath);
        const bool programCreated = !vertexSource.empty() && !fragmentSource.empty() ? m_impl->CreateDefaultProgram(vertexSource.c_str(), fragmentSource.c_str()) : m_impl->CreateDefaultProgram();
        if (!programCreated || !m_impl->CreateAuxiliaryPrograms() || !m_impl->CreateShadowTarget()) { Shutdown(); return false; }
        m_impl->vertexShaderWatcher.HasChanged(m_impl->vertexShaderPath); m_impl->fragmentShaderWatcher.HasChanged(m_impl->fragmentShaderPath);
        SDL_GL_SetSwapInterval(1); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
        int drawableWidth = 0, drawableHeight = 0; SDL_GL_GetDrawableSize(m_impl->window, &drawableWidth, &drawableHeight);
        if (drawableHeight > 0) m_aspect = static_cast<float>(drawableWidth) / static_cast<float>(drawableHeight);
        return true;
    }

    void SdlGlRenderDevice::Shutdown()
    {
        if (!m_impl) return;
        if (m_impl->context)
        {
            m_impl->DestroySceneTarget();
            m_impl->DestroyShadowTarget();
            for (const auto& pair : m_impl->meshes)
            {
                m_impl->DeleteBuffers(1, &pair.second.vertexBuffer); m_impl->DeleteBuffers(1, &pair.second.indexBuffer);
                m_impl->DeleteVertexArrays(1, &pair.second.vao);
            }
            m_impl->meshes.clear();
            for (const auto& pair : m_impl->textures) glDeleteTextures(1, &pair.second);
            m_impl->textures.clear();
            if (m_impl->program) { m_impl->DeleteProgram(m_impl->program); m_impl->program = 0; }
            if (m_impl->shadowProgram) { m_impl->DeleteProgram(m_impl->shadowProgram); m_impl->shadowProgram = 0; }
            if (m_impl->toneProgram) { m_impl->DeleteProgram(m_impl->toneProgram); m_impl->toneProgram = 0; }
            if (m_impl->fullScreenVao) { m_impl->DeleteVertexArrays(1, &m_impl->fullScreenVao); m_impl->fullScreenVao = 0; }
            SDL_GL_DeleteContext(m_impl->context); m_impl->context = nullptr;
        }
        if (m_impl->window) { SDL_DestroyWindow(m_impl->window); m_impl->window = nullptr; }
        SDL_Quit();
    }

    bool SdlGlRenderDevice::PumpEvents()
    {
        m_impl->CheckShaderHotReload();
        m_impl->input.mouseX = m_impl->input.mouseY = 0.0f;
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (m_eventCallback) m_eventCallback(&event);
            if (event.type == SDL_QUIT) return false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                glViewport(0, 0, event.window.data1, event.window.data2);
                if (event.window.data2 > 0) m_aspect = static_cast<float>(event.window.data1) / static_cast<float>(event.window.data2);
            }
            if (event.type == SDL_MOUSEMOTION) { m_impl->input.mouseX += static_cast<float>(event.motion.xrel); m_impl->input.mouseY += static_cast<float>(event.motion.yrel); }
        }
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const auto axis = [keys](SDL_Scancode negative, SDL_Scancode positive) { return (keys[positive] ? 1.0f : 0.0f) - (keys[negative] ? 1.0f : 0.0f); };
        m_impl->input.moveX = axis(SDL_SCANCODE_A, SDL_SCANCODE_D); m_impl->input.moveY = axis(SDL_SCANCODE_F, SDL_SCANCODE_R); m_impl->input.moveZ = axis(SDL_SCANCODE_S, SDL_SCANCODE_W);
        m_impl->input.rotateX = axis(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN); m_impl->input.rotateY = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT); m_impl->input.rotateZ = axis(SDL_SCANCODE_Q, SDL_SCANCODE_E);
        m_impl->input.scale = axis(SDL_SCANCODE_X, SDL_SCANCODE_Z); m_impl->input.sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        m_impl->input.primaryAction = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        int cursorX = 0, cursorY = 0; SDL_GetMouseState(&cursorX, &cursorY); m_impl->input.cursorX = static_cast<float>(cursorX); m_impl->input.cursorY = static_cast<float>(cursorY);
        m_impl->input.quitRequested = keys[SDL_SCANCODE_ESCAPE] != 0;
        return !m_impl->input.quitRequested;
    }

    const InputState& SdlGlRenderDevice::GetInputState() const { return m_impl->input; }
    float SdlGlRenderDevice::GetAspectRatio() const
    {
        return m_impl && m_impl->sceneTargetEnabled && m_impl->sceneHeight > 0 ? static_cast<float>(m_impl->sceneWidth) / static_cast<float>(m_impl->sceneHeight) : m_aspect;
    }

    void SdlGlRenderDevice::BeginShadowPass(const FrameView& view)
    {
        m_impl->view = view;
        m_impl->BindFramebuffer(GL_FRAMEBUFFER, m_impl->shadowFramebuffer); glViewport(0, 0, m_impl->shadowResolution, m_impl->shadowResolution);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
        glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(2.0f, 4.0f); glClear(GL_DEPTH_BUFFER_BIT); m_impl->UseProgram(m_impl->shadowProgram);
    }

    void SdlGlRenderDevice::DrawShadow(const RenderItem& item)
    {
        const auto* mesh = m_impl->GetOrCreateMesh(item.mesh); if (!mesh) return;
        const Mat4 lightMvp = m_impl->view.lightViewProjection * item.model;
        m_impl->UniformMatrix4fv(m_impl->shadowMvpLocation, 1, GL_FALSE, lightMvp.Data());
        m_impl->BindVertexArray(mesh->vao); glDrawElements(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT, nullptr); m_impl->BindVertexArray(0);
    }

    void SdlGlRenderDevice::EndShadowPass()
    {
        glDisable(GL_POLYGON_OFFSET_FILL); glCullFace(GL_BACK); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); m_impl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void SdlGlRenderDevice::BeginFrame(const FrameView& view)
    {
        m_impl->view = view;
        if (m_impl->sceneTargetEnabled && m_impl->sceneFramebuffer)
        {
            m_impl->BindFramebuffer(GL_FRAMEBUFFER, m_impl->sceneFramebuffer); glViewport(0, 0, m_impl->sceneWidth, m_impl->sceneHeight);
        }
        else
        {
            m_impl->BindFramebuffer(GL_FRAMEBUFFER, 0); int width = 0, height = 0; SDL_GL_GetDrawableSize(m_impl->window, &width, &height); glViewport(0, 0, width, height);
        }
        glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glClearColor(0.04f, 0.07f, 0.11f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_impl->UseProgram(m_impl->program);
        m_impl->UniformMatrix4fv(m_impl->lightViewProjectionLocation, 1, GL_FALSE, view.lightViewProjection.Data());
        m_impl->Uniform3f(m_impl->lightDirectionLocation, view.lightDirection.x, view.lightDirection.y, view.lightDirection.z);
        m_impl->Uniform3f(m_impl->lightColorLocation, view.lightColor.x, view.lightColor.y, view.lightColor.z);
        m_impl->Uniform1f(m_impl->lightIntensityLocation, view.lightIntensity);
        m_impl->Uniform3f(m_impl->cameraPositionLocation, view.cameraPosition.x, view.cameraPosition.y, view.cameraPosition.z);
        m_impl->Uniform1i(m_impl->textureLocation, 0);
        m_impl->Uniform1i(m_impl->shadowMapLocation, 1);
        m_impl->ActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_impl->shadowDepth); m_impl->ActiveTexture(GL_TEXTURE0);
    }

    void SdlGlRenderDevice::Draw(const RenderItem& item)
    {
        if (item.transparent) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }
        else { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
        const auto* mesh = m_impl->GetOrCreateMesh(item.mesh); if (!mesh) return;
        const Mat4 mvp = m_impl->view.projection * m_impl->view.view * item.model;
        const Vec4 color = item.material ? item.material->GetBaseColor() : Vec4{0.3f, 0.7f, 1.0f, 1.0f};
        const float metallic = item.material ? item.material->GetMetallic() : 0.0f;
        const float roughness = item.material ? item.material->GetRoughness() : 0.7f;
        const Vec3 emissive = item.material ? item.material->GetEmissive() : Vec3{};
        m_impl->UniformMatrix4fv(m_impl->mvpLocation, 1, GL_FALSE, mvp.Data());
        m_impl->UniformMatrix4fv(m_impl->modelLocation, 1, GL_FALSE, item.model.Data());
        m_impl->Uniform4f(m_impl->baseColorLocation, color.x, color.y, color.z, color.w);
        m_impl->Uniform1f(m_impl->metallicLocation, metallic); m_impl->Uniform1f(m_impl->roughnessLocation, roughness);
        m_impl->Uniform3f(m_impl->emissiveLocation, emissive.x, emissive.y, emissive.z);
        const GLuint texture = m_impl->GetOrCreateTexture(item.texture);
        m_impl->ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
        m_impl->Uniform1i(m_impl->hasTextureLocation, texture != 0 ? 1 : 0);
        m_impl->BindVertexArray(mesh->vao); glDrawElements(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT, nullptr); m_impl->BindVertexArray(0);
    }

    void SdlGlRenderDevice::EndFrame()
    {
        if (!m_impl->sceneTargetEnabled) return;
        m_impl->BindFramebuffer(GL_FRAMEBUFFER, m_impl->toneFramebuffer); glViewport(0, 0, m_impl->sceneWidth, m_impl->sceneHeight);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); m_impl->UseProgram(m_impl->toneProgram);
        m_impl->ActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m_impl->sceneHdrColor);
        m_impl->Uniform1i(m_impl->toneTextureLocation, 2); m_impl->Uniform1f(m_impl->exposureLocation, m_impl->exposure);
        m_impl->BindVertexArray(m_impl->fullScreenVao); glDrawArrays(GL_TRIANGLES, 0, 3); m_impl->BindVertexArray(0);
        m_impl->ActiveTexture(GL_TEXTURE0); glEnable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST);
        m_impl->BindFramebuffer(GL_FRAMEBUFFER, 0);
        int width = 0, height = 0; SDL_GL_GetDrawableSize(m_impl->window, &width, &height); glViewport(0, 0, width, height);
        glDepthMask(GL_TRUE); glDisable(GL_SCISSOR_TEST); glClearColor(0.025f, 0.03f, 0.04f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    void SdlGlRenderDevice::DrawUi(const std::vector<UIDrawCommand>& commands)
    {
        if (!m_impl || !m_impl->window) return;
        int width = 0, height = 0;
        if (m_impl->sceneTargetEnabled) { width = m_impl->sceneWidth; height = m_impl->sceneHeight; }
        else SDL_GL_GetDrawableSize(m_impl->window, &width, &height);
        glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_SCISSOR_TEST);
        for (const auto& command : commands)
        {
            if (command.type == UIWidgetType::Label || command.rect.width <= 0.0f || command.rect.height <= 0.0f) continue;
            glScissor(static_cast<GLint>(command.rect.x), height - static_cast<GLint>(command.rect.y + command.rect.height), static_cast<GLsizei>(command.rect.width), static_cast<GLsizei>(command.rect.height));
            glClearColor(command.color.x, command.color.y, command.color.z, command.color.w); glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
    }
    void SdlGlRenderDevice::Present() { if (m_impl && m_impl->window) SDL_GL_SwapWindow(m_impl->window); }
    void* SdlGlRenderDevice::GetNativeWindow() const { return m_impl ? m_impl->window : nullptr; }
    void* SdlGlRenderDevice::GetGlContext() const { return m_impl ? m_impl->context : nullptr; }
    void SdlGlRenderDevice::SetEventCallback(std::function<void(const void*)> callback) { m_eventCallback = std::move(callback); }
    bool SdlGlRenderDevice::EnableSceneRenderTarget(const bool enabled, const int width, const int height)
    {
        if (!m_impl || !m_impl->context) return false;
        m_impl->sceneTargetEnabled = enabled;
        if (!enabled) { m_impl->DestroySceneTarget(); return true; }
        return m_impl->CreateSceneTarget(width, height);
    }
    bool SdlGlRenderDevice::ResizeSceneRenderTarget(const int width, const int height)
    {
        if (!m_impl || !m_impl->sceneTargetEnabled) return false;
        if (m_impl->sceneWidth == width && m_impl->sceneHeight == height) return true;
        return m_impl->CreateSceneTarget(width, height);
    }
    std::uintptr_t SdlGlRenderDevice::GetSceneColorTexture() const { return m_impl ? static_cast<std::uintptr_t>(m_impl->sceneColor) : 0; }
    int SdlGlRenderDevice::GetSceneTargetWidth() const { return m_impl ? m_impl->sceneWidth : 0; }
    int SdlGlRenderDevice::GetSceneTargetHeight() const { return m_impl ? m_impl->sceneHeight : 0; }
    bool SdlGlRenderDevice::ReloadShaders() { return m_impl && m_impl->ReloadDefaultShadersFromFiles(); }
    void SdlGlRenderDevice::SetExposure(const float exposure) { if (m_impl) m_impl->exposure = std::clamp(exposure, 0.05f, 16.0f); }
    float SdlGlRenderDevice::GetExposure() const { return m_impl ? m_impl->exposure : 1.0f; }
}
