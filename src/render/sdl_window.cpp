#define SDL_MAIN_HANDLED
#include "sdl_window.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace tinyengine
{
SdlWindow::~SdlWindow()
{
    shutdown();
}

bool SdlWindow::initialize(int width, int height, const std::string& title, std::uint32_t flags)
{
    if (m_window)
        return true;
    SDL_SetMainReady();
    constexpr auto subsystems = SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO;
    if (SDL_InitSubSystem(subsystems) != 0)
        return false;
    m_initializedSubsystems = subsystems;
    if (beforeCreateWindow)
        beforeCreateWindow();
    m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (!m_window)
    {
        shutdown();
        return false;
    }
    updateDpiScale();
    SDL_StopTextInput();
    return true;
}

void SdlWindow::updateDpiScale()
{
    float displayDpi = 0.0f;
    const int display = SDL_GetWindowDisplayIndex(m_window);
    if (display >= 0 && SDL_GetDisplayDPI(display, &displayDpi, nullptr, nullptr) == 0 && displayDpi > 0.0f)
        dpiScale = std::clamp(displayDpi / 96.0f, 1.0f, 4.0f);
}

void SdlWindow::shutdown()
{
    if (m_controller)
        SDL_GameControllerClose(m_controller);
    m_controller = nullptr;
    if (m_window)
    {
        setMouseCapture(false);
        SDL_DestroyWindow(m_window);
    }
    m_window = nullptr;
    // Balance our SDL references without shutting down other live SDL consumers.
    if (m_initializedSubsystems)
        SDL_QuitSubSystem(m_initializedSubsystems);
    m_initializedSubsystems = 0;
    input = {};
    m_previousUiNext = m_previousUiAccept = false;
}

bool SdlWindow::pumpEvents(const Vec2 uiViewport)
{
    input.mouseX = input.mouseY = 0.0f;
    input.textInput.clear();
    input.textEvents.clear();
    input.uiBackspace = false;
    input.compositionChanged = false;
    input.uiDelete = false;
    input.uiCursorLeft = input.uiCursorRight = false;
    input.uiHome = input.uiEnd = input.uiSelectAll = false;
    bool mousePressed = false;
    std::array<bool, SDL_NUM_SCANCODES> pressedThisFrame{};
    // 按下沿单独记录并过滤系统按键重复，避免按住换弹/暂停键每帧反复触发。
    std::array<bool, SDL_NUM_SCANCODES> pressedEdges{};
    int pressX = 0, pressY = 0;
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (eventCallback)
            eventCallback(&event);
        if (event.type == SDL_QUIT)
        {
            if (closeRequestCallback)
                closeRequestCallback();
            else
                return false;
        }
        if (event.type == SDL_WINDOWEVENT && m_window && event.window.windowID == SDL_GetWindowID(m_window))
        {
            if (event.window.event == SDL_WINDOWEVENT_CLOSE)
            {
                if (closeRequestCallback)
                    closeRequestCallback();
                else
                    return false;
            }
            if (event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED)
                updateDpiScale();
        }
        if (event.type == SDL_MOUSEMOTION)
        {
            input.mouseX += static_cast<float>(event.motion.xrel);
            input.mouseY += static_cast<float>(event.motion.yrel);
        }
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            mousePressed = true;
            pressX = event.button.x;
            pressY = event.button.y;
        }
        if (event.type == SDL_TEXTINPUT)
        {
            input.textInput += event.text.text;
            input.textEvents.push_back({TextInputEvent::Type::Commit, event.text.text});
            input.compositionText.clear();
            input.compositionChanged = true;
        }
        if (event.type == SDL_TEXTEDITING)
        {
            input.compositionText = event.edit.text;
            input.textEvents.push_back(
                {TextInputEvent::Type::Composition, event.edit.text, event.edit.start, event.edit.length});
            input.compositionCursor = event.edit.start;
            input.compositionLength = event.edit.length;
            input.compositionChanged = true;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode >= 0 &&
            event.key.keysym.scancode < SDL_NUM_SCANCODES)
        {
            pressedThisFrame[static_cast<std::size_t>(event.key.keysym.scancode)] = true;
            if (event.key.repeat == 0)
                pressedEdges[static_cast<std::size_t>(event.key.keysym.scancode)] = true;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE && event.key.repeat == 0)
            input.uiBackspace = true;
        if (event.type == SDL_KEYDOWN)
        {
            const auto code = event.key.keysym.scancode;
            input.uiDelete = input.uiDelete || code == SDL_SCANCODE_DELETE;
            input.uiCursorLeft = input.uiCursorLeft || code == SDL_SCANCODE_LEFT;
            input.uiCursorRight = input.uiCursorRight || code == SDL_SCANCODE_RIGHT;
            input.uiHome = input.uiHome || code == SDL_SCANCODE_HOME;
            input.uiEnd = input.uiEnd || code == SDL_SCANCODE_END;
            input.uiSelectAll =
                input.uiSelectAll || (code == SDL_SCANCODE_A && (event.key.keysym.mod & KMOD_CTRL) != 0);
            input.uiExtendSelection = (event.key.keysym.mod & KMOD_SHIFT) != 0;
        }
        if (event.type == SDL_CONTROLLERDEVICEADDED && !m_controller)
            m_controller = SDL_GameControllerOpen(event.cdevice.which);
        if (event.type == SDL_CONTROLLERDEVICEREMOVED && m_controller &&
            SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(m_controller)) == event.cdevice.which)
        {
            SDL_GameControllerClose(m_controller);
            m_controller = nullptr;
        }
    }
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    // 同一帧内按下又松开的短按也要交给游戏处理，持续按住仍以 SDL 当前状态为准。
    const auto down = [keys, &pressedThisFrame](SDL_Scancode code)
    { return keys[code] != 0 || pressedThisFrame[static_cast<std::size_t>(code)]; };
    // 逐键快照供游戏读取未映射按键；容量不足时只填充前缀，其余保持 false。
    const auto packedScancodes = std::min<std::size_t>(InputState::kMaxScancodes, SDL_NUM_SCANCODES);
    for (std::size_t index = 0; index < packedScancodes; ++index)
    {
        input.keysDown[index] = keys[index] != 0;
        input.keysPressed[index] = pressedEdges[index];
    }
    const auto axis = [&down](SDL_Scancode negative, SDL_Scancode positive)
    { return (down(positive) ? 1.0f : 0.0f) - (down(negative) ? 1.0f : 0.0f); };
    input.moveX = axis(SDL_SCANCODE_A, SDL_SCANCODE_D);
    input.moveY = axis(SDL_SCANCODE_F, SDL_SCANCODE_R);
    input.moveZ = axis(SDL_SCANCODE_S, SDL_SCANCODE_W);
    input.rotateX = axis(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN);
    input.rotateY = axis(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT);
    input.rotateZ = axis(SDL_SCANCODE_Q, SDL_SCANCODE_E);
    input.scale = axis(SDL_SCANCODE_X, SDL_SCANCODE_Z);
    input.sprint = down(SDL_SCANCODE_LSHIFT) || down(SDL_SCANCODE_RSHIFT);
    input.jump = down(SDL_SCANCODE_SPACE);
    input.primaryAction = mousePressed || (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    input.primaryPressed = mousePressed;
    int cursorX = 0, cursorY = 0;
    SDL_GetMouseState(&cursorX, &cursorY);
    if (mousePressed)
    {
        cursorX = pressX;
        cursorY = pressY;
    }
    int windowWidth = 0, windowHeight = 0;
    if (m_window)
        SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);
    input.cursorX = static_cast<float>(cursorX) * uiViewport.x / static_cast<float>(std::max(1, windowWidth));
    input.cursorY = static_cast<float>(cursorY) * uiViewport.y / static_cast<float>(std::max(1, windowHeight));
    // 编辑器通过 SetEscapeQuits(false) 关闭平台层 ESC→退出，改由 ImGui 侧处理 ESC（清空选择/关闭弹窗）。
    input.quitRequested = (escapeQuits && down(SDL_SCANCODE_ESCAPE));
    if (!m_controller)
        for (int index = 0; index < SDL_NumJoysticks(); ++index)
            if (SDL_IsGameController(index))
            {
                m_controller = SDL_GameControllerOpen(index);
                if (m_controller)
                    break;
            }
    bool controllerNext = false, controllerAccept = false;
    if (m_controller)
    {
        const auto stick = [this](const SDL_GameControllerAxis axisValue)
        {
            constexpr float kDeadZone = 8000.0f;
            const float value = static_cast<float>(SDL_GameControllerGetAxis(m_controller, axisValue));
            if (std::abs(value) < kDeadZone)
                return 0.0f;
            return std::clamp((value - std::copysign(kDeadZone, value)) / (32767.0f - kDeadZone), -1.0f, 1.0f);
        };
        const float gamepadMoveX = stick(SDL_CONTROLLER_AXIS_LEFTX), gamepadMoveZ = -stick(SDL_CONTROLLER_AXIS_LEFTY);
        const float gamepadLookX = stick(SDL_CONTROLLER_AXIS_RIGHTX), gamepadLookY = stick(SDL_CONTROLLER_AXIS_RIGHTY);
        if (std::abs(gamepadMoveX) > std::abs(input.moveX))
            input.moveX = gamepadMoveX;
        if (std::abs(gamepadMoveZ) > std::abs(input.moveZ))
            input.moveZ = gamepadMoveZ;
        input.rotateY += gamepadLookX * 2.0f;
        input.rotateX += gamepadLookY * 2.0f;
        input.primaryAction = input.primaryAction || SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_A);
        input.sprint = input.sprint || SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        input.jump = input.jump || SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_A);
        controllerNext = SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        controllerAccept = SDL_GameControllerGetButton(m_controller, SDL_CONTROLLER_BUTTON_A);
    }
    const bool uiNext = down(SDL_SCANCODE_TAB) || controllerNext,
               uiAccept = down(SDL_SCANCODE_RETURN) || down(SDL_SCANCODE_SPACE) || controllerAccept;
    input.uiNavigateNext = uiNext && !m_previousUiNext;
    input.uiAccept = uiAccept && !m_previousUiAccept;
    m_previousUiNext = uiNext;
    m_previousUiAccept = uiAccept;
    return !input.quitRequested;
}

void SdlWindow::setTextInputEnabled(bool enabled)
{
    if (!m_window || (SDL_IsTextInputActive() == SDL_TRUE) == enabled)
        return;
    if (enabled)
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
}

void SdlWindow::setMouseCapture(bool captured)
{
    if (m_window && SDL_SetRelativeMouseMode(captured ? SDL_TRUE : SDL_FALSE) == 0)
        SDL_ShowCursor(captured ? SDL_DISABLE : SDL_ENABLE);
}

void SdlWindow::setTextInputRect(const UIRect& rect, const Vec2 viewport)
{
    if (!m_window || rect.width <= 0 || rect.height <= 0)
        return;
    int windowWidth = 0, windowHeight = 0;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);
    const float scaleX = static_cast<float>(windowWidth) / std::max(1.0f, viewport.x);
    const float scaleY = static_cast<float>(windowHeight) / std::max(1.0f, viewport.y);
    SDL_Rect inputRect{static_cast<int>(rect.x * scaleX), static_cast<int>(rect.y * scaleY),
                       std::max(1, static_cast<int>(rect.width * scaleX)),
                       std::max(1, static_cast<int>(rect.height * scaleY))};
    SDL_SetTextInputRect(&inputRect);
}

bool SdlWindow::isMouseCaptured() const
{
    return m_window && SDL_GetRelativeMouseMode() == SDL_TRUE;
}

bool SdlWindow::rumbleController(float low, float high, std::uint32_t milliseconds)
{
    if (!m_controller)
        return false;
    const auto motor = [](float value)
    { return static_cast<Uint16>(std::clamp(value, 0.0f, 1.0f) * std::numeric_limits<Uint16>::max()); };
    return SDL_GameControllerRumble(m_controller, motor(low), motor(high), milliseconds) == 0;
}

bool SdlWindow::isControllerConnected() const
{
    return m_controller && SDL_GameControllerGetAttached(m_controller) == SDL_TRUE;
}
} // namespace tinyengine
