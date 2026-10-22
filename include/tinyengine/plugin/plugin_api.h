// 文件：include/tinyengine/plugin/plugin_api.h
// 作用：声明供外部动态库使用的 C ABI，避免跨编译器暴露 C++ 对象布局。
// 约定：跨动态库边界仅使用版本化 C ABI，所有权由接口文档约定。
// 类型：公开声明。

#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define TINY_PLUGIN_EXPORT __declspec(dllexport)
#define TINY_PLUGIN_CALL __cdecl
#else
#define TINY_PLUGIN_EXPORT __attribute__((visibility("default")))
#define TINY_PLUGIN_CALL
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define TINY_PLUGIN_ABI_VERSION 1u
#define TINY_PLUGIN_ENTRY_POINT "TinyPlugin_GetAPI"

    typedef enum TinyPluginLogLevel
    {
        TINY_PLUGIN_LOG_INFO = 0,
        TINY_PLUGIN_LOG_WARNING = 1,
        TINY_PLUGIN_LOG_ERROR = 2
    } TinyPluginLogLevel;

    typedef struct TinyPluginHostAPI
    {
        /* 主程序支持的 ABI 版本，插件必须在使用后续字段前核对该值。 */
        uint32_t abi_version;
        /* 当前结构体的字节数，用于兼容未来在末尾追加的字段。 */
        uint32_t struct_size;
        /* 由主程序传回桥接函数的透明上下文，插件不得释放或解释。 */
        void* host_context;
        /** 向主程序日志系统写入一条指定长度的 UTF-8 消息；message 只在调用期间有效。 */
        void(TINY_PLUGIN_CALL* log)(void* host_context, TinyPluginLogLevel level, const char* message, size_t length);
        /** 使用主程序分配器申请内存；返回的内存必须通过同一 Host API 的 deallocate 释放。 */
        void*(TINY_PLUGIN_CALL* allocate)(void* host_context, size_t size, size_t alignment);
        /** 释放 allocate 返回的内存；alignment 必须与申请时一致。 */
        void(TINY_PLUGIN_CALL* deallocate)(void* host_context, void* memory, size_t alignment);
    } TinyPluginHostAPI;

    typedef struct TinyPluginAPI
    {
        /* 插件实现的 ABI 版本。 */
        uint32_t abi_version;
        /* 当前结构体的字节数。 */
        uint32_t struct_size;
        /* 以空字符结尾、由插件静态持有的 UTF-8 名称。 */
        const char* name;
        /* 由插件自行定义的功能版本号。 */
        uint32_t plugin_version;
        /** 启动插件；成功返回非零值，失败返回零且不得保留 Host API 之外的临时指针。 */
        int(TINY_PLUGIN_CALL* startup)(const TinyPluginHostAPI* host);
        /** 停止插件并释放 startup 创建的全部资源。 */
        void(TINY_PLUGIN_CALL* shutdown)(void);
        /** 推进一帧插件逻辑；delta_seconds 是未缩放的帧时间。 */
        void(TINY_PLUGIN_CALL* tick)(double delta_seconds);
    } TinyPluginAPI;

    /** 插件入口函数指针类型；requested_abi 不受支持时必须返回空指针。 */
    typedef const TinyPluginAPI*(TINY_PLUGIN_CALL* TinyPluginGetAPIFn)(uint32_t requested_abi);
    /** 查询插件 API；返回对象由插件静态持有，主程序不得释放。 */
    TINY_PLUGIN_EXPORT const TinyPluginAPI* TINY_PLUGIN_CALL TinyPlugin_GetAPI(uint32_t requested_abi);

#ifdef __cplusplus
}
#endif
