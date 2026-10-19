#pragma once

#include "MiniEngine/ProjectSDK/ProjectAPI.h"
#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
struct ProjectSettings;
enum class ProjectCodeProfile { Development, Shipping };

struct ProjectModuleSelection
{
    std::filesystem::path projectRoot;
    std::string moduleId;
    std::string projectId;
    std::string sdkBuildId;
    std::string projectBuildId;
    std::string sourceFingerprint;
    ProjectCodeProfile profile = ProjectCodeProfile::Development;
    std::filesystem::path libraryPath;
    std::filesystem::path pdbPath;
    std::vector<std::filesystem::path> dependencies;
};

const char *ProjectCodeProfileName(ProjectCodeProfile profile) noexcept;
bool LoadProjectModuleSelection(const ProjectSettings &settings, ProjectCodeProfile profile,
                                ProjectModuleSelection &out, std::string *error = nullptr);
bool ValidateProjectModuleInfo(const TinyProjectModuleInfo &info, const ProjectModuleSelection &selection,
                               std::string *error = nullptr);
}
