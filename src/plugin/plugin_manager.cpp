// 文件：src/plugin/plugin_manager.cpp
// 作用：发现、校验和装卸动态插件，并维护稳定 ABI 的生命周期。
// 约定：跨动态库边界仅使用版本化 C ABI，所有权由接口文档约定。
// 类型：实现文件。

#include "tinyengine/plugin/plugin_manager.h"

#include <algorithm>
#include <cstdlib>
#include <new>

#include <malloc.h>

namespace tinyengine
{
PluginManager::~PluginManager()
{
    unloadAll();
}

void PluginManager::setLogCallback(LogCallback callback)
{
    m_logCallback = std::move(callback);
}

bool PluginManager::load(const std::filesystem::path& path)
{
    m_lastError.clear();
    DynamicLibrary library(DynamicLibrarySearch::PlatformDefault);
    if (!library.Open(path, {std::filesystem::absolute(path).parent_path()}, &m_lastError))
        return false;
    auto getApi = reinterpret_cast<TinyPluginGetAPIFn>(library.findSymbol(TINY_PLUGIN_ENTRY_POINT));
    if (!getApi)
    {
        m_lastError = "Plugin entry point is missing";
        return false;
    }
    const TinyPluginAPI* api = getApi(TINY_PLUGIN_ABI_VERSION);
    return Register(std::move(library), api);
}

bool PluginManager::registerStatic(const TinyPluginAPI* api)
{
    m_lastError.clear();
    return Register({}, api);
}

// 在调用插件代码前验证 ABI 版本、结构尺寸和必需函数；失败时立即归还动态库句柄。
bool PluginManager::Register(DynamicLibrary library, const TinyPluginAPI* api)
{
    if (!api || api->abi_version != TINY_PLUGIN_ABI_VERSION || api->struct_size < sizeof(TinyPluginAPI) || !api->name ||
        !api->startup || !api->shutdown)
    {
        m_lastError = "Plugin API is null, incomplete, or ABI-incompatible";
        return false;
    }
    if (std::any_of(m_plugins.begin(), m_plugins.end(),
                    [api](const LoadedPlugin& plugin) { return plugin.name == api->name; }))
    {
        m_lastError = "A plugin with the same name is already loaded";
        return false;
    }
    auto host = std::make_unique<TinyPluginHostAPI>(
        TinyPluginHostAPI{TINY_PLUGIN_ABI_VERSION, sizeof(TinyPluginHostAPI), this, &PluginManager::LogBridge,
                          &PluginManager::AllocateBridge, &PluginManager::DeallocateBridge});
    if (api->startup(host.get()) == 0)
    {
        m_lastError = "Plugin startup rejected the host";
        return false;
    }
    m_plugins.push_back({std::move(library), api, api->name, std::move(host)});
    return true;
}

void PluginManager::Tick(const double deltaSeconds)
{
    for (const auto& plugin : m_plugins)
        if (plugin.api->tick)
            plugin.api->tick(std::max(0.0, deltaSeconds));
}

// 按加载逆序关闭插件，允许后加载插件在 Shutdown 中访问其依赖的早期插件。
void PluginManager::unloadAll()
{
    for (auto plugin = m_plugins.rbegin(); plugin != m_plugins.rend(); ++plugin)
    {
        plugin->api->shutdown();
        plugin->library.Close();
    }
    m_plugins.clear();
}

std::vector<std::string> PluginManager::GetLoadedNames() const
{
    std::vector<std::string> result;
    for (const auto& plugin : m_plugins)
        result.push_back(plugin.name);
    return result;
}

const std::string& PluginManager::GetLastError() const
{
    return m_lastError;
}

void TINY_PLUGIN_CALL PluginManager::LogBridge(void* context, const TinyPluginLogLevel level, const char* message,
                                               const std::size_t length)
{
    auto& manager = *static_cast<PluginManager*>(context);
    if (manager.m_logCallback && message)
        manager.m_logCallback(level, std::string(message, length));
}

void* TINY_PLUGIN_CALL PluginManager::AllocateBridge(void*, const std::size_t size, const std::size_t alignment)
{
#ifdef _WIN32
    return _aligned_malloc(size, std::max(alignment, alignof(void*)));
#else
    void* memory = nullptr;
    return posix_memalign(&memory, std::max(alignment, alignof(void*)), size) == 0 ? memory : nullptr;
#endif
}

void TINY_PLUGIN_CALL PluginManager::DeallocateBridge(void*, void* memory, const std::size_t)
{
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
} // namespace tinyengine
