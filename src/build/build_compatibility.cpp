// 文件：src/build/build_compatibility.cpp
// 作用：读写并校验运行时构建兼容标识。

#include "tinyengine/build/build_compatibility.h"

#include "tinyengine/core/config.h"

#include <algorithm>

namespace tinyengine
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

RuntimeBuildInfo BuildCompatibility::current()
{
    return {TINYENGINE_VERSION,
            kEngineAbiVersion,
            kSceneFormatVersion,
            TINYENGINE_BUILD_ID,
            TINYENGINE_BUILD_CONFIGURATION,
            TINYENGINE_COMPILER_ID};
}

std::filesystem::path BuildCompatibility::sidecarPath(const std::filesystem::path& executable)
{
    return executable.string() + ".buildinfo";
}

bool BuildCompatibility::load(const std::filesystem::path& path, RuntimeBuildInfo& info)
{
    Config config;
    if (!config.load(path.string()))
        return false;
    info.engineVersion = config.getString("engine.version");
    info.engineAbi = static_cast<std::uint32_t>(std::max(0, config.getInt("engine.abi", 0)));
    info.sceneFormat = static_cast<std::uint32_t>(std::max(0, config.getInt("scene.format", 0)));
    info.buildId = config.getString("build.id");
    info.configuration = config.getString("build.configuration");
    info.compiler = config.getString("build.compiler");
    return !info.engineVersion.empty() && info.engineAbi != 0 && info.sceneFormat != 0 && !info.buildId.empty() &&
           !info.configuration.empty() && !info.compiler.empty();
}

bool BuildCompatibility::Save(const std::filesystem::path& path, const RuntimeBuildInfo& info)
{
    Config config;
    config.set("engine.version", info.engineVersion);
    config.set("engine.abi", std::to_string(info.engineAbi));
    config.set("scene.format", std::to_string(info.sceneFormat));
    config.set("build.id", info.buildId);
    config.set("build.configuration", info.configuration);
    config.set("build.compiler", info.compiler);
    return config.Save(path.string());
}

bool BuildCompatibility::isCompatible(const RuntimeBuildInfo& candidate, std::string* reason)
{
    const RuntimeBuildInfo active = current();
    const auto reject = [reason](const std::string& message)
    {
        if (reason)
            *reason = message;
        return false;
    };
    if (candidate.engineAbi != active.engineAbi)
        return reject("engine ABI mismatch (expected " + std::to_string(active.engineAbi) + ", got " +
                      std::to_string(candidate.engineAbi) + ")");
    if (candidate.sceneFormat != active.sceneFormat)
        return reject("scene format mismatch (expected " + std::to_string(active.sceneFormat) + ", got " +
                      std::to_string(candidate.sceneFormat) + ")");
    if (candidate.engineVersion != active.engineVersion)
        return reject("engine version mismatch (expected " + active.engineVersion + ", got " + candidate.engineVersion +
                      ")");
    if (candidate.buildId != active.buildId)
        return reject("build ID mismatch (expected " + active.buildId + ", got " + candidate.buildId + ")");
    if (candidate.configuration != active.configuration)
        return reject("build configuration mismatch (expected " + active.configuration + ", got " +
                      candidate.configuration + ")");
    if (candidate.compiler != active.compiler)
        return reject("compiler mismatch (expected " + active.compiler + ", got " + candidate.compiler + ")");
    if (reason)
        reason->clear();
    return true;
}
} // namespace tinyengine
