cmake_minimum_required(VERSION 3.20)
project(TinyEngine VERSION 0.2.0 LANGUAGES CXX)

option(TINYENGINE_BUILD_TESTS "Build TinyEngine tests" ON)
option(TINYENGINE_BUILD_DEMO "Build TinyEngine demo" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 CONFIG QUIET)
if (NOT SDL2_FOUND)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL2
        URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.32.10.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL2)
endif()

find_package(OpenGL REQUIRED)

add_library(tinyengine STATIC
    src/Core/Logger.cpp
    src/Core/Config.cpp
    src/Core/Module.cpp
    src/Platform/Platform.cpp
    src/Memory/Allocators.cpp
    src/Task/JobSystem.cpp
    src/Resource/Resource.cpp
    src/Resource/AssetRegistry.cpp
    src/Resource/ResourceManager.cpp
    src/ECS/ECS.cpp
    src/ECS/SceneSerializer.cpp
    src/ECS/Systems.cpp
    src/Render/Renderer.cpp
    src/Render/SdlGlRenderDevice.cpp
    src/Physics/PhysicsWorld.cpp
    src/Audio/AudioEngine.cpp
    src/Input/InputSystem.cpp
    src/Script/ScriptEngine.cpp
    src/Runtime/Engine.cpp
)

target_include_directories(tinyengine PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
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

if(TINYENGINE_BUILD_DEMO)
    add_executable(tinyengine_demo src/main.cpp)
    target_link_libraries(tinyengine_demo PRIVATE tinyengine)
endif()

if(TINYENGINE_BUILD_TESTS)
    enable_testing()
    add_executable(tinyengine_tests tests/EngineTests.cpp)
    target_link_libraries(tinyengine_tests PRIVATE tinyengine)
    add_test(NAME tinyengine_core COMMAND tinyengine_tests)
    set_tests_properties(tinyengine_core PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

include(GNUInstallDirs)
install(TARGETS tinyengine ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(TINYENGINE_BUILD_DEMO)
    install(TARGETS tinyengine_demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/tinyengine/assets)

set(CPACK_PACKAGE_NAME "TinyEngine")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
