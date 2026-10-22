#include "tinyengine/platform/dynamic_library.h"
#include "tinyengine/platform/platform.h"
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace tinyengine
{
DynamicLibrary::~DynamicLibrary()
{
    Close();
}
DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : m_handle(std::exchange(other.m_handle, nullptr)), m_search(other.m_search)
{
}
DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
{
    if (this != &other)
    {
        Close();
        m_handle = std::exchange(other.m_handle, nullptr);
        m_search = other.m_search;
    }
    return *this;
}
bool DynamicLibrary::Open(const std::filesystem::path& path, const std::vector<std::filesystem::path>& directories,
                          std::string* error)
{
    Close();
    if (error)
        error->clear();
#ifdef _WIN32
    std::vector<DLL_DIRECTORY_COOKIE> cookies;
    DWORD failure = 0;
    if (m_search == DynamicLibrarySearch::PlatformDefault)
    {
        // Generic plugins retain the pre-existing native search behavior.
        m_handle = LoadLibraryW(std::filesystem::absolute(path).c_str());
        if (!m_handle)
            failure = GetLastError();
    }
    for (const auto& directory :
         m_search == DynamicLibrarySearch::PlatformDefault ? std::vector<std::filesystem::path>{} : directories)
    {
        const auto cookie = AddDllDirectory(std::filesystem::absolute(directory).c_str());
        if (!cookie)
        {
            failure = GetLastError();
            break;
        }
        cookies.push_back(cookie);
    }
    if (!failure && m_search == DynamicLibrarySearch::ExplicitDirectories)
    {
        m_handle = LoadLibraryExW(std::filesystem::absolute(path).c_str(), nullptr,
                                  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                      LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
        if (!m_handle)
            failure = GetLastError();
    }
    for (const auto cookie : cookies)
        RemoveDllDirectory(cookie);
    if (failure && error)
        *error = "Could not open library " + toUtf8Path(path) + " (Windows error " + std::to_string(failure) + ")";
#else
    (void)directories;
    m_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_handle && error)
        *error = "Could not open library " + toUtf8Path(path) + ": " + dlerror();
#endif
    return m_handle != nullptr;
}
void* DynamicLibrary::findSymbol(const char* name) const
{
    if (!m_handle || !name)
        return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(m_handle), name));
#else
    return dlsym(m_handle, name);
#endif
}
void DynamicLibrary::Close() noexcept
{
    if (!m_handle)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(m_handle));
#else
    dlclose(m_handle);
#endif
    m_handle = nullptr;
}
} // namespace tinyengine
