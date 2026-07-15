#include "MiniEngine/Build/TargetPlatform.hpp"
#include "MiniEngine/Build/AssetCompiler.hpp"

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/Render/ShaderPipeline.hpp"
#include "MiniEngine/Resource/PakArchive.hpp"
#include "MiniEngine/Serialization/Archive.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace MiniEngine
{
namespace
{
std::string Lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool CopyFile(const std::filesystem::path &source, const std::filesystem::path &destination, std::string &errorMessage)
{
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (!error)
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (!error)
        return true;
    errorMessage = source.generic_string() + ": " + error.message();
    return false;
}

bool IsSafeRelativePath(const std::filesystem::path &path)
{
    if (path.empty() || path.is_absolute())
        return false;
    for (const auto &part : path.lexically_normal())
        if (part == "..")
            return false;
    return true;
}
} // namespace

TargetPlatformRegistry::TargetPlatformRegistry()
{
    m_platforms.push_back(
        {"Windows", ".exe", "WIC/RGBA", "GLSL330", false, {".fbx", ".obj", ".dae", ".3ds", ".blend", ".gltf", ".glb", ".psd", ".pdb"}});
    m_platforms.push_back(
        {"Linux", "", "RGBA", "GLSL330", true, {".fbx", ".obj", ".dae", ".3ds", ".blend", ".gltf", ".glb", ".psd", ".pdb"}});
}

const TargetPlatformRegistry &TargetPlatformRegistry::Get()
{
    static const TargetPlatformRegistry registry;
    return registry;
}

const TargetPlatform *TargetPlatformRegistry::Find(const std::string &name) const
{
    const std::string query = Lower(name);
    for (const auto &platform : m_platforms)
        if (Lower(platform.name) == query)
            return &platform;
    return nullptr;
}

const std::vector<TargetPlatform> &TargetPlatformRegistry::GetPlatforms() const
{
    return m_platforms;
}

PlatformCookResult TargetPlatformCooker::Cook(const PlatformCookOptions &options)
{
    PlatformCookResult result;
    ProjectSettings project;
    if (!project.Load(options.projectFile))
    {
        result.errors.push_back("Cannot load project: " + options.projectFile.generic_string());
        return result;
    }
    if (!IsSafeRelativePath(project.assetDirectory) || !IsSafeRelativePath(project.startupScene))
    {
        result.errors.push_back("Platform cook asset and startup-scene paths must be safe relative paths");
        return result;
    }
    const TargetPlatform *platform = TargetPlatformRegistry::Get().Find(options.targetPlatform);
    if (!platform)
    {
        result.errors.push_back("Unknown target platform: " + options.targetPlatform);
        return result;
    }
    result.outputDirectory = std::filesystem::absolute(options.outputDirectory / platform->name).lexically_normal();
    if (result.outputDirectory == result.outputDirectory.root_path())
    {
        result.errors.push_back("Unsafe platform cook output directory");
        return result;
    }
    const auto cookedRoot = result.outputDirectory / "Cooked";
    const auto cookedAssets = cookedRoot / project.assetDirectory;
    const auto assetArchive = result.outputDirectory / "AssetCook.tpak";
    std::error_code error;
    if (!options.incremental)
        std::filesystem::remove_all(cookedRoot, error);
    error.clear();
    std::filesystem::create_directories(cookedAssets, error);
    if (error)
    {
        result.errors.push_back("Cannot create platform cook directory: " + error.message());
        return result;
    }
    CookOptions cookOptions;
    cookOptions.incremental = options.incremental;
    const auto excluded = platform->excludedExtensions;
    cookOptions.include = [excluded](const std::filesystem::path &path) {
        const std::string extension = Lower(path.extension().string());
        return std::find(excluded.begin(), excluded.end(), extension) == excluded.end();
    };
    cookOptions.transform = [](const std::filesystem::path &source, const std::filesystem::path &destination, std::string &message) {
        const auto compiled = PlatformAssetCompiler::Compile(source, destination);
        message = compiled.error;
        return compiled.success;
    };
    result.assets = AssetCooker::Cook(project.Resolve(project.assetDirectory), cookedAssets, assetArchive, cookOptions);
    if (!result.assets.success)
    {
        result.errors.insert(result.errors.end(), result.assets.errors.begin(), result.assets.errors.end());
        return result;
    }
    std::filesystem::remove(assetArchive, error);

    const auto sceneSource = project.Resolve(project.startupScene);
    std::string copyError;
    if (!std::filesystem::is_regular_file(sceneSource) || !CopyFile(sceneSource, cookedRoot / project.startupScene, copyError))
    {
        result.errors.push_back(copyError.empty() ? "Startup scene does not exist: " + sceneSource.generic_string() : copyError);
        return result;
    }

    const auto shaderCache = cookedRoot / "ShaderCache" / platform->name;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(project.Resolve(project.assetDirectory), error))
    {
        if (error)
            break;
        if (!entry.is_regular_file() || (entry.path().extension() != ".vert" && entry.path().extension() != ".frag"))
            continue;
        const auto variants = ShaderPipeline::BuildVariants(entry.path());
        for (const auto &variant : variants)
        {
            if (!variant.success || !ShaderPipeline::WriteCache(variant, shaderCache))
            {
                result.errors.push_back(variant.error.empty() ? "Cannot cache shader: " + entry.path().generic_string() : variant.error);
                continue;
            }
            ++result.shaderVariants;
        }
    }
    if (error)
    {
        result.errors.push_back("Cannot enumerate shaders: " + error.message());
        return result;
    }

    const auto content = result.outputDirectory / "Content";
    std::filesystem::create_directories(content, error);
    result.archive = content / "Game.tpak";
    if (error || !PakArchive::PackDirectory(cookedRoot, result.archive))
    {
        result.errors.push_back(error ? error.message() : "Cannot create platform Game.tpak");
        return result;
    }

    VersionedDocument document;
    document.format = "TinyEnginePlatformCook";
    document.version = 1;
    document.fields = {{"project", project.name},
                       {"platform", platform->name},
                       {"configuration", ToString(options.configuration)},
                       {"textureFormat", platform->textureFormat},
                       {"shaderFormat", platform->shaderFormat},
                       {"archive", "Content/Game.tpak"},
                       {"startupScene", project.startupScene.generic_string()},
                       {"shaderVariants", std::to_string(result.shaderVariants)}};
    result.manifest = result.outputDirectory / "PlatformCook.manifest";
    if (!VersionedTextArchive::SaveAtomic(result.manifest, document, false))
    {
        result.errors.push_back("Cannot write platform cook manifest");
        return result;
    }
    result.success = result.errors.empty();
    return result;
}

std::optional<BuildConfiguration> TargetPlatformCooker::ParseConfiguration(const std::string &value)
{
    const std::string normalized = Lower(value);
    if (normalized == "debug")
        return BuildConfiguration::Debug;
    if (normalized == "development" || normalized == "dev")
        return BuildConfiguration::Development;
    if (normalized == "shipping" || normalized == "release")
        return BuildConfiguration::Shipping;
    return std::nullopt;
}

const char *TargetPlatformCooker::ToString(const BuildConfiguration configuration)
{
    switch (configuration)
    {
    case BuildConfiguration::Debug:
        return "Debug";
    case BuildConfiguration::Development:
        return "Development";
    case BuildConfiguration::Shipping:
        return "Shipping";
    }
    return "Development";
}
} // namespace MiniEngine
