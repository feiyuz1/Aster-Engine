cmake_minimum_required(VERSION 3.20)
project(TinyEngine VERSION 0.5.0 LANGUAGES C CXX)

option(TINYENGINE_BUILD_TESTS "Build TinyEngine tests" ON)
option(TINYENGINE_BUILD_DEMO "Build TinyEngine demo" ON)
option(TINYENGINE_BUILD_EDITOR "Build TinyEngine editor" ON)
option(TINYENGINE_BUILD_IMPORTERS "Build Assimp based asset importers" ON)

set(TINYENGINE_ASSIMP_DIR "D:/OpenSourceProject/assimp-master" CACHE PATH "Path to the Assimp source tree")
set(TINYENGINE_IMGUI_DIR "D:/OpenSourceProject/imgui-master" CACHE PATH "Path to the Dear ImGui source tree")

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL2 CONFIG QUIET)
if (NOT SDL2_FOUND)
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

if(TINYENGINE_BUILD_IMPORTERS)
    if(NOT EXISTS "${TINYENGINE_ASSIMP_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Assimp source tree not found: ${TINYENGINE_ASSIMP_DIR}")
    endif()
    set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
    set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT ON CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${TINYENGINE_ASSIMP_DIR}" "${CMAKE_BINARY_DIR}/_deps/assimp-build" EXCLUDE_FROM_ALL)
endif()

if(TINYENGINE_BUILD_EDITOR AND NOT EXISTS "${TINYENGINE_IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui source tree not found: ${TINYENGINE_IMGUI_DIR}")
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
    src/Resource/AssetCooker.cpp
    src/Resource/PakArchive.cpp
    src/Resource/ResourceManager.cpp
    src/ECS/ECS.cpp
    src/ECS/SceneSerializer.cpp
    src/ECS/Systems.cpp
    src/Project/Project.cpp
    src/Render/Renderer.cpp
    src/Render/SdlGlRenderDevice.cpp
    src/Physics/PhysicsWorld.cpp
    src/Audio/AudioEngine.cpp
    src/Input/InputSystem.cpp
    src/Script/ScriptEngine.cpp
    src/UI/UISystem.cpp
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
if(WIN32)
    target_link_libraries(tinyengine PUBLIC windowscodecs ole32)
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

if(TINYENGINE_BUILD_IMPORTERS)
    add_library(tinyengine_importers STATIC src/Import/AssimpImporter.cpp)
    target_include_directories(tinyengine_importers PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(tinyengine_importers PUBLIC tinyengine assimp::assimp)
    if(MSVC)
        target_compile_options(tinyengine_importers PRIVATE /W4 /permissive-)
    else()
        target_compile_options(tinyengine_importers PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(TINYENGINE_BUILD_EDITOR)
    add_library(tinyengine_imgui STATIC
        ${TINYENGINE_IMGUI_DIR}/imgui.cpp
        ${TINYENGINE_IMGUI_DIR}/imgui_draw.cpp
        ${TINYENGINE_IMGUI_DIR}/imgui_tables.cpp
        ${TINYENGINE_IMGUI_DIR}/imgui_widgets.cpp
        ${TINYENGINE_IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${TINYENGINE_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(tinyengine_imgui PUBLIC ${TINYENGINE_IMGUI_DIR} ${TINYENGINE_IMGUI_DIR}/backends)
    target_link_libraries(tinyengine_imgui PUBLIC tinyengine)
    add_executable(tinyengine_editor editor/EditorMain.cpp editor/EditorApp.cpp)
    target_include_directories(tinyengine_editor PRIVATE editor)
    target_link_libraries(tinyengine_editor PRIVATE tinyengine tinyengine_imgui)
    if(TINYENGINE_BUILD_IMPORTERS)
        target_link_libraries(tinyengine_editor PRIVATE tinyengine_importers)
        target_compile_definitions(tinyengine_editor PRIVATE TINYENGINE_HAS_ASSIMP=1)
    endif()
endif()

add_executable(tinyengine_asset_tool tools/AssetTool.cpp)
target_link_libraries(tinyengine_asset_tool PRIVATE tinyengine)
if(TINYENGINE_BUILD_IMPORTERS)
    target_link_libraries(tinyengine_asset_tool PRIVATE tinyengine_importers)
    target_compile_definitions(tinyengine_asset_tool PRIVATE TINYENGINE_HAS_ASSIMP=1)
endif()
add_executable(tinyengine_scene_tool tools/SceneTool.cpp)
target_link_libraries(tinyengine_scene_tool PRIVATE tinyengine)

if(TINYENGINE_BUILD_TESTS)
    enable_testing()
    add_executable(tinyengine_tests tests/EngineTests.cpp)
    target_link_libraries(tinyengine_tests PRIVATE tinyengine)
    if(TINYENGINE_BUILD_IMPORTERS)
        target_link_libraries(tinyengine_tests PRIVATE tinyengine_importers)
        target_compile_definitions(tinyengine_tests PRIVATE TINYENGINE_HAS_ASSIMP=1)
    endif()
    add_test(NAME tinyengine_core COMMAND tinyengine_tests)
    set_tests_properties(tinyengine_core PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

include(GNUInstallDirs)
install(TARGETS tinyengine ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(TINYENGINE_BUILD_DEMO)
    install(TARGETS tinyengine_demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(TINYENGINE_BUILD_EDITOR)
    install(TARGETS tinyengine_editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS tinyengine_asset_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS tinyengine_scene_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} PATTERN "Import" EXCLUDE)
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/tinyengine/assets)
install(FILES TinyEngine.tinyproject DESTINATION .)

set(CPACK_PACKAGE_NAME "TinyEngine")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
