// 文件：src/Build/BuildCompatibility.cpp
// 作用：读写并校验运行时构建兼容标识。

#include "MiniEngine/Build/BuildCompatibility.hpp"

#include "MiniEngine/Core/Config.hpp"

#include <algorithm>

namespace MiniEngine
{
#ifndef TINYENGINE_VERSION
#define TINYENGINE_VERSION "unknown"
#endif
#ifndef TINYENGINE_BUILD_ID
#define TINYENGINE_BUILD_ID "unknown"
#endif
#ifndef TINYENGINE_BUILD_CONFIGURATION
#define TINYENGINE_BUILD_CONFIGURATION "unknown"
#endif
#ifndef TINYENGINE_COMPILER_ID
#define TINYENGINE_COMPILER_ID "unknown"
#endif

RuntimeBuildInfo BuildCompatibility::Current()
{
    return {TINYENGINE_VERSION, EngineAbiVersion, SceneFormatVersion, TINYENGINE_BUILD_ID, TINYENGINE_BUILD_CONFIGURATION,
            TINYENGINE_COMPILER_ID};
}

std::filesystem::path BuildCompatibility::SidecarPath(const std::filesystem::path &executable)
{
    return executable.string() + ".buildinfo";
}

bool BuildCompatibility::Load(const std::filesystem::path &path, RuntimeBuildInfo &info)
{
    Config config;
    if (!config.Load(path.string()))
        return false;
    info.engineVersion = config.GetString("engine.version");
    info.engineAbi = static_cast<std::uint32_t>(std::max(0, config.GetInt("engine.abi", 0)));
    info.sceneFormat = static_cast<std::uint32_t>(std::max(0, config.GetInt("scene.format", 0)));
    info.buildId = config.GetString("build.id");
    info.configuration = config.GetString("build.configuration");
    info.compiler = config.GetString("build.compiler");
    return !info.engineVersion.empty() && info.engineAbi != 0 && info.sceneFormat != 0 && !info.buildId.empty() &&
           !info.configuration.empty() && !info.compiler.empty();
}

bool BuildCompatibility::Save(const std::filesystem::path &path, const RuntimeBuildInfo &info)
{
    Config config;
    config.Set("engine.version", info.engineVersion);
    config.Set("engine.abi", std::to_string(info.engineAbi));
    config.Set("scene.format", std::to_string(info.sceneFormat));
    config.Set("build.id", info.buildId);
    config.Set("build.configuration", info.configuration);
    config.Set("build.compiler", info.compiler);
    return config.Save(path.string());
}

bool BuildCompatibility::IsCompatible(const RuntimeBuildInfo &candidate, std::string *reason)
{
    const RuntimeBuildInfo current = Current();
    const auto reject = [reason](const std::string &message) {
        if (reason)
            *reason = message;
        return false;
    };
    if (candidate.engineAbi != current.engineAbi)
        return reject("engine ABI mismatch (expected " + std::to_string(current.engineAbi) + ", got " +
                      std::to_string(candidate.engineAbi) + ")");
    if (candidate.sceneFormat != current.sceneFormat)
        return reject("scene format mismatch (expected " + std::to_string(current.sceneFormat) + ", got " +
                      std::to_string(candidate.sceneFormat) + ")");
    if (candidate.engineVersion != current.engineVersion)
        return reject("engine version mismatch (expected " + current.engineVersion + ", got " + candidate.engineVersion + ")");
    if (candidate.buildId != current.buildId)
        return reject("build ID mismatch (expected " + current.buildId + ", got " + candidate.buildId + ")");
    if (candidate.configuration != current.configuration)
        return reject("build configuration mismatch (expected " + current.configuration + ", got " + candidate.configuration + ")");
    if (candidate.compiler != current.compiler)
        return reject("compiler mismatch (expected " + current.compiler + ", got " + candidate.compiler + ")");
    if (reason)
        reason->clear();
    return true;
}
} // namespace MiniEngine
