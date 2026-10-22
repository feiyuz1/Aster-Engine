# A project module links only this library, never the full engine or SDL/ImGui.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include("${CMAKE_CURRENT_LIST_DIR}/ProjectSDKIdentity.cmake")

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(TINYENGINE_SDK_PLATFORM "Windows-x64")
else()
    set(TINYENGINE_SDK_PLATFORM "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(_tiny_sdk_include "${CMAKE_CURRENT_BINARY_DIR}/generated/sdk/include")
set(_tiny_sdk_cmake "${CMAKE_CURRENT_BINARY_DIR}/generated/sdk/cmake")
file(MAKE_DIRECTORY "${_tiny_sdk_include}/tinyengine/project_sdk" "${_tiny_sdk_cmake}")

add_library(tinyengine_project_sdk STATIC src/project_sdk/sdk_info.cpp src/project_sdk/project.cpp)
add_library(tinyengine::project_sdk ALIAS tinyengine_project_sdk)
set_target_properties(tinyengine_project_sdk PROPERTIES EXPORT_NAME project_sdk)
target_compile_features(tinyengine_project_sdk PUBLIC cxx_std_20)
target_include_directories(tinyengine_project_sdk PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${_tiny_sdk_include}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
if(MSVC)
    target_compile_options(tinyengine_project_sdk PRIVATE /utf-8 /FS)
endif()

# Other standard configurations remain buildable for existing engine consumers.
# The project-module helper restricts the initial supported profiles separately.
foreach(TINYENGINE_SDK_CONFIGURATION Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${TINYENGINE_SDK_CONFIGURATION}" _tiny_config_upper)
    target_compile_definitions(tinyengine_project_sdk PUBLIC
        "$<$<CONFIG:${TINYENGINE_SDK_CONFIGURATION}>:TINYENGINE_PROJECT_SDK_${_tiny_config_upper}=1>")
    string(SHA256 TINYENGINE_SDK_ID
        "${PROJECT_VERSION}|${TINYENGINE_GIT_REVISION}|${TINYENGINE_SDK_INPUT_ID}|1|${TINYENGINE_SDK_PLATFORM}|${TINYENGINE_COMPILER_ID}|${TINYENGINE_SDK_CONFIGURATION}")
    if(TINYENGINE_SDK_CONFIGURATION STREQUAL "RelWithDebInfo")
        set(_tiny_config_suffix "rel_with_deb_info")
    elseif(TINYENGINE_SDK_CONFIGURATION STREQUAL "MinSizeRel")
        set(_tiny_config_suffix "min_size_rel")
    else()
        string(TOLOWER "${TINYENGINE_SDK_CONFIGURATION}" _tiny_config_suffix)
    endif()
    set(_tiny_header "${_tiny_sdk_include}/tinyengine/project_sdk/build_info_${_tiny_config_suffix}.h")
    set(_tiny_info "${_tiny_sdk_cmake}/TinyEngineSDKInfo-${TINYENGINE_SDK_CONFIGURATION}.cmake")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectSDKBuildInfo.h.in" "${_tiny_header}" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectSDKBuildInfo.cmake.in" "${_tiny_info}" @ONLY)
    install(FILES "${_tiny_header}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tinyengine/project_sdk"
        CONFIGURATIONS ${TINYENGINE_SDK_CONFIGURATION} COMPONENT ProjectSDK)
    install(FILES "${_tiny_info}" DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine"
        CONFIGURATIONS ${TINYENGINE_SDK_CONFIGURATION} COMPONENT ProjectSDK)
endforeach()

install(TARGETS tinyengine_project_sdk EXPORT TinyEngineProjectSDKTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>" COMPONENT ProjectSDK)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/tinyengine/project_sdk/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tinyengine/project_sdk" COMPONENT ProjectSDK)
install(EXPORT TinyEngineProjectSDKTargets FILE TinyEngineTargets.cmake NAMESPACE tinyengine::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine" COMPONENT ProjectSDK)
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TinyEngineConfig.cmake.in"
    "${_tiny_sdk_cmake}/TinyEngineConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine")
write_basic_package_version_file("${_tiny_sdk_cmake}/TinyEngineConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES "${_tiny_sdk_cmake}/TinyEngineConfig.cmake" "${_tiny_sdk_cmake}/TinyEngineConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine" COMPONENT ProjectSDK)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TinyEngineProject.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckProjectHost.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectInputFingerprint.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteProjectModuleManifest.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine" COMPONENT ProjectSDK)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_sdk_consumer/CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_sdk_consumer/Game.tinyproject"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_sdk_consumer/Start.scene"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/tinyengine/templates/ProjectModule" COMPONENT ProjectSDK)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_sdk_consumer/source/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/tinyengine/templates/ProjectModule/source" COMPONENT ProjectSDK)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/default.input"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/player.mesh"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/default.mat"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/default.ppm"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/tinyengine/templates/ProjectModule/assets" COMPONENT ProjectSDK)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/tinyengine/templates/ProjectModule/assets/shaders"
    COMPONENT ProjectSDK PATTERN "*.meta" EXCLUDE)
