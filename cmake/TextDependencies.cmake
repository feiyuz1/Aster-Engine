include_guard(GLOBAL)

# 字体依赖只暴露 CMake target；第三方头文件不进入引擎公开接口。
# 默认从固定归档构建 FT/HB，ICU 使用固定版本包；离线可替换归档或源码。
option(TINYENGINE_TEXT_USE_SYSTEM_LIBS "Use installed FreeType/HarfBuzz packages instead of pinned sources" OFF)
option(TINYENGINE_ICU_STATIC "Use a separately installed static ICU package" OFF)
set(TINYENGINE_ICU_VERSION "76.1" CACHE STRING "Required exact ICU package version")
set(TINYENGINE_FREETYPE_ARCHIVE "https://codeload.github.com/freetype/freetype/zip/refs/tags/VER-2-13-3" CACHE STRING "FreeType 2.13.3 archive URL or local file")
set(TINYENGINE_HARFBUZZ_ARCHIVE "https://codeload.github.com/harfbuzz/harfbuzz/zip/refs/tags/10.2.0" CACHE STRING "HarfBuzz 10.2.0 archive URL or local file")
set(TINYENGINE_ICU_ARCHIVE "https://github.com/unicode-org/icu/releases/download/release-76-1/icu4c-76_1-Win64-MSVC2022.zip" CACHE STRING "ICU 76.1 Windows x64 archive URL or local file")
set(TINYENGINE_TEXT_RUNTIME_FILES "" CACHE STRING "Extra shared text dependency files to stage/install (semicolon separated)")

if(NOT DEFINED FETCHCONTENT_BASE_DIR)
    get_filename_component(_tinyengine_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(FETCHCONTENT_BASE_DIR "${_tinyengine_root}/intermediate/Dependencies" CACHE PATH "Dependency download directory")
endif()
include(FetchContent)

function(tinyengine_add_pinned_text_sources)
    # 函数作用域避免 BUILD_SHARED_LIBS 改变其他引擎依赖的构建方式。
    set(BUILD_SHARED_LIBS OFF)
    foreach(dependency ZLIB BZIP2 PNG HARFBUZZ BROTLI)
        set(FT_DISABLE_${dependency} ON CACHE BOOL "" FORCE)
    endforeach()
    set(SKIP_INSTALL_ALL ON)
    FetchContent_Declare(tinyengine_freetype
        URL "${TINYENGINE_FREETYPE_ARCHIVE}"
        URL_HASH SHA256=a7165cb405dfce349ae64d6f59efb2567565ca8edbacbeff2a8d0e4cfa779cf5
        BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/tinyengine_freetype-build"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(tinyengine_freetype)
    if(NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()

    set(HB_HAVE_FREETYPE ON CACHE BOOL "" FORCE)
    set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
    set(HB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(tinyengine_harfbuzz
        URL "${TINYENGINE_HARFBUZZ_ARCHIVE}"
        URL_HASH SHA256=180cec309e817ec50953cd4d4208025c1931203231cf455418b0dd4021091951
        BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/tinyengine_harfbuzz-build"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(tinyengine_harfbuzz)
    if(NOT TARGET harfbuzz::harfbuzz)
        add_library(harfbuzz::harfbuzz ALIAS harfbuzz)
    endif()
    set_property(TARGET freetype harfbuzz PROPERTY POSITION_INDEPENDENT_CODE ON)
    set(TINYENGINE_FREETYPE_LICENSE_DIR "${tinyengine_freetype_SOURCE_DIR}/docs" PARENT_SCOPE)
    set(TINYENGINE_HARFBUZZ_LICENSE "${tinyengine_harfbuzz_SOURCE_DIR}/COPYING" PARENT_SCOPE)
endfunction()

if(TINYENGINE_TEXT_USE_SYSTEM_LIBS)
    # Config 包保留 SHARED/IMPORTED_LOCATION 信息，才能完整收集 FT 及其 DLL 依赖。
    find_package(freetype 2.13.3 CONFIG QUIET)
    if(TARGET freetype AND NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    if(NOT TARGET Freetype::Freetype)
        find_package(Freetype 2.13.3 REQUIRED)
    endif()
    find_package(harfbuzz 10.2.0 CONFIG REQUIRED)
else()
    tinyengine_add_pinned_text_sources()
endif()

# ICU 上游尚不提供通用 CMake 构建。Windows x64/MSVC 使用官方二进制；
# 其他平台/架构通过 ICU_ROOT 或 CMAKE_PREFIX_PATH 提供相同版本开发包。
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT TINYENGINE_ICU_STATIC AND NOT ICU_ROOT AND NOT DEFINED ENV{ICU_ROOT})
    if(NOT CMAKE_GENERATOR_PLATFORM MATCHES "ARM64" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        if(NOT TINYENGINE_ICU_VERSION STREQUAL "76.1")
            message(FATAL_ERROR "A custom ICU version requires an installed package: set ICU_ROOT")
        endif()
        FetchContent_Declare(tinyengine_icu
            URL "${TINYENGINE_ICU_ARCHIVE}"
            URL_HASH SHA256=bedba77dd1feca09e9ae9922109a285c0ecf46d09c80b65eae6eae63a4e155dc
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(tinyengine_icu)
        set(ICU_ROOT "${tinyengine_icu_SOURCE_DIR}")
    endif()
endif()
find_package(ICU ${TINYENGINE_ICU_VERSION} EXACT REQUIRED COMPONENTS uc i18n data)
if(TINYENGINE_ICU_STATIC)
    foreach(_icu_target ICU::uc ICU::i18n ICU::data)
        set_property(TARGET ${_icu_target} APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS U_STATIC_IMPLEMENTATION)
    endforeach()
endif()

# FindICU 的 UNKNOWN IMPORTED target 不会出现在 TARGET_RUNTIME_DLLS 中。
# 对 Windows ICU 显式收集 DLL，确保构建树与安装目录都能独立运行。
if(WIN32 AND NOT TINYENGINE_ICU_STATIC)
    string(REGEX MATCH "^[0-9]+" _tinyengine_icu_major "${ICU_VERSION}")
    foreach(component uc i18n data)
        if(component STREQUAL "i18n")
            set(_icu_basename icuin)
        elseif(component STREQUAL "data")
            set(_icu_basename icudt)
        else()
            set(_icu_basename icuuc)
        endif()
        unset(_tinyengine_icu_${component}_dll CACHE)
        find_file(_tinyengine_icu_${component}_dll
            NAMES "${_icu_basename}${_tinyengine_icu_major}.dll"
            HINTS "${ICU_ROOT}" "$ENV{ICU_ROOT}" "${ICU_INCLUDE_DIR}/.."
            PATH_SUFFIXES bin64 bin NO_DEFAULT_PATH)
        if(_tinyengine_icu_${component}_dll)
            list(APPEND TINYENGINE_TEXT_RUNTIME_FILES "${_tinyengine_icu_${component}_dll}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES TINYENGINE_TEXT_RUNTIME_FILES)
endif()

# 将采用的上游许可一并暂存，让项目发布器能复制静态库和 DLL 的许可。
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/text-licenses")
foreach(_dependency freetype harfbuzz icu)
    if(_dependency STREQUAL "freetype")
        set(_license "${TINYENGINE_FREETYPE_LICENSE_DIR}/FTL.TXT")
    elseif(_dependency STREQUAL "harfbuzz")
        set(_license "${TINYENGINE_HARFBUZZ_LICENSE}")
    else()
        set(_license "${ICU_ROOT}/LICENSE")
    endif()
    if(EXISTS "${_license}" AND NOT IS_DIRECTORY "${_license}")
        set(_staged_license "${CMAKE_CURRENT_BINARY_DIR}/generated/text-licenses/${_dependency}-LICENSE.txt")
        configure_file("${_license}" "${_staged_license}" COPYONLY)
        list(APPEND TINYENGINE_TEXT_RUNTIME_FILES "${_staged_license}")
    endif()
endforeach()

# 发布器只记录文件名，从被验证的 Game EXE 旁复制，不能嵌入开发机路径。
list(APPEND TINYENGINE_TEXT_RUNTIME_FILES ${TINYENGINE_ADDITIONAL_RUNTIME_FILES})
set(_tinyengine_runtime_names "")
foreach(_runtime IN LISTS TINYENGINE_TEXT_RUNTIME_FILES)
    get_filename_component(_name "${_runtime}" NAME)
    list(APPEND _tinyengine_runtime_names "${_name}")
endforeach()
list(REMOVE_DUPLICATES _tinyengine_runtime_names)
list(LENGTH _tinyengine_runtime_names _tinyengine_runtime_count)
set(_tinyengine_runtime_entries "")
foreach(_name IN LISTS _tinyengine_runtime_names)
    string(APPEND _tinyengine_runtime_entries "    \"${_name}\",\n")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/tinyengine/text")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/tinyengine/text/runtime_dependencies.h"
    "#pragma once\n#include <array>\n#include <string_view>\nnamespace tinyengine::text_runtime {\ninline constexpr std::string_view manifestName = \".text-runtime.txt\";\ninline constexpr std::string_view manifestVersion = \"tinyengine-text-runtime-v1\";\ninline constexpr std::array<std::string_view, ${_tinyengine_runtime_count}> fileNames{\n${_tinyengine_runtime_entries}};\n}\n")

# 主目标定义后调用，覆盖工具、测试、编辑器和游戏的实际输出目录。
function(tinyengine_stage_text_runtime_targets)
        get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_target IN LISTS _targets)
            get_target_property(_type ${_target} TYPE)
            if(_type STREQUAL "EXECUTABLE")
                set(_script "${CMAKE_CURRENT_BINARY_DIR}/text-runtime/$<CONFIG>/${_target}.cmake")
                set(_script_content [=[
cmake_minimum_required(VERSION 3.21)
set(_files [==[@TINYENGINE_TEXT_RUNTIME_FILES@;$<TARGET_RUNTIME_DLLS:@_target@>]==])
list(FILTER _files EXCLUDE REGEX "^$")
list(REMOVE_DUPLICATES _files)
set(_names "")
foreach(_file IN LISTS _files)
    get_filename_component(_name "${_file}" NAME)
    list(APPEND _names "${_name}")
endforeach()
list(REMOVE_DUPLICATES _names)
set(_manifest "tinyengine-text-runtime-v1\n")
foreach(_name IN LISTS _names)
    string(APPEND _manifest "${_name}\n")
endforeach()
if(_files)
    file(COPY ${_files} DESTINATION [==[$<TARGET_FILE_DIR:@_target@>]==])
endif()
file(WRITE [==[$<TARGET_FILE:@_target@>.text-runtime.txt]==] "${_manifest}")
]=])
                string(CONFIGURE "${_script_content}" _script_content @ONLY)
                file(GENERATE OUTPUT "${_script}" CONTENT "${_script_content}")
                # 编辑器 POST_BUILD 会立即安装自身，清单必须在链接前准备就绪。
                add_custom_command(TARGET ${_target} PRE_LINK
                    COMMAND ${CMAKE_COMMAND} -P "${_script}" VERBATIM)
                install(FILES "$<TARGET_FILE:${_target}>.text-runtime.txt" DESTINATION bin OPTIONAL)
                if(_target STREQUAL "tinyengine_game")
                    install(FILES "$<TARGET_FILE:${_target}>.text-runtime.txt"
                        DESTINATION "bin/$<CONFIG>" COMPONENT ProjectRuntime)
                elseif(_target STREQUAL "tinyengine_editor")
                    install(FILES "$<TARGET_FILE:${_target}>.text-runtime.txt"
                        DESTINATION "bin/$<CONFIG>" COMPONENT EditorRuntime)
                endif()
            endif()
        endforeach()
endfunction()

function(tinyengine_stage_text_runtime)
    tinyengine_stage_text_runtime_targets()
    if(TINYENGINE_TEXT_RUNTIME_FILES)
        foreach(_component ProjectRuntime EditorRuntime)
            install(FILES ${TINYENGINE_TEXT_RUNTIME_FILES}
                DESTINATION "bin/$<CONFIG>" COMPONENT ${_component})
        endforeach()
        install(FILES ${TINYENGINE_TEXT_RUNTIME_FILES} DESTINATION bin)
    endif()
    foreach(_component ProjectRuntime EditorRuntime)
        if(TINYENGINE_FREETYPE_LICENSE_DIR)
            install(FILES "${TINYENGINE_FREETYPE_LICENSE_DIR}/FTL.TXT"
                DESTINATION share/tinyengine/licenses/freetype COMPONENT ${_component})
        endif()
        if(TINYENGINE_HARFBUZZ_LICENSE)
            install(FILES "${TINYENGINE_HARFBUZZ_LICENSE}"
                DESTINATION share/tinyengine/licenses/harfbuzz COMPONENT ${_component})
        endif()
        if(EXISTS "${ICU_ROOT}/LICENSE")
            install(FILES "${ICU_ROOT}/LICENSE"
                DESTINATION share/tinyengine/licenses/icu COMPONENT ${_component})
        endif()
    endforeach()
endfunction()
