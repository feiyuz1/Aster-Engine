// 文件：include/MiniEngine/Plugin/PluginManager.hpp
// 作用：发现、校验和装卸动态插件，并维护稳定 ABI 的生命周期。
// 约定：跨动态库边界仅使用版本化 C ABI，所有权由接口文档约定。
// 类型：公开声明。

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
