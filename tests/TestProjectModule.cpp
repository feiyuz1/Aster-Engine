#include "MiniEngine/ProjectSDK/ProjectAPI.h"
#include "MiniEngine/ProjectSDK/BuildInfo.h"
#include <cstddef>
#include <cstring>

#ifndef TINY_TEST_MODE
#define TINY_TEST_MODE 0
#endif
namespace
{
TinyProjectString Text(const char *s) { return {s, static_cast<uint64_t>(std::strlen(s))}; }
struct Module { const TinyProjectHostAPI *host; };
struct Session { Module *module; };
void Event(Module *m, const char *text) { m->host->log(m->host->host_context, 0, Text(text)); }
TinyProjectResult TINY_PROJECT_CALL Load(const TinyProjectHostAPI *host, void **out)
{
    *out = new Module{host}; Event(static_cast<Module *>(*out), "load"); return TINY_PROJECT_OK;
}
void TINY_PROJECT_CALL Unload(void *p) { auto *m = static_cast<Module *>(p); Event(m, "unload"); delete m; }
TinyProjectResult TINY_PROJECT_CALL Create(void *p, TinyProjectWorld, void **out)
{
    auto *m = static_cast<Module *>(p); Event(m, "create"); *out = new Session{m}; return TINY_PROJECT_OK;
}
void TINY_PROJECT_CALL Destroy(void *p) { auto *s = static_cast<Session *>(p); Event(s->module, "destroy"); delete s; }
TinyProjectResult TINY_PROJECT_CALL Begin(void *p) { Event(static_cast<Session *>(p)->module, "begin"); return TINY_PROJECT_OK; }
TinyProjectResult TINY_PROJECT_CALL Update(void *p, float) { Event(static_cast<Session *>(p)->module, "update"); return TINY_PROJECT_OK; }
void TINY_PROJECT_CALL End(void *p) { Event(static_cast<Session *>(p)->module, "end"); }
const TinyProjectAPI api{
    TINY_TEST_MODE == 1 ? 99u : TINY_PROJECT_ABI_VERSION,
    TINY_TEST_MODE == 2 ? 8u : TINY_TEST_MODE == 3 ? static_cast<uint32_t>(offsetof(TinyProjectAPI, fixed_update)) :
    TINY_TEST_MODE == 4 ? static_cast<uint32_t>(sizeof(TinyProjectAPI) + 16) : static_cast<uint32_t>(sizeof(TinyProjectAPI)),
    {sizeof(TinyProjectModuleInfo), Text("game.test"), Text("p0-project"),
     Text(TINY_TEST_MODE == 5 ? "wrong-sdk" : TINYENGINE_PROJECT_SDK_BUILD_ID), Text("build-1"), Text("source-1"),
     Text(TINY_TEST_PROFILE)},
    &Load, &Unload, &Create, &Destroy, &Begin, TINY_TEST_MODE == 6 ? nullptr : &Update, &End, nullptr, nullptr};
struct Extended { TinyProjectAPI api; uint64_t tail[2]; };
const Extended extended{api, {0,0}};
}
extern "C" TINY_PROJECT_EXPORT const TinyProjectAPI *TINY_PROJECT_CALL TinyProject_GetAPI(uint32_t version)
{
    return version == TINY_PROJECT_ABI_VERSION ? &extended.api : nullptr;
}
