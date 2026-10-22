/* Public project module ABI. Keep this header independent of C++ and engine headers. */
#pragma once

#include <stdint.h>

#if defined(_WIN32)
#define TINY_PROJECT_EXPORT __declspec(dllexport)
#define TINY_PROJECT_CALL __cdecl
#else
#define TINY_PROJECT_EXPORT __attribute__((visibility("default")))
#define TINY_PROJECT_CALL
#endif

#define TINY_PROJECT_ABI_VERSION 1u
#define TINY_PROJECT_ENTRY_POINT "TinyProject_GetAPI"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef int32_t TinyProjectResult;
#define TINY_PROJECT_OK 0
#define TINY_PROJECT_INVALID_ARGUMENT 1
#define TINY_PROJECT_NOT_FOUND 2
#define TINY_PROJECT_NOT_READY 3
#define TINY_PROJECT_INCOMPATIBLE 4
#define TINY_PROJECT_FAILED 5
#define TINY_PROJECT_UNSUPPORTED 6
#define TINY_PROJECT_WRONG_THREAD 7
#define TINY_PROJECT_READ_ONLY 8

#define TINY_PROJECT_LOG_INFO 0u
#define TINY_PROJECT_LOG_WARNING 1u
#define TINY_PROJECT_LOG_ERROR 2u

    /* Strings are borrowed UTF-8 spans. Empty spans may have a null data pointer. */
    typedef struct TinyProjectString
    {
        const char* data;
        uint64_t size;
    } TinyProjectString;
    typedef struct TinyProjectWorld
    {
        uint64_t id;
    } TinyProjectWorld;
    typedef struct TinyProjectEntity
    {
        uint64_t world_id;
        uint64_t value;
    } TinyProjectEntity;
    typedef struct TinyProjectVec3
    {
        float x;
        float y;
        float z;
    } TinyProjectVec3;
    /* Local transform; Euler rotation uses degrees, matching the engine. */
    typedef struct TinyProjectTransform
    {
        TinyProjectVec3 position;
        TinyProjectVec3 rotation;
        TinyProjectVec3 scale;
    } TinyProjectTransform;

    typedef struct TinyProjectModuleInfo
    {
        uint32_t struct_size;
        TinyProjectString module_id;
        TinyProjectString project_id;
        TinyProjectString sdk_build_id;
        TinyProjectString project_build_id;
        TinyProjectString source_fingerprint;
        TinyProjectString profile;
    } TinyProjectModuleInfo;

    /* The host owns this table until the module has unloaded. No callback may throw. */
    typedef struct TinyProjectHostAPI
    {
        uint32_t abi_version;
        uint32_t struct_size;
        void* host_context;
        void(TINY_PROJECT_CALL* log)(void*, uint32_t, TinyProjectString);
        TinyProjectResult(TINY_PROJECT_CALL* allocate)(void*, uint64_t, uint32_t, void**);
        void(TINY_PROJECT_CALL* deallocate)(void*, void*, uint32_t);
        TinyProjectResult(TINY_PROJECT_CALL* find_entity)(void*, TinyProjectWorld, TinyProjectString,
                                                          TinyProjectEntity*);
        TinyProjectResult(TINY_PROJECT_CALL* is_alive)(void*, TinyProjectEntity, uint32_t*);
        TinyProjectResult(TINY_PROJECT_CALL* get_transform)(void*, TinyProjectEntity, TinyProjectTransform*);
        TinyProjectResult(TINY_PROJECT_CALL* set_transform)(void*, TinyProjectEntity, const TinyProjectTransform*);
        TinyProjectResult(TINY_PROJECT_CALL* get_axis)(void*, TinyProjectString, float*);
        TinyProjectResult(TINY_PROJECT_CALL* is_down)(void*, TinyProjectString, uint32_t*);
        TinyProjectResult(TINY_PROJECT_CALL* query_interface)(void*, TinyProjectString, uint32_t, const void**);
    } TinyProjectHostAPI;

    /* Objects and this table are module-owned; only the module destroys its objects. */
    typedef struct TinyProjectAPI
    {
        uint32_t abi_version;
        uint32_t struct_size;
        TinyProjectModuleInfo info;
        TinyProjectResult(TINY_PROJECT_CALL* load)(const TinyProjectHostAPI*, void**);
        void(TINY_PROJECT_CALL* unload)(void*);
        TinyProjectResult(TINY_PROJECT_CALL* create_session)(void*, TinyProjectWorld, void**);
        void(TINY_PROJECT_CALL* destroy_session)(void*);
        TinyProjectResult(TINY_PROJECT_CALL* begin_play)(void*);
        TinyProjectResult(TINY_PROJECT_CALL* update)(void*, float);
        void(TINY_PROJECT_CALL* end_play)(void*);
        /* Optional tail. Check struct_size before reading these members. */
        TinyProjectResult(TINY_PROJECT_CALL* fixed_update)(void*, float);
        TinyProjectResult(TINY_PROJECT_CALL* late_update)(void*, float);
    } TinyProjectAPI;

    typedef const TinyProjectAPI*(TINY_PROJECT_CALL* TinyProjectGetAPIFn)(uint32_t requested_abi);

#ifdef __cplusplus
}
#endif
