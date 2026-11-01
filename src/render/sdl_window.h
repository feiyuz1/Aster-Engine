#pragma once

#include "tinyengine/render/renderer.h"
#include <cstdint>
#include <functional>
#include <string>

struct SDL_Window;
struct _SDL_GameController;

namespace tinyengine
{
// Common SDL lifetime and input semantics for both graphics backends.
class SdlWindow
{
  public:
    ~SdlWindow();
    bool initialize(int width, int height, const std::string& title, std::uint32_t flags);
    void shutdown();
    bool pumpEvents(Vec2 viewport);
    SDL_Window* get() const
    {
        return m_window;
    }
    InputState input;
    float dpiScale = 1.0f;
    bool escapeQuits = true;
    std::function<void(const void*)> eventCallback;
    std::function<void()> closeRequestCallback;
    // Graphics configuration runs after SDL video startup and before window creation.
    std::function<void()> beforeCreateWindow;
    void setTextInputEnabled(bool enabled);
    void setTextInputRect(const UIRect&, Vec2 viewport);
    void setMouseCapture(bool captured);
    bool isMouseCaptured() const;
    bool rumbleController(float low, float high, std::uint32_t milliseconds);
    bool isControllerConnected() const;

  private:
    void updateDpiScale();
    SDL_Window* m_window = nullptr;
    _SDL_GameController* m_controller = nullptr;
    std::uint32_t m_initializedSubsystems = 0;
    bool m_previousUiNext = false, m_previousUiAccept = false;
};
} // namespace tinyengine
