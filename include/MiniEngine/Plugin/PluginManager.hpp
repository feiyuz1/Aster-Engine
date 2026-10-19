// 文件：include/MiniEngine/Plugin/PluginManager.hpp
// 作用：发现、校验和装卸动态插件，并维护稳定 ABI 的生命周期。
// 约定：跨动态库边界仅使用版本化 C ABI，所有权由接口文档约定。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Plugin/PluginAPI.h"
#include "MiniEngine/Platform/DynamicLibrary.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniEngine
{
class PluginManager
{
  public:
    using LogCallback = std::function<void(TinyPluginLogLevel, const std::string &)>;
    /// @brief 析构 `PluginManager` 对象并释放其持有资源。
    ~PluginManager();
    /// @brief 设置 `LogCallback`。
    /// @param callback 满足条件时调用的回调函数。
    void SetLogCallback(LogCallback callback);
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::filesystem::path &path);
    /// @brief 注册 `Static`。
    /// @param api 插件或宿主 API 函数表。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool RegisterStatic(const TinyPluginAPI *api);
    /// @brief 推进一次按帧更新。
    /// @param deltaSeconds 时间值，单位按接口约定为秒。
    void Tick(double deltaSeconds);
    /// @brief 卸载 `All`。
    void UnloadAll();
    /// @brief 获取 `LoadedNames`。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<std::string> GetLoadedNames() const;
    /// @brief 获取 `LastError`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetLastError() const;

  private:
    struct LoadedPlugin
    {
        DynamicLibrary library;
        const TinyPluginAPI *api = nullptr;
        std::string name;
        std::unique_ptr<TinyPluginHostAPI> host;
    };
    /// @brief 注册对象或处理器。
    /// @param library 接口所需的输入参数。
    /// @param api 插件或宿主 API 函数表。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Register(DynamicLibrary library, const TinyPluginAPI *api);
    /// @brief 记录日志 `Bridge`。
    /// @param context 要处理的输入或输出数据。
    /// @param level 日志、Mip 或层级值。
    /// @param message 要记录或返回的诊断消息。
    /// @param length 数据长度或几何长度。
    static void TINY_PLUGIN_CALL LogBridge(void *context, TinyPluginLogLevel level, const char *message, std::size_t length);
    /// @brief 分配 `Bridge`。
    /// @param size 数据、内存或目标区域的尺寸。
    /// @param alignment 内存对齐字节数，必须为有效的二次幂。
    /// @return 返回主程序分配的内存；申请失败时返回 `nullptr`。
    static void *TINY_PLUGIN_CALL AllocateBridge(void *, std::size_t size, std::size_t alignment);
    /// @brief 释放 `Bridge`。
    /// @param memory 接口所需的输入参数。
    /// @param alignment 内存对齐字节数，必须为有效的二次幂。
    static void TINY_PLUGIN_CALL DeallocateBridge(void *, void *memory, std::size_t alignment);

    LogCallback m_logCallback;
    std::vector<LoadedPlugin> m_plugins;
    std::string m_lastError;
};
} // 结束 MiniEngine 命名空间
