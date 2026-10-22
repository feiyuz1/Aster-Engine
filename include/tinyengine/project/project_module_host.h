#pragma once
#include "tinyengine/platform/dynamic_library.h"
#include "tinyengine/project/project_module_manifest.h"

namespace tinyengine
{
enum class ProjectModuleState
{
    Unconfigured,
    Missing,
    Incompatible,
    Registered,
    Playing,
    Faulted,
    Closed
};
class ProjectModuleHost
{
  public:
    ProjectModuleHost() = default;
    ~ProjectModuleHost();
    ProjectModuleHost(const ProjectModuleHost&) = delete;
    ProjectModuleHost& operator=(const ProjectModuleHost&) = delete;
    bool load(const ProjectModuleSelection&, const TinyProjectHostAPI&);
    bool beginPlay(TinyProjectWorld);
    bool Update(float);
    bool fixedUpdate(float);
    bool lateUpdate(float);
    void endPlay() noexcept;
    void Close() noexcept;
    const std::string& GetLastError() const
    {
        return m_lastError;
    }
    ProjectModuleState getState() const
    {
        return m_state;
    }
    const std::string& GetLoadedBuildId() const
    {
        return m_buildId;
    }
    const std::filesystem::path& GetLoadedLibraryPath() const
    {
        return m_libraryPath;
    }
    const std::filesystem::path& GetLoadedPdbPath() const
    {
        return m_pdbPath;
    }

  private:
    bool Fail(ProjectModuleState, std::string);
    void release() noexcept;
    bool step(TinyProjectResult(TINY_PROJECT_CALL*)(void*, float), float, const char*);
    DynamicLibrary m_library;
    TinyProjectHostAPI m_host{};
    TinyProjectAPI m_api{};
    void* m_module = nullptr;
    void* m_session = nullptr;
    bool m_begun = false;
    ProjectModuleState m_state = ProjectModuleState::Unconfigured;
    std::string m_lastError, m_buildId;
    std::filesystem::path m_libraryPath, m_pdbPath;
};
} // namespace tinyengine
