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
        uint32_t abi_version;
        uint32_t struct_size;
        void *host_context;
        void(TINY_PLUGIN_CALL *log)(void *host_context, TinyPluginLogLevel level, const char *message, size_t length);
        void *(TINY_PLUGIN_CALL *allocate)(void *host_context, size_t size, size_t alignment);
        void(TINY_PLUGIN_CALL *deallocate)(void *host_context, void *memory, size_t alignment);
    } TinyPluginHostAPI;

    typedef struct TinyPluginAPI
    {
        uint32_t abi_version;
        uint32_t struct_size;
        const char *name;
        uint32_t plugin_version;
        int(TINY_PLUGIN_CALL *startup)(const TinyPluginHostAPI *host);
        void(TINY_PLUGIN_CALL *shutdown)(void);
        void(TINY_PLUGIN_CALL *tick)(double delta_seconds);
    } TinyPluginAPI;

    typedef const TinyPluginAPI *(TINY_PLUGIN_CALL *TinyPluginGetAPIFn)(uint32_t requested_abi);
    TINY_PLUGIN_EXPORT const TinyPluginAPI *TINY_PLUGIN_CALL TinyPlugin_GetAPI(uint32_t requested_abi);

#ifdef __cplusplus
}
#endif
