#include "tinyengine/project/project_module_host.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/project_sdk/sdk_info.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <set>

namespace fs = std::filesystem;
namespace tinyengine
{
namespace
{
bool sameFileBytes(const fs::path& a, const fs::path& b)
{
    if (!fs::exists(b) || fs::file_size(a) != fs::file_size(b))
        return false;
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    return std::equal(std::istreambuf_iterator<char>(left), std::istreambuf_iterator<char>(),
                      std::istreambuf_iterator<char>(right));
}
void copyArtifact(const fs::path& source, const fs::path& target)
{
    if (!sameFileBytes(source, target))
        fs::copy_file(source, target, fs::copy_options::overwrite_existing);
}
bool inside(const fs::path& root, const fs::path& path)
{
    const auto relative = fs::relative(fs::weakly_canonical(path), root);
    return !relative.empty() && relative != "." && !relative.is_absolute() && *relative.begin() != "..";
}
} // namespace
ProjectModuleHost::~ProjectModuleHost()
{
    Close();
}
void ProjectModuleHost::release() noexcept
{
    endPlay();
    if (m_module && m_api.unload)
    {
        try
        {
            m_api.unload(m_module);
        }
        catch (...)
        {
        }
    }
    m_module = nullptr;
    m_api = {};
    m_library.Close();
    m_host = {};
    m_libraryPath.clear();
    m_pdbPath.clear();
    m_buildId.clear();
}
bool ProjectModuleHost::Fail(ProjectModuleState state, std::string error)
{
    release();
    m_state = state;
    m_lastError = std::move(error);
    return false;
}
bool ProjectModuleHost::load(const ProjectModuleSelection& selection, const TinyProjectHostAPI& host)
{
    if (m_state != ProjectModuleState::Unconfigured)
    {
        m_lastError = "Project module Load requires a fresh host; restart after recompiling";
        return false;
    }
    m_lastError.clear();
    try
    {
        if (host.abi_version != TINY_PROJECT_ABI_VERSION || host.struct_size < sizeof(TinyProjectHostAPI) || !host.log)
            return Fail(ProjectModuleState::Incompatible, "Incomplete project host API");
        if (selection.sdkBuildId != project_sdk::getSdkBuildId() || selection.projectBuildId.empty() ||
            selection.projectBuildId.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_") != std::string::npos ||
            selection.projectBuildId == "." || selection.projectBuildId == "..")
            return Fail(ProjectModuleState::Incompatible, "Invalid sdk.buildId or project buildId");
        const auto root = fs::canonical(selection.projectRoot);
        if (!inside(root, selection.libraryPath) || (!selection.pdbPath.empty() && !inside(root, selection.pdbPath)))
            return Fail(ProjectModuleState::Incompatible, "Module library or PDB escapes project root");
        if (!fs::is_regular_file(selection.libraryPath))
            return Fail(ProjectModuleState::Missing, "Missing module library: " + toUtf8Path(selection.libraryPath));
        const auto shadow = root / "Library/ProjectModules" / selection.projectBuildId;
        if (!inside(root, shadow))
            return Fail(ProjectModuleState::Incompatible, "Shadow directory escapes project root");
        fs::create_directories(shadow);
        std::set<fs::path> names;
        const auto copy = [&](const fs::path& path)
        {
            if (!inside(root, path) || !fs::is_regular_file(path))
                throw std::runtime_error("Missing or escaping module dependency: " + toUtf8Path(path));
            auto name = path.filename().wstring();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (!names.insert(fs::path(name)).second)
                throw std::runtime_error("Duplicate module artifact filename: " + toUtf8Path(path));
            copyArtifact(path, shadow / path.filename());
        };
        copy(selection.libraryPath);
        if (!selection.pdbPath.empty() && fs::exists(selection.pdbPath))
            copy(selection.pdbPath);
        for (const auto& dependency : selection.dependencies)
            copy(dependency);
        std::string error;
        if (!m_library.Open(shadow / selection.libraryPath.filename(), {shadow}, &error))
            return Fail(ProjectModuleState::Missing, error);
        const auto entry = reinterpret_cast<TinyProjectGetAPIFn>(m_library.findSymbol(TINY_PROJECT_ENTRY_POINT));
        if (!entry)
            return Fail(ProjectModuleState::Incompatible, "Missing TinyProject_GetAPI entry point");
        const auto* api = entry(TINY_PROJECT_ABI_VERSION);
        constexpr auto kRequired = offsetof(TinyProjectAPI, fixed_update);
        if (!api || api->struct_size < kRequired || api->abi_version != TINY_PROJECT_ABI_VERSION)
            return Fail(ProjectModuleState::Incompatible, "Project API is null, short, or ABI-incompatible");
        if (!validateProjectModuleInfo(api->info, selection, &error))
            return Fail(ProjectModuleState::Incompatible, error);
        if (!api->load || !api->unload || !api->create_session || !api->destroy_session || !api->begin_play ||
            !api->update || !api->end_play)
            return Fail(ProjectModuleState::Incompatible, "Project API is missing required callbacks");
        std::memcpy(&m_api, api, kRequired);
        if (api->struct_size >= offsetof(TinyProjectAPI, fixed_update) + sizeof(api->fixed_update))
            m_api.fixed_update = api->fixed_update;
        if (api->struct_size >= offsetof(TinyProjectAPI, late_update) + sizeof(api->late_update))
            m_api.late_update = api->late_update;
        m_host = host;
        if (m_api.load(&m_host, &m_module) != TINY_PROJECT_OK || !m_module)
            return Fail(ProjectModuleState::Faulted, "Project module load callback failed");
        m_buildId = selection.projectBuildId;
        m_libraryPath = shadow / selection.libraryPath.filename();
        if (!selection.pdbPath.empty() && fs::exists(selection.pdbPath))
            m_pdbPath = shadow / selection.pdbPath.filename();
        m_state = ProjectModuleState::Registered;
        return true;
    }
    catch (const std::exception& e)
    {
        return Fail(ProjectModuleState::Faulted, e.what());
    }
    catch (...)
    {
        return Fail(ProjectModuleState::Faulted, "Project module load threw an exception");
    }
}
bool ProjectModuleHost::beginPlay(TinyProjectWorld world)
{
    if (m_state != ProjectModuleState::Registered || !world.id)
        return false;
    m_lastError.clear();
    try
    {
        if (m_api.create_session(m_module, world, &m_session) != TINY_PROJECT_OK || !m_session)
            m_lastError = "Project create_session failed";
        else if (m_api.begin_play(m_session) != TINY_PROJECT_OK)
            m_lastError = "Project begin_play failed";
        else
        {
            m_begun = true;
            m_state = ProjectModuleState::Playing;
            return true;
        }
    }
    catch (...)
    {
        m_lastError = "Project session startup threw an exception";
    }
    m_state = ProjectModuleState::Faulted;
    endPlay();
    return false;
}
bool ProjectModuleHost::step(TinyProjectResult(TINY_PROJECT_CALL* callback)(void*, float), float dt, const char* name)
{
    if (m_state != ProjectModuleState::Playing || !m_session)
        return false;
    if (std::isfinite(dt) && dt >= 0)
    {
        try
        {
            if (!callback || callback(m_session, dt) == TINY_PROJECT_OK)
                return true;
        }
        catch (...)
        {
        }
    }
    m_lastError = std::string("Project ") + name + " failed";
    m_state = ProjectModuleState::Faulted;
    return false;
}
bool ProjectModuleHost::Update(float dt)
{
    return step(m_api.update, dt, "update");
}
bool ProjectModuleHost::fixedUpdate(float dt)
{
    return step(m_api.fixed_update, dt, "fixed_update");
}
bool ProjectModuleHost::lateUpdate(float dt)
{
    return step(m_api.late_update, dt, "late_update");
}
void ProjectModuleHost::endPlay() noexcept
{
    if (!m_session)
        return;
    if (m_begun)
    {
        m_begun = false;
        try
        {
            m_api.end_play(m_session);
        }
        catch (...)
        {
            m_state = ProjectModuleState::Faulted;
        }
    }
    auto* session = m_session;
    m_session = nullptr;
    try
    {
        m_api.destroy_session(session);
    }
    catch (...)
    {
        m_state = ProjectModuleState::Faulted;
    }
    if (m_state == ProjectModuleState::Playing)
        m_state = ProjectModuleState::Registered;
}
void ProjectModuleHost::Close() noexcept
{
    release();
    m_state = ProjectModuleState::Closed;
}
} // namespace tinyengine
