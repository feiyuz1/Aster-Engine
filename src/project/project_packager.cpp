// 文件：src/project/project_packager.cpp
// 作用：把烘焙资源、运行时文件和项目配置组装成可分发目录。
// 约定：编辑态和运行态场景相互隔离，运行结束后恢复进入前快照。
// 类型：实现文件。

#include "tinyengine/project/project_packager.h"

#include "tinyengine/build/asset_compiler.h"
#include "tinyengine/build/build_compatibility.h"
#include "tinyengine/core/config.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/project/project.h"
#include "tinyengine/resource/pak_archive.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace tinyengine
{
namespace
{
bool isSafeRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute())
        return false;
    const auto normalized = path.lexically_normal();
    for (const auto& part : normalized)
        if (part == "..")
            return false;
    return true;
}

bool isInside(const std::filesystem::path& child, const std::filesystem::path& parent)
{
    std::error_code error;
    const auto relative = std::filesystem::relative(child, parent, error);
    if (error || relative.empty())
        return !error;
    const auto first = *relative.begin();
    return first != ".." && !relative.is_absolute();
}

std::string safeFileName(std::string name)
{
    for (char& character : name)
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-' && character != '_')
            character = '_';
    while (!name.empty() && name.back() == '_')
        name.pop_back();
    return name.empty() ? "TinyGame" : name;
}

bool isAuthoringOnly(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".meta" || extension == ".fbx" || extension == ".obj" || extension == ".dae" ||
           extension == ".3ds" || extension == ".blend" || extension == ".gltf" || extension == ".glb";
}

bool copyFile(const std::filesystem::path& source, const std::filesystem::path& destination, PackageResult& result)
{
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (!error)
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (error)
    {
        result.errors.push_back("Cannot stage " + source.generic_string() + ": " + error.message());
        return false;
    }
    ++result.stagedFiles;
    return true;
}

bool copyAssetTree(const std::filesystem::path& sourceRoot, const std::filesystem::path& destinationRoot,
                   PackageResult& result)
{
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(sourceRoot, error), end; iterator != end && !error;
         iterator.increment(error))
    {
        if (!iterator->is_regular_file() || isAuthoringOnly(iterator->path()))
            continue;
        const auto relative = std::filesystem::relative(iterator->path(), sourceRoot, error);
        if (error)
            return false;
        const auto extension = iterator->path().extension();
        if (extension == ".mat" || extension == ".matgraph")
        {
            const auto compiled =
                PlatformAssetCompiler::compile(iterator->path(), destinationRoot / relative, sourceRoot);
            if (!compiled.success)
            {
                result.errors.push_back("Material CPU validation failed: " + toUtf8GenericPath(iterator->path()) +
                                        ": " + compiled.error);
                return false;
            }
            ++result.stagedFiles;
        }
        else if (!copyFile(iterator->path(), destinationRoot / relative, result))
            return false;
    }
    if (error)
        result.errors.push_back("Cannot enumerate assets: " + error.message());
    return !error;
}
} // namespace

PackageResult ProjectPackager::package(const PackageOptions& options)
{
    PackageResult result;
    ProjectSettings project;
    if (!project.load(options.projectFile))
    {
        result.errors.emplace_back("Cannot load project: " + options.projectFile.generic_string());
        return result;
    }
    if (!std::filesystem::is_regular_file(options.gameExecutable))
    {
        result.errors.emplace_back("Game executable not found: " + options.gameExecutable.generic_string());
        return result;
    }
    const auto sourceBuildInfo = BuildCompatibility::sidecarPath(options.gameExecutable);
    RuntimeBuildInfo candidateBuild;
    if (!BuildCompatibility::load(sourceBuildInfo, candidateBuild))
    {
        result.errors.emplace_back("Game build metadata is missing or invalid: " + sourceBuildInfo.generic_string());
        return result;
    }
    std::string compatibilityError;
    if (!BuildCompatibility::isCompatible(candidateBuild, &compatibilityError))
    {
        result.errors.emplace_back("Incompatible Game executable: " + compatibilityError);
        return result;
    }
    if (!isSafeRelativePath(project.assetDirectory) || !isSafeRelativePath(project.startupScene))
    {
        result.errors.emplace_back("Packaged asset and startup-scene paths must be relative and cannot contain '..'");
        return result;
    }

    const auto projectRoot = project.projectFile.parent_path();
    const auto assetSource = project.resolve(project.assetDirectory);
    const auto sceneSource = project.resolve(project.startupScene);
    if (!std::filesystem::is_directory(assetSource))
    {
        result.errors.emplace_back("Asset directory not found: " + assetSource.generic_string());
        return result;
    }
    if (!std::filesystem::is_regular_file(sceneSource))
    {
        result.errors.emplace_back("Startup scene not found: " + sceneSource.generic_string());
        return result;
    }

    const auto output = std::filesystem::absolute(options.outputDirectory).lexically_normal();
    if (output == output.root_path() || output == projectRoot || isInside(output, assetSource))
    {
        result.errors.emplace_back("Unsafe package output directory: " + output.generic_string());
        return result;
    }
    result.outputDirectory = output;
    const auto stage = output / ".tinyengine-stage";
    std::error_code error;
    std::filesystem::remove_all(stage, error);
    error.clear();
    std::filesystem::create_directories(stage, error);
    if (error)
    {
        result.errors.emplace_back("Cannot create package staging directory: " + error.message());
        return result;
    }

    if (!copyAssetTree(assetSource, stage / project.assetDirectory, result))
    {
        std::filesystem::remove_all(stage, error);
        return result;
    }
    const auto stagedScene = stage / project.startupScene;
    if (!copyFile(sceneSource, stagedScene, result))
    {
        std::filesystem::remove_all(stage, error);
        return result;
    }

    const auto contentDirectory = output / "Content";
    std::filesystem::create_directories(contentDirectory, error);
    result.archive = contentDirectory / "Game.tpak";
    if (error || !PakArchive::packDirectory(stage, result.archive))
    {
        result.errors.emplace_back(error ? "Cannot create Content directory: " + error.message()
                                         : "Cannot create Game.tpak");
        std::filesystem::remove_all(stage, error);
        return result;
    }
    result.archiveBytes = std::filesystem::file_size(result.archive, error);
    std::filesystem::remove_all(stage, error);

    const std::string packageName = safeFileName(project.name);
#ifdef _WIN32
    const std::string executableName = packageName + ".exe";
#else
    const std::string executableName = packageName;
#endif
    result.executable = output / executableName;
    if (!copyFile(options.gameExecutable, result.executable, result))
        return result;
    result.buildInfo = BuildCompatibility::sidecarPath(result.executable);
    if (!copyFile(sourceBuildInfo, result.buildInfo, result))
        return result;

    ProjectSettings shippingProject = project;
    shippingProject.packageArchive = "Content/Game.tpak";
    shippingProject.cookedDirectory = "Content";
    result.projectFile = output / (packageName + ".tinyproject");
    if (!shippingProject.Save(result.projectFile))
    {
        result.errors.emplace_back("Cannot write packaged project settings");
        return result;
    }

    Config manifest;
    manifest.set("package.version", "1");
    manifest.set("package.project", project.name);
    manifest.set("package.executable", executableName);
    manifest.set("package.projectFile", result.projectFile.filename().generic_string());
    manifest.set("package.archive", "Content/Game.tpak");
    manifest.set("package.startupScene", project.startupScene.generic_string());
    manifest.set("package.engineVersion", candidateBuild.engineVersion);
    manifest.set("package.engineAbi", std::to_string(candidateBuild.engineAbi));
    manifest.set("package.sceneFormat", std::to_string(candidateBuild.sceneFormat));
    manifest.set("package.buildId", candidateBuild.buildId);
    manifest.set("package.configuration", candidateBuild.configuration);
    manifest.set("package.compiler", candidateBuild.compiler);
    manifest.set("package.stagedFiles", std::to_string(result.stagedFiles));
    manifest.set("package.archiveBytes", std::to_string(result.archiveBytes));
    if (!manifest.Save((output / "Package.manifest").string()))
    {
        result.errors.emplace_back("Cannot write Package.manifest");
        return result;
    }

    PlatformFile::writeAllText(output / "RUN_GAME.txt",
                               "Run " + executableName + " " + result.projectFile.filename().generic_string() + "\n");
    const auto entries = PakArchive::List(result.archive);
    const std::string startupPath = project.startupScene.lexically_normal().generic_string();
    if (std::none_of(entries.begin(), entries.end(), [&](const PakEntry& entry) { return entry.path == startupPath; }))
    {
        result.errors.emplace_back("Package verification failed: startup scene is missing from Game.tpak");
        return result;
    }

    result.success = result.errors.empty();
    return result;
}
} // namespace tinyengine
