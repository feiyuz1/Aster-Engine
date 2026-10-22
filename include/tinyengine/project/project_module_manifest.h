#pragma once

#include "tinyengine/project_sdk/project_api.h"
#include <filesystem>
#include <string>
#include <vector>

namespace tinyengine
{
struct ProjectSettings;
enum class ProjectCodeProfile
{
    Development,
    Shipping
};

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

const char* projectCodeProfileName(ProjectCodeProfile profile) noexcept;
bool loadProjectModuleSelection(const ProjectSettings& settings, ProjectCodeProfile profile,
                                ProjectModuleSelection& out, std::string* error = nullptr);
bool validateProjectModuleInfo(const TinyProjectModuleInfo& info, const ProjectModuleSelection& selection,
                               std::string* error = nullptr);
} // namespace tinyengine
