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
file(MAKE_DIRECTORY "${_tiny_sdk_include}/MiniEngine/ProjectSDK" "${_tiny_sdk_cmake}")

add_library(tinyengine_project_sdk STATIC src/ProjectSDK/SDKInfo.cpp src/ProjectSDK/Project.cpp)
add_library(TinyEngine::ProjectSDK ALIAS tinyengine_project_sdk)
set_target_properties(tinyengine_project_sdk PROPERTIES EXPORT_NAME ProjectSDK)
target_compile_features(tinyengine_project_sdk PUBLIC cxx_std_17)
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
    set(_tiny_header "${_tiny_sdk_include}/MiniEngine/ProjectSDK/BuildInfo${TINYENGINE_SDK_CONFIGURATION}.h")
    set(_tiny_info "${_tiny_sdk_cmake}/TinyEngineSDKInfo-${TINYENGINE_SDK_CONFIGURATION}.cmake")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectSDKBuildInfo.h.in" "${_tiny_header}" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectSDKBuildInfo.cmake.in" "${_tiny_info}" @ONLY)
    install(FILES "${_tiny_header}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/MiniEngine/ProjectSDK"
        CONFIGURATIONS ${TINYENGINE_SDK_CONFIGURATION} COMPONENT ProjectSDK)
    install(FILES "${_tiny_info}" DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine"
        CONFIGURATIONS ${TINYENGINE_SDK_CONFIGURATION} COMPONENT ProjectSDK)
endforeach()

install(TARGETS tinyengine_project_sdk EXPORT TinyEngineProjectSDKTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/$<CONFIG>" COMPONENT ProjectSDK)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/MiniEngine/ProjectSDK/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/MiniEngine/ProjectSDK" COMPONENT ProjectSDK)
install(EXPORT TinyEngineProjectSDKTargets FILE TinyEngineTargets.cmake NAMESPACE TinyEngine::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine" COMPONENT ProjectSDK)
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TinyEngineConfig.cmake.in"
    "${_tiny_sdk_cmake}/TinyEngineConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine")
write_basic_package_version_file("${_tiny_sdk_cmake}/TinyEngineConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY ExactVersion)
install(FILES "${_tiny_sdk_cmake}/TinyEngineConfig.cmake" "${_tiny_sdk_cmake}/TinyEngineConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine" COMPONENT ProjectSDK)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TinyEngineProject.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ProjectInputFingerprint.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteProjectModuleManifest.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/TinyEngine" COMPONENT ProjectSDK)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/tests/ProjectSDKConsumer/CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ProjectSDKConsumer/Game.tinyproject"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/ProjectSDKConsumer/Start.scene"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/tinyengine/templates/ProjectModule" COMPONENT ProjectSDK)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests/ProjectSDKConsumer/Source/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/tinyengine/templates/ProjectModule/Source" COMPONENT ProjectSDK)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/default.input"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/tinyengine/templates/ProjectModule/assets" COMPONENT ProjectSDK)
