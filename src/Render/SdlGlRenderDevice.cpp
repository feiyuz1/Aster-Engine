#include "MiniEngine/Render/SdlGlRenderDevice.hpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <cstddef>
#include <cstdint>
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
            uniform mat4 uMvp;
            uniform mat4 uModel;
            out vec3 vNormal;
            void main()
            {
                gl_Position = uMvp * vec4(aPosition, 1.0);
                vNormal = normalize(mat3(uModel) * aNormal);
            }
        )GLSL";

        constexpr const char* FragmentShaderSource = R"GLSL(
            #version 330 core
            in vec3 vNormal;
            uniform vec4 uBaseColor;
            uniform vec3 uLightDirection;
            uniform vec3 uLightColor;
            uniform float uLightIntensity;
            out vec4 outColor;
            void main()
            {
                float diffuse = max(dot(normalize(vNormal), normalize(-uLightDirection)), 0.0);
                vec3 lighting = vec3(0.16) + uLightColor * diffuse * uLightIntensity;
                outColor = vec4(uBaseColor.rgb * lighting, uBaseColor.a);
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

        SDL_Window* window = nullptr;
        SDL_GLContext context = nullptr;
        InputState input;
        FrameView view;
        GLuint program = 0;
        GLint mvpLocation = -1, modelLocation = -1, baseColorLocation = -1;
        GLint lightDirectionLocation = -1, lightColorLocation = -1, lightIntensityLocation = -1;

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

        struct GpuMesh { GLuint vao = 0, vertexBuffer = 0, indexBuffer = 0; GLsizei indexCount = 0; };
        std::unordered_map<std::string, GpuMesh> meshes;

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
                LoadGl(Uniform4f, "glUniform4f") && LoadGl(Uniform3f, "glUniform3f") && LoadGl(Uniform1f, "glUniform1f");
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

        bool CreateDefaultProgram()
        {
            const GLuint vertex = Compile(GL_VERTEX_SHADER, VertexShaderSource);
            const GLuint fragment = Compile(GL_FRAGMENT_SHADER, FragmentShaderSource);
            if (!vertex || !fragment) return false;
            program = CreateProgram(); AttachShader(program, vertex); AttachShader(program, fragment); LinkProgram(program);
            DeleteShader(vertex); DeleteShader(fragment);
            GLint success = GL_FALSE; GetProgramiv(program, GL_LINK_STATUS, &success);
            if (success != GL_TRUE)
            {
                GLchar log[1024]{}; GetProgramInfoLog(program, sizeof(log), nullptr, log);
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Program link failed: %s", log);
                return false;
            }
            mvpLocation = GetUniformLocation(program, "uMvp"); modelLocation = GetUniformLocation(program, "uModel");
            baseColorLocation = GetUniformLocation(program, "uBaseColor"); lightDirectionLocation = GetUniformLocation(program, "uLightDirection");
            lightColorLocation = GetUniformLocation(program, "uLightColor"); lightIntensityLocation = GetUniformLocation(program, "uLightIntensity");
            return true;
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
            mesh.indexCount = static_cast<GLsizei>(indices.size());
            return &meshes.emplace(resource->GetPath(), mesh).first->second;
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
        if (!m_impl->context || !m_impl->LoadFunctions() || !m_impl->CreateDefaultProgram()) { Shutdown(); return false; }
        SDL_GL_SetSwapInterval(1); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
        return true;
    }

    void SdlGlRenderDevice::Shutdown()
    {
        if (!m_impl) return;
        if (m_impl->context)
        {
            for (const auto& pair : m_impl->meshes)
            {
                m_impl->DeleteBuffers(1, &pair.second.vertexBuffer); m_impl->DeleteBuffers(1, &pair.second.indexBuffer);
                m_impl->DeleteVertexArrays(1, &pair.second.vao);
            }
            m_impl->meshes.clear();
            if (m_impl->program) { m_impl->DeleteProgram(m_impl->program); m_impl->program = 0; }
            SDL_GL_DeleteContext(m_impl->context); m_impl->context = nullptr;
        }
        if (m_impl->window) { SDL_DestroyWindow(m_impl->window); m_impl->window = nullptr; }
        SDL_Quit();
    }

    bool SdlGlRenderDevice::PumpEvents()
    {
        m_impl->input.mouseX = m_impl->input.mouseY = 0.0f;
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT) return false;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) glViewport(0, 0, event.window.data1, event.window.data2);
            if (event.type == SDL_MOUSEMOTION) { m_impl->input.mouseX += static_cast<float>(event.motion.xrel); m_impl->input.mouseY += static_cast<float>(event.motion.yrel); }
        }
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const auto axis = [keys](SDL_Scancode negative, SDL_Scancode positive) { return (keys[positive] ? 1.0f : 0.0f) - (keys[negative] ? 1.0f : 0.0f); };
        m_impl->input.moveX = axis(SDL_SCANCODE_A, SDL_SCANCODE_D); m_impl->input.moveY = axis(SDL_SCANCODE_F, SDL_SCANCODE_R); m_impl->input.moveZ = axis(SDL_SCANCODE_S, SDL_SCANCODE_W);
        m_impl->input.rotateX = axis(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN); m_impl->input.rotateY = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT); m_impl->input.rotateZ = axis(SDL_SCANCODE_Q, SDL_SCANCODE_E);
        m_impl->input.scale = axis(SDL_SCANCODE_X, SDL_SCANCODE_Z); m_impl->input.sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        m_impl->input.primaryAction = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        m_impl->input.quitRequested = keys[SDL_SCANCODE_ESCAPE] != 0;
        return !m_impl->input.quitRequested;
    }

    const InputState& SdlGlRenderDevice::GetInputState() const { return m_impl->input; }

    void SdlGlRenderDevice::BeginFrame(const FrameView& view)
    {
        m_impl->view = view; glClearColor(0.04f, 0.07f, 0.11f, 1.0f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_impl->UseProgram(m_impl->program);
        m_impl->Uniform3f(m_impl->lightDirectionLocation, view.lightDirection.x, view.lightDirection.y, view.lightDirection.z);
        m_impl->Uniform3f(m_impl->lightColorLocation, view.lightColor.x, view.lightColor.y, view.lightColor.z);
        m_impl->Uniform1f(m_impl->lightIntensityLocation, view.lightIntensity);
    }

    void SdlGlRenderDevice::Draw(const RenderItem& item)
    {
        const auto* mesh = m_impl->GetOrCreateMesh(item.mesh); if (!mesh) return;
        const Mat4 mvp = m_impl->view.projection * m_impl->view.view * item.model;
        const Vec4 color = item.material ? item.material->GetBaseColor() : Vec4{0.3f, 0.7f, 1.0f, 1.0f};
        m_impl->UniformMatrix4fv(m_impl->mvpLocation, 1, GL_FALSE, mvp.Data());
        m_impl->UniformMatrix4fv(m_impl->modelLocation, 1, GL_FALSE, item.model.Data());
        m_impl->Uniform4f(m_impl->baseColorLocation, color.x, color.y, color.z, color.w);
        m_impl->BindVertexArray(mesh->vao); glDrawElements(GL_TRIANGLES, mesh->indexCount, GL_UNSIGNED_INT, nullptr); m_impl->BindVertexArray(0);
    }

    void SdlGlRenderDevice::EndFrame() {}
    void SdlGlRenderDevice::Present() { if (m_impl && m_impl->window) SDL_GL_SwapWindow(m_impl->window); }
}
