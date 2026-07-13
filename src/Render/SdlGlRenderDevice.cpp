#include "MiniEngine/Render/SdlGlRenderDevice.hpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include <cmath>
#include <cstdint>
#include <utility>

namespace MiniEngine
{
    struct SdlGlRenderDevice::Impl
    {
        SDL_Window* window = nullptr;
        SDL_GLContext context = nullptr;
    };

    SdlGlRenderDevice::SdlGlRenderDevice(const int width, const int height, std::string title)
        : m_impl(std::make_unique<Impl>()),
          m_width(width),
          m_height(height),
          m_title(std::move(title))
    {
    }

    SdlGlRenderDevice::~SdlGlRenderDevice()
    {
        Shutdown();
    }

    bool SdlGlRenderDevice::Initialize()
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0)
        {
            return false;
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        m_impl->window = SDL_CreateWindow(
            m_title.c_str(),
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            m_width,
            m_height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);

        if (m_impl->window == nullptr)
        {
            SDL_Quit();
            return false;
        }

        m_impl->context = SDL_GL_CreateContext(m_impl->window);
        if (m_impl->context == nullptr)
        {
            SDL_DestroyWindow(m_impl->window);
            m_impl->window = nullptr;
            SDL_Quit();
            return false;
        }

        SDL_GL_SetSwapInterval(1);

        int width = 0;
        int height = 0;
        SDL_GetWindowSize(m_impl->window, &width, &height);
        m_aspect = (height > 0) ? static_cast<float>(width) / static_cast<float>(height) : (16.0f / 9.0f);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);

        return true;
    }

    void SdlGlRenderDevice::Shutdown()
    {
        if (m_impl == nullptr)
        {
            return;
        }

        if (m_impl->context != nullptr)
        {
            SDL_GL_DeleteContext(m_impl->context);
            m_impl->context = nullptr;
        }

        if (m_impl->window != nullptr)
        {
            SDL_DestroyWindow(m_impl->window);
            m_impl->window = nullptr;
        }

        SDL_Quit();
    }

    bool SdlGlRenderDevice::PumpEvents()
    {
        if (m_impl == nullptr || m_impl->window == nullptr)
        {
            return false;
        }

        SDL_Event event{};
        while (SDL_PollEvent(&event) != 0)
        {
            if (event.type == SDL_QUIT)
            {
                m_inputState.quitRequested = true;
                return false;
            }

            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                const int width = event.window.data1;
                const int height = event.window.data2;
                if (height > 0)
                {
                    m_aspect = static_cast<float>(width) / static_cast<float>(height);
                }
                glViewport(0, 0, width, height);
            }
        }

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const auto axis = [keys](const SDL_Scancode negativeKey, const SDL_Scancode positiveKey)
        {
            const float negative = keys[negativeKey] != 0 ? 1.0f : 0.0f;
            const float positive = keys[positiveKey] != 0 ? 1.0f : 0.0f;
            return positive - negative;
        };

        m_inputState.moveX = axis(SDL_SCANCODE_A, SDL_SCANCODE_D);
        m_inputState.moveY = axis(SDL_SCANCODE_F, SDL_SCANCODE_R);
        m_inputState.moveZ = axis(SDL_SCANCODE_S, SDL_SCANCODE_W);

        m_inputState.rotateX = axis(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
        m_inputState.rotateY = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
        m_inputState.rotateZ = axis(SDL_SCANCODE_Q, SDL_SCANCODE_E);

        m_inputState.scale = axis(SDL_SCANCODE_X, SDL_SCANCODE_Z);
        m_inputState.sprint = keys[SDL_SCANCODE_LSHIFT] != 0 || keys[SDL_SCANCODE_RSHIFT] != 0;
        m_inputState.quitRequested = keys[SDL_SCANCODE_ESCAPE] != 0;

        if (m_inputState.quitRequested)
        {
            return false;
        }

        return true;
    }

    const InputState& SdlGlRenderDevice::GetInputState() const
    {
        return m_inputState;
    }

    void SdlGlRenderDevice::BeginFrame()
    {
        glClearColor(0.08f, 0.12f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        const float nearPlane = 0.1f;
        const float farPlane = 100.0f;
        const float fovRadians = 60.0f * 3.1415926f / 180.0f;
        const float top = std::tan(fovRadians * 0.5f) * nearPlane;
        const float right = top * m_aspect;
        glFrustum(-right, right, -top, top, nearPlane, farPlane);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -8.0f);
    }

    void SdlGlRenderDevice::Draw(const RenderItem& item)
    {
        const std::uint32_t id = item.entity;
        const float r = static_cast<float>((id * 97u) % 255u) / 255.0f;
        const float g = static_cast<float>((id * 57u) % 255u) / 255.0f;
        const float b = static_cast<float>((id * 17u) % 255u) / 255.0f;
        glColor3f(r, g, b);

        glPushMatrix();
        glTranslatef(item.worldX, item.worldY, item.worldZ);
        glRotatef(item.rotationX, 1.0f, 0.0f, 0.0f);
        glRotatef(item.rotationY, 0.0f, 1.0f, 0.0f);
        glRotatef(item.rotationZ, 0.0f, 0.0f, 1.0f);
        glScalef(item.scaleX, item.scaleY, item.scaleZ);

        const float size = 0.35f;
        glBegin(GL_TRIANGLES);
        glVertex3f(0.0f, size, 0.0f);
        glVertex3f(-size, -size, 0.0f);
        glVertex3f(size, -size, 0.0f);
        glEnd();

        glPopMatrix();
    }

    void SdlGlRenderDevice::EndFrame()
    {
    }

    void SdlGlRenderDevice::Present()
    {
        if (m_impl != nullptr && m_impl->window != nullptr)
        {
            SDL_GL_SwapWindow(m_impl->window);
        }
    }
}
