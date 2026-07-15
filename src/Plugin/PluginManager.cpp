#include "MiniEngine/Plugin/PluginManager.hpp"

#include <algorithm>
#include <cstdlib>
#include <new>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace MiniEngine
{
namespace
{
void CloseLibrary(void *library)
{
    if (!library)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}
} // namespace

PluginManager::~PluginManager()
{
    UnloadAll();
}

void PluginManager::SetLogCallback(LogCallback callback)
{
    m_logCallback = std::move(callback);
}

bool PluginManager::Load(const std::filesystem::path &path)
{
    m_lastError.clear();
#ifdef _WIN32
    void *library = static_cast<void *>(LoadLibraryW(path.wstring().c_str()));
    auto getApi =
        library ? reinterpret_cast<TinyPluginGetAPIFn>(GetProcAddress(static_cast<HMODULE>(library), TINY_PLUGIN_ENTRY_POINT)) : nullptr;
#else
    void *library = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    auto getApi = library ? reinterpret_cast<TinyPluginGetAPIFn>(dlsym(library, TINY_PLUGIN_ENTRY_POINT)) : nullptr;
#endif
    if (!library || !getApi)
    {
        m_lastError = library ? "Plugin entry point is missing" : "Plugin library could not be opened";
        CloseLibrary(library);
        return false;
    }
    const TinyPluginAPI *api = getApi(TINY_PLUGIN_ABI_VERSION);
    if (!Register(library, api))
    {
        CloseLibrary(library);
        return false;
    }
    return true;
}

bool PluginManager::RegisterStatic(const TinyPluginAPI *api)
{
    m_lastError.clear();
    return Register(nullptr, api);
}

bool PluginManager::Register(void *library, const TinyPluginAPI *api)
{
    if (!api || api->abi_version != TINY_PLUGIN_ABI_VERSION || api->struct_size < sizeof(TinyPluginAPI) || !api->name || !api->startup ||
        !api->shutdown)
    {
        m_lastError = "Plugin API is null, incomplete, or ABI-incompatible";
        return false;
    }
    if (std::any_of(m_plugins.begin(), m_plugins.end(), [api](const LoadedPlugin &plugin) { return plugin.name == api->name; }))
    {
        m_lastError = "A plugin with the same name is already loaded";
        return false;
    }
    const TinyPluginHostAPI host{TINY_PLUGIN_ABI_VERSION,   sizeof(TinyPluginHostAPI),      this,
                                 &PluginManager::LogBridge, &PluginManager::AllocateBridge, &PluginManager::DeallocateBridge};
    if (api->startup(&host) == 0)
    {
        m_lastError = "Plugin startup rejected the host";
        return false;
    }
    m_plugins.push_back({library, api, api->name});
    return true;
}

void PluginManager::Tick(const double deltaSeconds)
{
    for (const auto &plugin : m_plugins)
        if (plugin.api->tick)
            plugin.api->tick(std::max(0.0, deltaSeconds));
}

void PluginManager::UnloadAll()
{
    for (auto plugin = m_plugins.rbegin(); plugin != m_plugins.rend(); ++plugin)
    {
        plugin->api->shutdown();
        CloseLibrary(plugin->library);
    }
    m_plugins.clear();
}

std::vector<std::string> PluginManager::GetLoadedNames() const
{
    std::vector<std::string> result;
    for (const auto &plugin : m_plugins)
        result.push_back(plugin.name);
    return result;
}

const std::string &PluginManager::GetLastError() const
{
    return m_lastError;
}

void TINY_PLUGIN_CALL PluginManager::LogBridge(void *context, const TinyPluginLogLevel level, const char *message, const std::size_t length)
{
    auto &manager = *static_cast<PluginManager *>(context);
    if (manager.m_logCallback && message)
        manager.m_logCallback(level, std::string(message, length));
}

void *TINY_PLUGIN_CALL PluginManager::AllocateBridge(void *, const std::size_t size, const std::size_t alignment)
{
#ifdef _WIN32
    return _aligned_malloc(size, std::max(alignment, alignof(void *)));
#else
    void *memory = nullptr;
    return posix_memalign(&memory, std::max(alignment, alignof(void *)), size) == 0 ? memory : nullptr;
#endif
}

void TINY_PLUGIN_CALL PluginManager::DeallocateBridge(void *, void *memory, const std::size_t)
{
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
} // namespace MiniEngine
