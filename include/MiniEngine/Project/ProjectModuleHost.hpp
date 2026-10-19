#pragma once
#include "MiniEngine/Platform/DynamicLibrary.hpp"
#include "MiniEngine/Project/ProjectModuleManifest.hpp"

namespace MiniEngine
{
enum class ProjectModuleState { Unconfigured, Missing, Incompatible, Registered, Playing, Faulted, Closed };
class ProjectModuleHost
{
public:
    ProjectModuleHost() = default;
    ~ProjectModuleHost();
    ProjectModuleHost(const ProjectModuleHost &) = delete;
    ProjectModuleHost &operator=(const ProjectModuleHost &) = delete;
    bool Load(const ProjectModuleSelection &, const TinyProjectHostAPI &);
    void Close() noexcept;
    const std::string &GetLastError() const { return m_lastError; }
    ProjectModuleState GetState() const { return m_state; }
    const std::string &GetLoadedBuildId() const { return m_buildId; }
    const std::filesystem::path &GetLoadedLibraryPath() const { return m_libraryPath; }
    const std::filesystem::path &GetLoadedPdbPath() const { return m_pdbPath; }
private:
    bool Fail(ProjectModuleState, std::string);
    void Release() noexcept;
    DynamicLibrary m_library;
    TinyProjectHostAPI m_host{};
    TinyProjectAPI m_api{};
    void *m_module = nullptr;
    ProjectModuleState m_state = ProjectModuleState::Unconfigured;
    std::string m_lastError, m_buildId;
    std::filesystem::path m_libraryPath, m_pdbPath;
};
}
