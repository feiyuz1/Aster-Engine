#include "tinyengine/project_sdk/build_info.h"
#include "tinyengine/project_sdk/project_api.h"
#include <cstddef>
#include <cstring>

#ifndef TINY_TEST_MODE
#define TINY_TEST_MODE 0
#endif
namespace
{
TinyProjectString text(const char* s)
{
    return {s, static_cast<uint64_t>(std::strlen(s))};
}
struct Module
{
    const TinyProjectHostAPI* host;
};
struct Session
{
    Module* module;
    TinyProjectWorld world;
    TinyProjectEntity player{};
};
const TinyProjectHostAPI* lastHost = nullptr;
TinyProjectEntity lastPlayer{};
void Event(Module* m, const char* caption)
{
    m->host->log(m->host->host_context, 0, text(caption));
}
TinyProjectResult TINY_PROJECT_CALL load(const TinyProjectHostAPI* host, void** out)
{
    *out = new Module{host};
    Event(static_cast<Module*>(*out), "load");
    return TINY_PROJECT_OK;
}
void TINY_PROJECT_CALL Unload(void* p)
{
    auto* m = static_cast<Module*>(p);
    Event(m, "unload");
    delete m;
}
TinyProjectResult TINY_PROJECT_CALL Create(void* p, TinyProjectWorld world, void** out)
{
    auto* m = static_cast<Module*>(p);
    Event(m, "create");
    *out = new Session{m, world};
    return TINY_TEST_MODE == 9 ? TINY_PROJECT_FAILED : TINY_PROJECT_OK;
}
void TINY_PROJECT_CALL Destroy(void* p)
{
    auto* s = static_cast<Session*>(p);
    Event(s->module, "destroy");
    delete s;
}
TinyProjectResult TINY_PROJECT_CALL Begin(void* p)
{
    auto* s = static_cast<Session*>(p);
    auto* h = s->module->host;
    Event(s->module, "begin");
    if (h->find_entity)
        h->find_entity(h->host_context, s->world, text("Player"), &s->player);
    lastHost = h;
    lastPlayer = s->player;
    return TINY_TEST_MODE == 7 ? TINY_PROJECT_FAILED : TINY_PROJECT_OK;
}
TinyProjectResult TINY_PROJECT_CALL Update(void* p, float dt)
{
    auto* s = static_cast<Session*>(p);
    auto* h = s->module->host;
    Event(s->module, "update");
    TinyProjectTransform v{};
    if (h->get_transform && h->get_transform(h->host_context, s->player, &v) == TINY_PROJECT_OK)
    {
        v.rotation.y += 90 * dt;
        h->set_transform(h->host_context, s->player, &v);
    }
    return TINY_TEST_MODE == 8 ? TINY_PROJECT_FAILED : TINY_PROJECT_OK;
}
void TINY_PROJECT_CALL End(void* p)
{
    Event(static_cast<Session*>(p)->module, "end");
}
const TinyProjectAPI api{TINY_TEST_MODE == 1 ? 99u : TINY_PROJECT_ABI_VERSION,
                         TINY_TEST_MODE == 2   ? 8u
                         : TINY_TEST_MODE == 3 ? static_cast<uint32_t>(offsetof(TinyProjectAPI, fixed_update))
                         : TINY_TEST_MODE == 4 ? static_cast<uint32_t>(sizeof(TinyProjectAPI) + 16)
                                               : static_cast<uint32_t>(sizeof(TinyProjectAPI)),
                         {sizeof(TinyProjectModuleInfo), text("game.test"), text("p0-project"),
                          text(TINY_TEST_MODE == 5 ? "wrong-sdk" : TINYENGINE_PROJECT_SDK_BUILD_ID), text("build-1"),
                          text("source-1"), text(TINY_TEST_PROFILE)},
                         &load,
                         &Unload,
                         &Create,
                         &Destroy,
                         &Begin,
                         TINY_TEST_MODE == 6 ? nullptr : &Update,
                         &End,
                         nullptr,
                         nullptr};
struct Extended
{
    TinyProjectAPI api;
    uint64_t tail[2];
};
const Extended extended{api, {0, 0}};
} // namespace
extern "C" TINY_PROJECT_EXPORT const TinyProjectAPI* TINY_PROJECT_CALL TinyProject_GetAPI(uint32_t version)
{
    return version == TINY_PROJECT_ABI_VERSION ? &extended.api : nullptr;
}
extern "C" TINY_PROJECT_EXPORT uint32_t TINY_PROJECT_CALL TinyTest_PreviousPlayerAlive()
{
    uint32_t alive = 0;
    if (lastHost && lastHost->is_alive)
        lastHost->is_alive(lastHost->host_context, lastPlayer, &alive);
    return alive;
}
extern "C" TINY_PROJECT_EXPORT TinyProjectResult TINY_PROJECT_CALL TinyTest_FindWithPreviousWorld()
{
    TinyProjectEntity out{};
    return lastHost->find_entity(lastHost->host_context, {lastPlayer.world_id}, text("Player"), &out);
}
