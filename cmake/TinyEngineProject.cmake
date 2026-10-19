include_guard(GLOBAL)

function(tinyengine_add_project_module)
    cmake_parse_arguments(P "" "TARGET;PROJECT_ID;MODULE_ID" "SOURCES;RUNTIME_DEPENDENCIES" ${ARGN})
    if(P_UNPARSED_ARGUMENTS OR NOT P_TARGET OR NOT P_PROJECT_ID OR NOT P_MODULE_ID OR NOT P_SOURCES)
        message(FATAL_ERROR "tinyengine_add_project_module requires TARGET, PROJECT_ID, MODULE_ID and SOURCES")
    endif()
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "ProjectSDK P0 requires Windows x64 with Visual Studio 2022")
    endif()
    foreach(id IN ITEMS "${P_TARGET}" "${P_PROJECT_ID}" "${P_MODULE_ID}")
        string(LENGTH "${id}" length)
        if(length GREATER 256 OR NOT id MATCHES "^[A-Za-z0-9_.-]+$" OR id STREQUAL "." OR id STREQUAL "..")
            message(FATAL_ERROR "Invalid project/module/target ID: ${id}")
        endif()
    endforeach()
    if(NOT TinyEngine_SDK_COMPILER STREQUAL "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
        message(FATAL_ERROR "Project compiler does not match installed SDK compiler ${TinyEngine_SDK_COMPILER}")
    endif()
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE MATCHES "^(Debug|Release)$")
        message(FATAL_ERROR "ProjectSDK P0 supports Debug (Development) and Release (Shipping)")
    endif()
    # Explicit source/header inputs plus project configuration and runtime dependency bytes.
    # Generated files and source tree absolute locations do not enter the fingerprint.
    file(GLOB configs CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/*.tinyproject" "${PROJECT_SOURCE_DIR}/*.cmake")
    set(inputs ${P_SOURCES} ${P_RUNTIME_DEPENDENCIES} ${configs} "${PROJECT_SOURCE_DIR}/CMakeLists.txt")
    set(paths)
    foreach(input IN LISTS inputs)
        get_filename_component(path "${input}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        file(REAL_PATH "${path}" path)
        file(RELATIVE_PATH relative "${PROJECT_SOURCE_DIR}" "${path}")
        if(relative MATCHES "^\\.\\.(/|$)" OR IS_ABSOLUTE "${relative}" OR NOT EXISTS "${path}")
            message(FATAL_ERROR "Module input must be an existing file inside the project: ${input}")
        endif()
        list(APPEND paths "${relative}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    endforeach()
    list(REMOVE_DUPLICATES paths)
    list(SORT paths)
    set(content "${CMAKE_CXX_FLAGS}|${CMAKE_CXX_FLAGS_DEBUG}|${CMAKE_CXX_FLAGS_RELEASE}|${CMAKE_MSVC_RUNTIME_LIBRARY}\n")
    foreach(path IN LISTS paths)
        file(SHA256 "${PROJECT_SOURCE_DIR}/${path}" hash)
        string(APPEND content "${path}:${hash}\n")
    endforeach()
    string(SHA256 fingerprint "${content}")
    set(dependencies)
    foreach(input IN LISTS P_RUNTIME_DEPENDENCIES)
        get_filename_component(path "${input}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND dependencies "${path}")
    endforeach()
    add_library(${P_TARGET} SHARED ${P_SOURCES})
    target_link_libraries(${P_TARGET} PRIVATE TinyEngine::ProjectSDK)
    target_compile_features(${P_TARGET} PRIVATE cxx_std_17)
    target_compile_options(${P_TARGET} PRIVATE /utf-8 /FS)
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/TinyEngineProject/${P_TARGET}")
    target_include_directories(${P_TARGET} PRIVATE "${generated}/$<CONFIG>")
    set_target_properties(${P_TARGET} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${generated}/link/$<CONFIG>"
        PDB_OUTPUT_DIRECTORY "${generated}/link/$<CONFIG>")
    set(profile "$<IF:$<CONFIG:Debug>,Development,Shipping>")
    foreach(config Debug Release)
        if(config STREQUAL "Debug")
            set(p Development)
        else()
            set(p Shipping)
        endif()
        set(sdk_${config} "${TinyEngine_SDK_BUILD_ID_${config}}")
        string(SHA256 build_${config} "${sdk_${config}}|${p}|${fingerprint}")
    endforeach()
    set(sdk "$<IF:$<CONFIG:Debug>,${sdk_Debug},${sdk_Release}>")
    set(build "$<IF:$<CONFIG:Debug>,${build_Debug},${build_Release}>")
    file(GENERATE OUTPUT "${generated}/$<CONFIG>/ProjectModuleBuildInfo.h" CONTENT
"#pragma once
#include <MiniEngine/ProjectSDK/ProjectAPI.h>
#if !defined(TINYENGINE_PROJECT_SDK_DEBUG) && !defined(TINYENGINE_PROJECT_SDK_RELEASE)
#error ProjectSDK P0 supports only Debug and Release
#endif
namespace TinyEngineProject {
inline const TinyProjectModuleInfo &GetModuleInfo() noexcept {
    static const TinyProjectModuleInfo info{sizeof(TinyProjectModuleInfo),
        {\"${P_MODULE_ID}\",sizeof(\"${P_MODULE_ID}\")-1}, {\"${P_PROJECT_ID}\",sizeof(\"${P_PROJECT_ID}\")-1},
        {\"${sdk}\",sizeof(\"${sdk}\")-1}, {\"${build}\",sizeof(\"${build}\")-1},
        {\"${fingerprint}\",sizeof(\"${fingerprint}\")-1}, {\"${profile}\",sizeof(\"${profile}\")-1}};
    return info;
}
}
")
    file(GENERATE OUTPUT "${generated}/$<CONFIG>/Publish.cmake" CONTENT
"set(root [==[${PROJECT_SOURCE_DIR}]==])
set(module [==[${P_MODULE_ID}]==])
set(project [==[${P_PROJECT_ID}]==])
set(profile [==[${profile}]==])
set(sdk [==[${sdk}]==])
set(build [==[${build}]==])
set(fingerprint [==[${fingerprint}]==])
set(library [==[$<TARGET_FILE:${P_TARGET}>]==])
set(pdb [==[$<TARGET_PDB_FILE:${P_TARGET}>]==])
set(dependencies [==[${dependencies}]==])
include([==[${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WriteProjectModuleManifest.cmake]==])
")
    add_custom_command(TARGET ${P_TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -P "${generated}/$<CONFIG>/Publish.cmake"
        COMMENT "Publishing ${P_MODULE_ID} ${profile} only after a successful link" VERBATIM)
endfunction()
