cmake_minimum_required(VERSION 3.21)
project(Aster-Engine VERSION 0.27.0 LANGUAGES C CXX)

execute_process(COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE TINYENGINE_GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT TINYENGINE_GIT_REVISION)
    set(TINYENGINE_GIT_REVISION "source-archive")
endif()
set(TINYENGINE_BUILD_ID "${PROJECT_VERSION}-${TINYENGINE_GIT_REVISION}")
set(TINYENGINE_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")

option(TINYENGINE_BUILD_TESTS "Build Aster-Engine tests" OFF)
option(TINYENGINE_BUILD_DEMO "Build Aster-Engine demo" ON)
option(TINYENGINE_BUILD_GAME "Build standalone Aster-Engine game runtime" ON)
option(TINYENGINE_BUILD_EDITOR "Build Aster-Engine editor" ON)
option(TINYENGINE_BUILD_IMPORTERS "Build Assimp based asset importers" ON)
option(TINYENGINE_ENABLE_JOLT "Enable the optional Jolt Physics backend" OFF)

set(TINYENGINE_ASSIMP_DIR "" CACHE PATH "Path to the Assimp source tree")
set(TINYENGINE_IMGUI_DIR "" CACHE PATH "Optional path to a Dear ImGui docking source tree")
set(TINYENGINE_JOLT_DIR "" CACHE PATH "Path to the Jolt Physics source tree")
set(TINYENGINE_STB_DIR "" CACHE PATH "Path to stb single-header libraries")

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/ProjectSDK.cmake)

find_package(SDL2 CONFIG QUIET)
if (NOT SDL2_FOUND)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL2
        BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/sdl2-build"
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

if(TINYENGINE_BUILD_EDITOR)
    set(TINYENGINE_IMGUI_HAS_DOCKING OFF)
    if(EXISTS "${TINYENGINE_IMGUI_DIR}/imgui.h")
        file(STRINGS "${TINYENGINE_IMGUI_DIR}/imgui.h" TINYENGINE_IMGUI_DOCKING_DEFINE REGEX "^#define IMGUI_HAS_DOCK")
        file(STRINGS "${TINYENGINE_IMGUI_DIR}/imgui.h" TINYENGINE_IMGUI_VERSION_LINE REGEX "^#define IMGUI_VERSION_NUM +[0-9]+")
        string(REGEX MATCH "[0-9]+$" TINYENGINE_IMGUI_VERSION_NUMBER "${TINYENGINE_IMGUI_VERSION_LINE}")
        if(TINYENGINE_IMGUI_DOCKING_DEFINE AND TINYENGINE_IMGUI_VERSION_NUMBER GREATER_EQUAL 19280)
            set(TINYENGINE_IMGUI_HAS_DOCKING ON)
        endif()
    endif()
    if(NOT TINYENGINE_IMGUI_HAS_DOCKING)
        message(STATUS "Using pinned Dear ImGui v1.92.8-docking for the editor workspace")
        FetchContent_Declare(imgui_docking
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/imgui_docking-build"
            URL https://codeload.github.com/ocornut/imgui/zip/572f249ce1975f98ad9f8aabce512ffa12a52d6c
            URL_HASH SHA256=ac62c7493bb14b5197ae5c4f100b69a1e894628250bd53a97910adec3d78d5ca
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(imgui_docking)
        set(TINYENGINE_IMGUI_DIR "${imgui_docking_SOURCE_DIR}")
    endif()
endif()

if(NOT EXISTS "${TINYENGINE_STB_DIR}/stb_vorbis.c")
    FetchContent_Declare(
        stb
        BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/stb-build"
        URL https://github.com/nothings/stb/archive/refs/heads/master.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(stb)
    set(TINYENGINE_STB_DIR "${stb_SOURCE_DIR}")
endif()

find_package(OpenGL REQUIRED)
if(NOT WIN32)
    find_package(Freetype REQUIRED)
endif()

if(TINYENGINE_ENABLE_JOLT)
    if(NOT EXISTS "${TINYENGINE_JOLT_DIR}/Build/CMakeLists.txt")
        message(FATAL_ERROR "Jolt Physics source tree not found: ${TINYENGINE_JOLT_DIR}")
    endif()
    set(JPH_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(JPH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(JPH_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${TINYENGINE_JOLT_DIR}/Build" "${CMAKE_BINARY_DIR}/_deps/jolt-build" EXCLUDE_FROM_ALL)
endif()

add_library(tinyengine STATIC
    src/core/logger.cpp
    src/core/crash_reporter.cpp
    src/core/config.cpp
    src/core/module.cpp
    src/serialization/archive.cpp
    src/diagnostics/diagnostics.cpp
    src/platform/platform.cpp
    src/memory/allocators.cpp
    src/task/job_system.cpp
    src/resource/resource.cpp
    src/resource/material_definition_resource.cpp
    src/resource/material_asset_references.cpp
    src/resource/asset_registry.cpp
    src/resource/asset_cooker.cpp
    src/resource/pak_archive.cpp
    src/resource/resource_manager.cpp
    src/resource/asset_streaming.cpp
    src/resource/asset_maintenance.cpp
    src/ecs/ecs.cpp
    src/ecs/reflection.cpp
    src/ecs/scene_serializer.cpp
    src/ecs/systems.cpp
    src/ai/behavior_tree.cpp
    src/animation/skeletal_animation.cpp
    src/cinematic/sequencer.cpp
    src/media/image_sequence.cpp
    src/navigation/navigation.cpp
    src/network/replication.cpp
    src/terrain/height_field.cpp
    src/world/world_partition.cpp
    src/vfx/particle_system.cpp
    src/localization/localization.cpp
    src/gameplay/gameplay.cpp
    src/project/project.cpp
    src/project/project_module_manifest.cpp
    src/project/project_module_host.cpp
    src/project/project_host_bridge.cpp
    src/platform/dynamic_library.cpp
    src/project/project_scaffolder.cpp
    src/project/solution_generator.cpp
    src/project/sdk_paths.cpp
    src/project/project_packager.cpp
    src/plugin/plugin_manager.cpp
    src/build/target_platform.cpp
    src/build/asset_compiler.cpp
    src/build/build_compatibility.cpp
    src/automation/automation.cpp
    src/render/renderer.cpp
    src/render/render_graph.cpp
    src/render/clustered_lighting.cpp
    src/render/environment_baker.cpp
    src/render/material_graph.cpp
    src/render/material_types.cpp
    src/render/material_system.cpp
    src/render/material_compiler.cpp
    src/render/gl_material_program_cache.cpp
    src/render/shader_pipeline.cpp
    src/render/sdl_gl_render_device.cpp
    src/physics/physics_world.cpp
    src/audio/audio_engine.cpp
    src/audio/stb_vorbis.cpp
    src/input/input_system.cpp
    src/script/script_engine.cpp
    src/ui/ui_system.cpp
    src/ui/text_layout.cpp
    src/runtime/engine.cpp
)

target_include_directories(tinyengine PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_compile_features(tinyengine PUBLIC cxx_std_20)
target_include_directories(tinyengine PRIVATE "${TINYENGINE_STB_DIR}")
target_link_libraries(tinyengine PRIVATE tinyengine_project_sdk)
target_compile_definitions(tinyengine PRIVATE TINYENGINE_CMAKE_EXECUTABLE="${CMAKE_COMMAND}")

# 外部着色器不可用时，内置版本仍须完整支持实例、运动矢量和分簇光照。
# 两条路径共用同一份 GLSL，避免备用版本随渲染管线升级而过时。
include(cmake/EmbedMaterialShaders.cmake)
tinyengine_expand_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/default.vert" TINYENGINE_DEFAULT_VERTEX_SHADER "")
tinyengine_expand_shader("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/default.frag" TINYENGINE_DEFAULT_FRAGMENT_SHADER "")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/tinyengine/render")
tinyengine_embed_material_shaders("${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/material"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/tinyengine/render/builtin_material_shaders.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/default.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/default.frag")
configure_file(src/render/builtin_shaders.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/tinyengine/render/builtin_shaders.h" @ONLY)
target_include_directories(tinyengine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(tinyengine PRIVATE TINYENGINE_HAS_STB_VORBIS=1)
set_property(SOURCE src/build/build_compatibility.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    TINYENGINE_VERSION="${PROJECT_VERSION}"
    TINYENGINE_BUILD_ID="${TINYENGINE_BUILD_ID}"
    TINYENGINE_BUILD_CONFIGURATION="$<CONFIG>"
    TINYENGINE_COMPILER_ID="${TINYENGINE_COMPILER_ID}")
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
    target_link_libraries(tinyengine PUBLIC windowscodecs ole32 gdi32 dbghelp ws2_32)
endif()
if(NOT WIN32)
    target_link_libraries(tinyengine PUBLIC Freetype::Freetype)
    target_compile_definitions(tinyengine PUBLIC TINYENGINE_HAS_FREETYPE=1)
endif()
if(TINYENGINE_ENABLE_JOLT)
    target_link_libraries(tinyengine PUBLIC Jolt)
    target_compile_definitions(tinyengine PUBLIC TINYENGINE_HAS_JOLT=1)
endif()

if(MSVC)
    target_compile_options(tinyengine PRIVATE /W4 /permissive-)
    set_source_files_properties(src/audio/stb_vorbis.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
    target_compile_options(tinyengine PRIVATE -Wall -Wextra -Wpedantic)
    set_source_files_properties(src/audio/stb_vorbis.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

if(TINYENGINE_BUILD_DEMO)
    add_executable(tinyengine_demo src/main.cpp)
    target_link_libraries(tinyengine_demo PRIVATE tinyengine)
endif()

if(TINYENGINE_BUILD_GAME)
    add_executable(tinyengine_game src/game_main.cpp)
    target_link_libraries(tinyengine_game PRIVATE tinyengine)
    target_compile_definitions(tinyengine_game PRIVATE TINYENGINE_SHIPPING=1)
    add_custom_command(TARGET tinyengine_game POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DOUTPUT=$<TARGET_FILE:tinyengine_game>.buildinfo
            -DENGINE_VERSION=${PROJECT_VERSION}
            -DENGINE_ABI=1
            -DSCENE_FORMAT=11
            -DBUILD_ID=${TINYENGINE_BUILD_ID}
            -DCONFIGURATION=$<CONFIG>
            -DCOMPILER=${TINYENGINE_COMPILER_ID}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteBuildInfo.cmake"
        COMMENT "Writing TinyEngine Game compatibility metadata"
        VERBATIM)
endif()

if(TINYENGINE_BUILD_IMPORTERS)
    add_library(tinyengine_importers STATIC src/import/assimp_importer.cpp)
    target_include_directories(tinyengine_importers PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
    target_link_libraries(tinyengine_importers PUBLIC tinyengine assimp::assimp)
    if(MSVC)
        target_compile_options(tinyengine_importers PRIVATE /W4 /permissive-)
    else()
        target_compile_options(tinyengine_importers PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
add_subdirectory(programs)

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
    add_executable(tinyengine_editor editor/editor_main.cpp editor/editor_app.cpp editor/editor_inspector.cpp editor/editor_workspace.cpp editor/editor_assets.cpp editor/editor_asset_references.cpp editor/asset_reference_graph.cpp editor/asset_trash.cpp editor/editor_asset_deletion.cpp editor/editor_material_assignment.cpp editor/editor_scene_view.cpp editor/material_asset_editor.cpp editor/material_graph_editor.cpp editor/animation_asset_editor.cpp)
    if(WIN32)
        target_sources(tinyengine_editor PRIVATE editor/TinyEngine.manifest)
    endif()
    target_include_directories(tinyengine_editor PRIVATE editor)
    target_link_libraries(tinyengine_editor PRIVATE tinyengine tinyengine_imgui)
    target_compile_definitions(tinyengine_editor PRIVATE TINYENGINE_DEVELOPMENT_SDK_ROOT="${CMAKE_CURRENT_BINARY_DIR}/ProjectSDK")
    add_custom_command(TARGET tinyengine_editor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config $<CONFIG>
            --component ProjectSDK --prefix "${CMAKE_CURRENT_BINARY_DIR}/ProjectSDK"
        COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config $<CONFIG>
            --component EditorRuntime --prefix "${CMAKE_CURRENT_BINARY_DIR}/ProjectSDK"
        COMMENT "Preparing the SDK for editor project creation" VERBATIM)
    if(TARGET tinyengine_project_tool)
        add_dependencies(tinyengine_editor tinyengine_project_tool)
        add_custom_command(TARGET tinyengine_editor POST_BUILD
            COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config $<CONFIG>
                --component ProjectTool --prefix "${CMAKE_CURRENT_BINARY_DIR}/ProjectSDK" VERBATIM)
    endif()
    if(TINYENGINE_BUILD_GAME)
        add_dependencies(tinyengine_editor tinyengine_game)
        add_custom_command(TARGET tinyengine_editor POST_BUILD
            COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --config $<CONFIG>
                --component ProjectRuntime --prefix "${CMAKE_CURRENT_BINARY_DIR}/ProjectSDK" VERBATIM)
    endif()
    if(TINYENGINE_BUILD_IMPORTERS)
        target_link_libraries(tinyengine_editor PRIVATE tinyengine_importers)
        target_compile_definitions(tinyengine_editor PRIVATE TINYENGINE_HAS_ASSIMP=1)
    endif()
endif()


if(TINYENGINE_BUILD_GAME)
    add_custom_target(package_game
        COMMAND $<TARGET_FILE:tinyengine_package_tool> "${CMAKE_CURRENT_SOURCE_DIR}/Aster-Engine.tinyproject" "${CMAKE_BINARY_DIR}/Package/Aster-Engine" "$<TARGET_FILE:tinyengine_game>"
        DEPENDS tinyengine_package_tool tinyengine_game
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Packaging standalone Aster-Engine"
        VERBATIM
    )
endif()

if(TINYENGINE_BUILD_TESTS)
    enable_testing()
    if(WIN32)
        find_program(TINYENGINE_POWERSHELL NAMES pwsh powershell REQUIRED)
        add_test(NAME tinyengine_repository_layout COMMAND "${TINYENGINE_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests/repository_layout.ps1")
    endif()
    if(TINYENGINE_BUILD_EDITOR AND WIN32 AND MSVC)
        add_executable(tinyengine_project_tool_discovery_tests tests/project_tool_discovery_test.cpp src/project/solution_generator.cpp)
        target_include_directories(tinyengine_project_tool_discovery_tests PRIVATE include)
        target_compile_definitions(tinyengine_project_tool_discovery_tests PRIVATE TINYENGINE_CMAKE_EXECUTABLE="Z:/missing-builder/cmake.exe")
        target_compile_options(tinyengine_project_tool_discovery_tests PRIVATE /utf-8 /FS)
        add_test(NAME tinyengine_project_tool_discovery COMMAND tinyengine_project_tool_discovery_tests)
        add_executable(tinyengine_project_creation_tests tests/project_creation_test.cpp)
        target_link_libraries(tinyengine_project_creation_tests PRIVATE tinyengine)
        target_compile_options(tinyengine_project_creation_tests PRIVATE /utf-8 /FS)
        add_test(NAME tinyengine_project_creation COMMAND ${CMAKE_COMMAND}
            "-DENGINE_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
            "-DCONFIG=$<CONFIG>"
            "-DCREATE_EXE=$<TARGET_FILE:tinyengine_project_creation_tests>"
            "-DRUNTIME_EXE=$<TARGET_FILE:tinyengine_project_runtime_tests>"
            "-DEDITOR_TEST_EXE=$<TARGET_FILE:tinyengine_editor_interaction_tests>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_creation_test.cmake")
    endif()
    add_executable(tinyengine_project_sdk_abi_tests tests/project_sdk_abi_test.cpp tests/project_sdk_abi_c.c)
    target_include_directories(tinyengine_project_sdk_abi_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(tinyengine_project_sdk_abi_tests PRIVATE tinyengine_project_sdk)
    add_test(NAME tinyengine_project_sdk_abi COMMAND tinyengine_project_sdk_abi_tests)
    add_executable(tinyengine_project_module_tests tests/project_module_test.cpp)
    target_link_libraries(tinyengine_project_module_tests PRIVATE tinyengine tinyengine_project_sdk)
    # Project SDK 玩法扩展（aster.gameplay）的宿主侧契约测试。
    add_executable(tinyengine_project_gameplay_tests tests/project_gameplay_test.cpp)
    target_link_libraries(tinyengine_project_gameplay_tests PRIVATE tinyengine tinyengine_project_sdk)
    if(MSVC)
        target_compile_options(tinyengine_project_gameplay_tests PRIVATE /utf-8 /FS)
    endif()
    add_test(NAME tinyengine_project_gameplay COMMAND tinyengine_project_gameplay_tests)
    add_test(NAME tinyengine_project_manifest COMMAND tinyengine_project_module_tests --group manifest)
    add_test(NAME tinyengine_project_loader COMMAND tinyengine_project_module_tests --group loader)
    add_test(NAME tinyengine_project_bridge COMMAND tinyengine_project_module_tests --group bridge)
    add_test(NAME tinyengine_project_session COMMAND tinyengine_project_module_tests --group session)
    add_executable(tinyengine_project_runtime_tests tests/project_runtime_test.cpp)
    target_link_libraries(tinyengine_project_runtime_tests PRIVATE tinyengine tinyengine_project_sdk)
    if(MSVC)
        target_compile_options(tinyengine_project_runtime_tests PRIVATE /utf-8 /FS)
    endif()
    add_test(NAME tinyengine_project_runtime COMMAND tinyengine_project_runtime_tests --group runtime)
    add_test(NAME tinyengine_project_sdk_consumer COMMAND ${CMAKE_COMMAND}
        "-DENGINE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DENGINE_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
        "-DGENERATOR=${CMAKE_GENERATOR}" "-DMODULE_TEST_EXE=$<TARGET_FILE:tinyengine_project_module_tests>"
        "-DQA_POINTER=${CMAKE_CURRENT_BINARY_DIR}/project-sdk-qa.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_sdk_consumer_test.cmake")
    add_test(NAME tinyengine_project_sdk_runtime_consumer COMMAND ${CMAKE_COMMAND}
        "-DENGINE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DENGINE_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
        "-DGENERATOR=${CMAKE_GENERATOR}" "-DMODULE_TEST_EXE=$<TARGET_FILE:tinyengine_project_module_tests>"
        "-DRUNTIME_TEST_EXE=$<TARGET_FILE:tinyengine_project_runtime_tests>" -DRUNTIME=ON
        "-DRELEASE_MODULE_TEST_EXE=${CMAKE_CURRENT_BINARY_DIR}/Release/tinyengine_project_module_tests.exe"
        "-DQA_POINTER=${CMAKE_CURRENT_BINARY_DIR}/project-sdk-runtime-qa.txt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_sdk_consumer_test.cmake")
    foreach(mode header cache)
        add_test(NAME tinyengine_project_fingerprint_${mode} COMMAND ${CMAKE_COMMAND}
            "-DENGINE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DENGINE_BUILD=${CMAKE_CURRENT_BINARY_DIR}"
            "-DGENERATOR=${CMAKE_GENERATOR}" "-DMODULE_TEST_EXE=$<TARGET_FILE:tinyengine_project_module_tests>"
            "-DREVIEW_VARIANT=${mode}" "-DQA_POINTER=${CMAKE_CURRENT_BINARY_DIR}/project-sdk-${mode}-qa.txt"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_sdk_consumer_test.cmake")
    endforeach()
    foreach(mode git source)
        add_test(NAME tinyengine_project_identity_${mode} COMMAND ${CMAKE_COMMAND}
            "-DENGINE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DGENERATOR=${CMAKE_GENERATOR}" "-DMODE=${mode}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_sdk_identity_test.cmake")
    endforeach()
    add_test(NAME tinyengine_project_host_runtime COMMAND ${CMAKE_COMMAND}
        "-DENGINE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}" "-DGENERATOR=${CMAKE_GENERATOR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/host_runtime_test.cmake")
    add_library(tinyengine_test_runtime_dependency SHARED tests/test_runtime_dependency.cpp)
    add_library(tinyengine_test_dependent_plugin SHARED tests/test_dependent_plugin.cpp)
    target_include_directories(tinyengine_test_dependent_plugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(tinyengine_test_dependent_plugin PRIVATE tinyengine_test_runtime_dependency)
    target_compile_definitions(tinyengine_project_module_tests PRIVATE
        TINY_TEST_DEPENDENT_PLUGIN="$<TARGET_FILE:tinyengine_test_dependent_plugin>")
    add_dependencies(tinyengine_project_module_tests tinyengine_test_dependent_plugin)
    foreach(variant IN ITEMS VALID ABI SHORT OPTIONAL LARGE IDENTITY CALLBACK BEGINFAIL UPDATEFAIL CREATEFAIL)
        if(variant STREQUAL "VALID")
            set(mode 0)
        elseif(variant STREQUAL "ABI")
            set(mode 1)
        elseif(variant STREQUAL "SHORT")
            set(mode 2)
        elseif(variant STREQUAL "OPTIONAL")
            set(mode 3)
        elseif(variant STREQUAL "LARGE")
            set(mode 4)
        elseif(variant STREQUAL "IDENTITY")
            set(mode 5)
        elseif(variant STREQUAL "CALLBACK")
            set(mode 6)
        elseif(variant STREQUAL "BEGINFAIL")
            set(mode 7)
        elseif(variant STREQUAL "UPDATEFAIL")
            set(mode 8)
        else()
            set(mode 9)
        endif()
        add_library(tinyengine_test_project_${variant} SHARED tests/test_project_module.cpp)
        target_link_libraries(tinyengine_test_project_${variant} PRIVATE tinyengine_project_sdk)
        target_compile_definitions(tinyengine_test_project_${variant} PRIVATE
            TINY_TEST_MODE=${mode} TINY_TEST_PROFILE="$<IF:$<CONFIG:Debug>,Development,Shipping>")
        target_compile_definitions(tinyengine_project_module_tests PRIVATE
            TINY_TEST_PROJECT_${variant}="$<TARGET_FILE:tinyengine_test_project_${variant}>")
        add_dependencies(tinyengine_project_module_tests tinyengine_test_project_${variant})
        target_compile_definitions(tinyengine_project_runtime_tests PRIVATE
            TINY_TEST_PROJECT_${variant}="$<TARGET_FILE:tinyengine_test_project_${variant}>")
        add_dependencies(tinyengine_project_runtime_tests tinyengine_test_project_${variant})
    endforeach()
    add_library(tinyengine_test_project_MISSING SHARED tests/test_project_missing_entry.cpp)
    target_compile_definitions(tinyengine_project_module_tests PRIVATE
        TINY_TEST_PROJECT_MISSING="$<TARGET_FILE:tinyengine_test_project_MISSING>"
        TINY_TEST_DYNAMIC_PLUGIN="$<TARGET_FILE:tinyengine_test_dynamic_plugin>")
    add_dependencies(tinyengine_project_module_tests tinyengine_test_project_MISSING tinyengine_test_dynamic_plugin)
    # 只有启用测试目标时才开放测试读取接口，正常构建不会带上这些入口。
    target_compile_definitions(tinyengine PUBLIC TINYENGINE_TEST_HOOKS=1)
    add_executable(tinyengine_material_tests tests/material_system_test.cpp)
    target_link_libraries(tinyengine_material_tests PRIVATE tinyengine)
    add_executable(tinyengine_material_render_tests tests/material_render_test.cpp)
    target_link_libraries(tinyengine_material_render_tests PRIVATE tinyengine)
    add_test(NAME tinyengine_material_render COMMAND tinyengine_material_render_tests)
    set_tests_properties(tinyengine_material_render PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/MaterialNoShaders")
    add_test(NAME tinyengine_material_builtin COMMAND tinyengine_material_render_tests)
    set_tests_properties(tinyengine_material_builtin PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/MaterialNoShaders")
    add_test(NAME tinyengine_material_cpu COMMAND tinyengine_material_tests)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME tinyengine_material_migration
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/material_migration_test.py)
    endif()
    set_tests_properties(tinyengine_material_cpu PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(tinyengine_tests tests/engine_test.cpp)
    target_link_libraries(tinyengine_tests PRIVATE tinyengine)
    add_executable(tinyengine_render_stability_tests tests/render_stability_test.cpp)
    target_link_libraries(tinyengine_render_stability_tests PRIVATE tinyengine)
    add_test(NAME tinyengine_render_stability COMMAND tinyengine_render_stability_tests)
    set_tests_properties(tinyengine_render_stability PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/RenderStabilityNoAssets")
    add_test(NAME tinyengine_render_builtin_shaders COMMAND tinyengine_render_stability_tests)
    set_tests_properties(tinyengine_render_builtin_shaders PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/RenderStabilityNoAssets")
    add_executable(tinyengine_scene_save_tests tests/scene_save_test.cpp)
    target_link_libraries(tinyengine_scene_save_tests PRIVATE tinyengine)
    add_executable(tinyengine_platform_mount_tests tests/platform_mount_test.cpp)
    target_link_libraries(tinyengine_platform_mount_tests PRIVATE tinyengine)
    add_executable(tinyengine_script_lifecycle_tests tests/script_lifecycle_test.cpp)
    target_link_libraries(tinyengine_script_lifecycle_tests PRIVATE tinyengine)
    add_library(tinyengine_test_dynamic_plugin SHARED tests/test_dynamic_plugin.cpp)
    target_include_directories(tinyengine_test_dynamic_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_executable(tinyengine_extended_tests tests/extended_regression_test.cpp)
    target_link_libraries(tinyengine_extended_tests PRIVATE tinyengine)
    target_compile_definitions(tinyengine_extended_tests PRIVATE
        TINYENGINE_TEST_PLUGIN_PATH="$<TARGET_FILE:tinyengine_test_dynamic_plugin>"
        TINYENGINE_TEST_GAME_PATH="$<TARGET_FILE:tinyengine_game>")
    add_dependencies(tinyengine_extended_tests tinyengine_game)
    add_dependencies(tinyengine_extended_tests tinyengine_test_dynamic_plugin)
    if(TINYENGINE_BUILD_IMPORTERS)
        target_link_libraries(tinyengine_tests PRIVATE tinyengine_importers)
        target_compile_definitions(tinyengine_tests PRIVATE TINYENGINE_HAS_ASSIMP=1)
        add_executable(tinyengine_import_compatibility_tests tests/import_compatibility_test.cpp)
        target_link_libraries(tinyengine_import_compatibility_tests PRIVATE tinyengine_importers)
        target_compile_definitions(tinyengine_import_compatibility_tests PRIVATE
            TINYENGINE_ASSIMP_MODEL_DIR="${TINYENGINE_ASSIMP_DIR}/test/models")
        add_test(NAME tinyengine_import_compatibility COMMAND tinyengine_import_compatibility_tests)
        add_test(NAME tinyengine_skeletal_import COMMAND tinyengine_import_compatibility_tests --skeletal-workflow)
        set_tests_properties(tinyengine_skeletal_import PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(tinyengine_import_compatibility PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    add_test(NAME tinyengine_core COMMAND tinyengine_tests)
    set_tests_properties(tinyengine_core PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME tinyengine_scene_save COMMAND tinyengine_scene_save_tests)
    set_tests_properties(tinyengine_scene_save PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME tinyengine_platform_mount COMMAND tinyengine_platform_mount_tests)
    set_tests_properties(tinyengine_platform_mount PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME tinyengine_script_lifecycle COMMAND tinyengine_script_lifecycle_tests)
    set_tests_properties(tinyengine_script_lifecycle PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME tinyengine_extended COMMAND tinyengine_extended_tests)
    set_tests_properties(tinyengine_extended PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    if(TINYENGINE_BUILD_EDITOR)
        add_executable(tinyengine_asset_trash_tests tests/asset_trash_test.cpp editor/asset_trash.cpp)
        target_include_directories(tinyengine_asset_trash_tests PRIVATE editor)
        target_link_libraries(tinyengine_asset_trash_tests PRIVATE tinyengine)
        add_test(NAME tinyengine_asset_trash COMMAND tinyengine_asset_trash_tests)
        set_tests_properties(tinyengine_asset_trash PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        add_executable(tinyengine_material_editor_tests tests/material_editor_test.cpp editor/material_graph_editor.cpp editor/material_asset_editor.cpp)
        target_include_directories(tinyengine_material_editor_tests PRIVATE editor)
        target_link_libraries(tinyengine_material_editor_tests PRIVATE tinyengine tinyengine_imgui)
        add_test(NAME tinyengine_material_editor COMMAND tinyengine_material_editor_tests)
        set_tests_properties(tinyengine_material_editor PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        add_executable(tinyengine_animation_editor_tests tests/animation_editor_test.cpp editor/animation_asset_editor.cpp)
        target_include_directories(tinyengine_animation_editor_tests PRIVATE editor)
        target_link_libraries(tinyengine_animation_editor_tests PRIVATE tinyengine tinyengine_imgui)
        add_test(NAME tinyengine_animation_editor COMMAND tinyengine_animation_editor_tests)
        set_tests_properties(tinyengine_animation_editor PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        add_executable(tinyengine_editor_interaction_tests tests/editor_interaction_test.cpp editor/editor_app.cpp editor/editor_inspector.cpp editor/editor_workspace.cpp editor/editor_assets.cpp editor/editor_asset_references.cpp editor/asset_reference_graph.cpp editor/asset_trash.cpp editor/editor_asset_deletion.cpp editor/editor_material_assignment.cpp editor/editor_scene_view.cpp editor/material_asset_editor.cpp editor/material_graph_editor.cpp editor/animation_asset_editor.cpp)
        target_include_directories(tinyengine_editor_interaction_tests PRIVATE editor)
        target_compile_definitions(tinyengine_editor_interaction_tests PRIVATE
            TINYENGINE_DEVELOPMENT_SDK_ROOT="${CMAKE_CURRENT_BINARY_DIR}/ProjectSDK")
        add_dependencies(tinyengine_editor_interaction_tests tinyengine_editor)
        target_compile_definitions(tinyengine_editor_interaction_tests PRIVATE TINYENGINE_TEST_GAME_PATH="$<TARGET_FILE:tinyengine_game>")
        add_dependencies(tinyengine_editor_interaction_tests tinyengine_game)
        target_link_libraries(tinyengine_editor_interaction_tests PRIVATE tinyengine tinyengine_imgui)
        if(TINYENGINE_BUILD_IMPORTERS)
            target_link_libraries(tinyengine_editor_interaction_tests PRIVATE tinyengine_importers)
            target_compile_definitions(tinyengine_editor_interaction_tests PRIVATE TINYENGINE_HAS_ASSIMP=1)
        endif()
        add_test(NAME tinyengine_editor_interaction COMMAND tinyengine_editor_interaction_tests)
        add_test(NAME tinyengine_editor_project_creation COMMAND tinyengine_editor_interaction_tests --project-create)
        set_tests_properties(tinyengine_editor_project_creation PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} TIMEOUT 180)
        set_tests_properties(tinyengine_editor_interaction PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        if(WIN32)
            # Process DPI awareness is fixed once per process; exercise startup in a fresh test process.
            add_test(NAME tinyengine_editor_dpi COMMAND tinyengine_editor_interaction_tests --dpi)
            set_tests_properties(tinyengine_editor_dpi PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        endif()
        add_executable(tinyengine_editor_asset_rename_tests tests/editor_asset_rename_test.cpp editor/editor_app.cpp editor/editor_inspector.cpp editor/editor_workspace.cpp editor/editor_assets.cpp editor/editor_asset_references.cpp editor/asset_reference_graph.cpp editor/asset_trash.cpp editor/editor_asset_deletion.cpp editor/editor_material_assignment.cpp editor/editor_scene_view.cpp editor/material_asset_editor.cpp editor/material_graph_editor.cpp editor/animation_asset_editor.cpp)
        target_include_directories(tinyengine_editor_asset_rename_tests PRIVATE editor)
        target_compile_definitions(tinyengine_editor_asset_rename_tests PRIVATE TINYENGINE_TEST_GAME_PATH="$<TARGET_FILE:tinyengine_game>")
        add_dependencies(tinyengine_editor_asset_rename_tests tinyengine_game)
        target_link_libraries(tinyengine_editor_asset_rename_tests PRIVATE tinyengine tinyengine_imgui)
        if(TINYENGINE_BUILD_IMPORTERS)
            target_link_libraries(tinyengine_editor_asset_rename_tests PRIVATE tinyengine_importers)
            target_compile_definitions(tinyengine_editor_asset_rename_tests PRIVATE TINYENGINE_HAS_ASSIMP=1)
        endif()
        add_test(NAME tinyengine_editor_asset_rename COMMAND tinyengine_editor_asset_rename_tests)
        set_tests_properties(tinyengine_editor_asset_rename PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()

# 只为 Aster-Engine 自有源码启用 UTF-8；不能把该选项全局传给 Assimp、zlib、SDL 或 ImGui。
if(MSVC)
    foreach(TINYENGINE_SOURCE_TARGET IN ITEMS
            tinyengine_project_sdk_abi_tests tinyengine_project_module_tests
            tinyengine tinyengine_demo tinyengine_game tinyengine_importers tinyengine_editor
            tinyengine_asset_tool tinyengine_scene_tool tinyengine_package_tool tinyengine_commandlet
            tinyengine_tests tinyengine_material_tests tinyengine_material_render_tests tinyengine_render_stability_tests tinyengine_scene_save_tests tinyengine_test_dynamic_plugin tinyengine_extended_tests
            tinyengine_import_compatibility_tests tinyengine_editor_interaction_tests tinyengine_editor_asset_rename_tests
            tinyengine_animation_editor_tests tinyengine_platform_mount_tests tinyengine_script_lifecycle_tests tinyengine_material_editor_tests tinyengine_asset_trash_tests)
        if(TARGET ${TINYENGINE_SOURCE_TARGET})
            # /FS 在 CMake/MSBuild 并行编译时串行化各目标的 PDB 写入，避免编译器争用。
            target_compile_options(${TINYENGINE_SOURCE_TARGET} PRIVATE /utf-8 /FS)
        endif()
    endforeach()
endif()

include(GNUInstallDirs)
include(cmake/InstallHostRuntime.cmake)
install(TARGETS tinyengine ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(TINYENGINE_BUILD_DEMO)
    install(TARGETS tinyengine_demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(TINYENGINE_BUILD_GAME)
    install(TARGETS tinyengine_game RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    tinyengine_install_host_runtime(tinyengine_game ProjectRuntime)
endif()
if(TINYENGINE_BUILD_EDITOR)
    install(TARGETS tinyengine_editor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    tinyengine_install_host_runtime(tinyengine_editor EditorRuntime)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/editor/$<CONFIG>/Aster-Engine.cmd"
        CONTENT "@echo off\r\nstart \"\" \"%~dp0bin\\$<CONFIG>\\tinyengine_editor.exe\" %*\r\n")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/editor/$<CONFIG>/Aster-Engine.cmd"
        DESTINATION . COMPONENT EditorRuntime)
endif()
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT ProjectRuntime)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}/$<CONFIG>")
    include(InstallRequiredSystemLibraries)
endif()
install(TARGETS tinyengine_asset_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS tinyengine_scene_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS tinyengine_package_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS tinyengine_commandlet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} PATTERN "import" EXCLUDE)
install(DIRECTORY assets/ DESTINATION ${CMAKE_INSTALL_DATADIR}/tinyengine/assets)
install(FILES Aster-Engine.tinyproject DESTINATION .)

set(CPACK_PACKAGE_NAME "Aster-Engine")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
