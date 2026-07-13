cmake_minimum_required(VERSION 3.20)
project(TinyEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 CONFIG QUIET)
if (NOT SDL2_FOUND)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL2
        URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.32.10.zip
    )
    FetchContent_MakeAvailable(SDL2)
endif()

find_package(OpenGL REQUIRED)

add_library(tinyengine STATIC
    src/Core/Logger.cpp
    src/Memory/Allocators.cpp
    src/Task/JobSystem.cpp
    src/Resource/Resource.cpp
    src/Resource/ResourceManager.cpp
    src/ECS/ECS.cpp
    src/ECS/Systems.cpp
    src/Render/Renderer.cpp
    src/Render/SdlGlRenderDevice.cpp
    src/Script/ScriptEngine.cpp
    src/Runtime/Engine.cpp
)

target_include_directories(tinyengine PUBLIC include)
if (TARGET SDL2::SDL2)
    target_link_libraries(tinyengine PUBLIC SDL2::SDL2 OpenGL::GL)
elseif(TARGET SDL2-static)
    target_link_libraries(tinyengine PUBLIC SDL2-static OpenGL::GL)
elseif(TARGET SDL2)
    target_link_libraries(tinyengine PUBLIC SDL2 OpenGL::GL)
else()
    message(FATAL_ERROR "SDL2 target not found")
endif()

if(MSVC)
    target_compile_options(tinyengine PRIVATE /W4 /permissive-)
else()
    target_compile_options(tinyengine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(tinyengine_demo src/main.cpp)
target_link_libraries(tinyengine_demo PRIVATE tinyengine)
