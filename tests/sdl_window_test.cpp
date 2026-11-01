#define SDL_MAIN_HANDLED
#include "sdl_window.h"
#include <SDL.h>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main()
{
    require(SDL_Init(SDL_INIT_EVENTS) == 0, "external SDL event consumer initializes");
    tinyengine::SdlWindow headless;
    bool headlessClose = false;
    headless.closeRequestCallback = [&] { headlessClose = true; };
    SDL_Event headlessQuit{};
    headlessQuit.type = SDL_QUIT;
    SDL_PushEvent(&headlessQuit);
    require(headless.pumpEvents({}) && headlessClose, "close callback works before window initialization");
    tinyengine::SdlWindow window;
    bool videoReadyBeforeWindow = false;
    window.beforeCreateWindow = [&]
    { videoReadyBeforeWindow = (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0 && !window.get(); };
    require(window.initialize(320, 200, "SDL platform regression", SDL_WINDOW_HIDDEN), "window initializes");
    require(videoReadyBeforeWindow, "graphics attributes can be set after video startup and before window creation");
    window.escapeQuits = false;
    window.pumpEvents({640, 400});
    SDL_Event key{};
    key.type = SDL_KEYDOWN;
    key.key.keysym.scancode = SDL_SCANCODE_R;
    SDL_PushEvent(&key);
    SDL_Event text{};
    text.type = SDL_TEXTINPUT;
    SDL_strlcpy(text.text.text, "input", sizeof(text.text.text));
    SDL_PushEvent(&text);
    SDL_Event mouse{};
    mouse.type = SDL_MOUSEBUTTONDOWN;
    mouse.button.button = SDL_BUTTON_LEFT;
    mouse.button.x = 40;
    mouse.button.y = 25;
    SDL_PushEvent(&mouse);
    require(window.pumpEvents({640, 400}), "events do not request exit");
    require(window.input.keysPressed[SDL_SCANCODE_R], "key edge survives a short press");
    require(window.input.moveY == 1.0f, "mapped action sees the short press");
    require(window.input.textInput == "input", "text input is preserved");
    require(window.input.textEvents.size() == 1 &&
                window.input.textEvents.front().type == tinyengine::TextInputEvent::Type::Commit,
            "Unicode input retains ordered text events after platform extraction");
    SDL_Event composition{};
    composition.type = SDL_TEXTEDITING;
    SDL_strlcpy(composition.edit.text, "ni", sizeof(composition.edit.text));
    composition.edit.start = 1;
    composition.edit.length = 1;
    SDL_PushEvent(&composition);
    require(window.input.primaryPressed && window.input.cursorX == 80 && window.input.cursorY == 50,
            "click position scales into render viewport");
    require(window.pumpEvents({640, 400}), "following frame runs");
    require(!window.input.keysPressed[SDL_SCANCODE_R] && window.input.textInput.empty(), "edges reset each frame");
    require(window.input.compositionChanged && window.input.compositionText == "ni" &&
                window.input.compositionCursor == 1 && window.input.textEvents.size() == 1,
            "IME preedit and cursor survive both backend input paths");
    bool closeRequested = false;
    window.closeRequestCallback = [&] { closeRequested = true; };
    SDL_Event quit{};
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit);
    require(window.pumpEvents({640, 400}) && closeRequested, "editor close callback keeps process alive");
    window.closeRequestCallback = {};
    SDL_PushEvent(&quit);
    require(!window.pumpEvents({640, 400}), "standalone close exits loop");
    window.shutdown();
    window.shutdown();
    require((SDL_WasInit(SDL_INIT_EVENTS) & SDL_INIT_EVENTS) != 0, "shutdown preserves external SDL references");
    tinyengine::SdlWindow graphics;
    graphics.beforeCreateWindow = []
    {
        require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) == 0, "GL major version config succeeds");
        require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) == 0, "GL minor version config succeeds");
        require(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) == 0,
                "GL core profile config succeeds");
    };
    require(graphics.initialize(64, 64, "GL attributes regression", SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN),
            "graphics window initializes");
    auto context = SDL_GL_CreateContext(graphics.get());
    require(context != nullptr, "configured graphics context initializes");
    int major = 0, minor = 0, profile = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &profile);
    require((major > 4 || (major == 4 && minor >= 3)) && profile == SDL_GL_CONTEXT_PROFILE_CORE,
            "requested OpenGL 4.3 core context survives common window creation");
    SDL_GL_DeleteContext(context);
    graphics.shutdown();
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    std::cout << "SDL platform regression passed\n";
}
