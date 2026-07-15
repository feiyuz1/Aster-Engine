#pragma once

#include "MiniEngine/Plugin/PluginAPI.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace MiniEngine
{
class PluginManager
{
  public:
    using LogCallback = std::function<void(TinyPluginLogLevel, const std::string &)>;
    ~PluginManager();
    void SetLogCallback(LogCallback callback);
    bool Load(const std::filesystem::path &path);
    bool RegisterStatic(const TinyPluginAPI *api);
    void Tick(double deltaSeconds);
    void UnloadAll();
    std::vector<std::string> GetLoadedNames() const;
    const std::string &GetLastError() const;

  private:
    struct LoadedPlugin
    {
        void *library = nullptr;
        const TinyPluginAPI *api = nullptr;
        std::string name;
    };
    bool Register(void *library, const TinyPluginAPI *api);
    static void TINY_PLUGIN_CALL LogBridge(void *context, TinyPluginLogLevel level, const char *message, std::size_t length);
    static void *TINY_PLUGIN_CALL AllocateBridge(void *, std::size_t size, std::size_t alignment);
    static void TINY_PLUGIN_CALL DeallocateBridge(void *, void *memory, std::size_t alignment);

    LogCallback m_logCallback;
    std::vector<LoadedPlugin> m_plugins;
    std::string m_lastError;
};
} // namespace MiniEngine
