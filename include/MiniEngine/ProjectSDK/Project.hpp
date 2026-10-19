#pragma once
#include "MiniEngine/ProjectSDK/ProjectAPI.h"
#include <exception>
#include <string_view>
#include <type_traits>

namespace MiniEngine::ProjectSDK
{
class Context
{
public:
    Context(const TinyProjectHostAPI &host, TinyProjectWorld world) noexcept : m_host(host), m_world(world) {}
    TinyProjectEntity FindEntity(std::string_view);
    bool IsAlive(TinyProjectEntity);
    bool GetTransform(TinyProjectEntity, TinyProjectTransform &);
    bool SetTransform(TinyProjectEntity, const TinyProjectTransform &);
    float GetAxis(std::string_view);
    bool IsDown(std::string_view);
    void Log(std::string_view) const noexcept;
    TinyProjectResult GetLastResult() const noexcept { return m_result; }
private:
    const TinyProjectHostAPI &m_host;
    TinyProjectWorld m_world;
    TinyProjectResult m_result = TINY_PROJECT_OK;
};
class Session
{
public:
    virtual ~Session() = default;
    virtual void OnBeginPlay(Context &) {}
    virtual void OnUpdate(Context &, float) {}
    virtual void OnFixedUpdate(Context &, float) {}
    virtual void OnLateUpdate(Context &, float) {}
    virtual void OnEndPlay(Context &) {}
};
namespace Detail
{
template<class T> struct Adapter
{
    struct Module { const TinyProjectHostAPI *host; };
    struct Instance { Context context; T session; Instance(Module &m, TinyProjectWorld w) : context(*m.host, w) {} };
    template<class F> static TinyProjectResult Invoke(void *p, F &&f) noexcept
    {
        if (!p) return TINY_PROJECT_INVALID_ARGUMENT;
        auto &i = *static_cast<Instance *>(p);
        try { f(i.session, i.context); return TINY_PROJECT_OK; }
        catch (const std::exception &e) { i.context.Log("Project session exception"); i.context.Log(e.what()); }
        catch (...) { i.context.Log("Unknown project session exception"); }
        return TINY_PROJECT_FAILED;
    }
    static TinyProjectResult TINY_PROJECT_CALL Load(const TinyProjectHostAPI *host, void **out) noexcept
    {
        if (out) *out = nullptr;
        if (!out || !host || host->abi_version != TINY_PROJECT_ABI_VERSION || host->struct_size < sizeof(TinyProjectHostAPI)) return TINY_PROJECT_INCOMPATIBLE;
        try { *out = new Module{host}; return TINY_PROJECT_OK; } catch (...) { return TINY_PROJECT_FAILED; }
    }
    static void TINY_PROJECT_CALL Unload(void *p) noexcept { delete static_cast<Module *>(p); }
    static TinyProjectResult TINY_PROJECT_CALL Create(void *p, TinyProjectWorld world, void **out) noexcept
    {
        if (out) *out = nullptr;
        if (!p || !out || !world.id) return TINY_PROJECT_INVALID_ARGUMENT;
        try { *out = new Instance(*static_cast<Module *>(p), world); return TINY_PROJECT_OK; }
        catch (...) { return TINY_PROJECT_FAILED; }
    }
    static void TINY_PROJECT_CALL Destroy(void *p) noexcept { delete static_cast<Instance *>(p); }
    static TinyProjectResult TINY_PROJECT_CALL Begin(void *p) noexcept { return Invoke(p, [](T &s, Context &c) { s.OnBeginPlay(c); }); }
    static TinyProjectResult TINY_PROJECT_CALL Update(void *p, float dt) noexcept { return Invoke(p, [dt](T &s, Context &c) { s.OnUpdate(c,dt); }); }
    static TinyProjectResult TINY_PROJECT_CALL Fixed(void *p, float dt) noexcept { return Invoke(p, [dt](T &s, Context &c) { s.OnFixedUpdate(c,dt); }); }
    static TinyProjectResult TINY_PROJECT_CALL Late(void *p, float dt) noexcept { return Invoke(p, [dt](T &s, Context &c) { s.OnLateUpdate(c,dt); }); }
    static void TINY_PROJECT_CALL End(void *p) noexcept { (void)Invoke(p, [](T &s, Context &c) { s.OnEndPlay(c); }); }
};
}
template<class TSession> const TinyProjectAPI *ExportProject(uint32_t requestedAbi, const TinyProjectModuleInfo &info) noexcept
{
    static_assert(std::is_base_of<Session,TSession>::value, "Project session must derive from SDK Session");
    if (requestedAbi != TINY_PROJECT_ABI_VERSION) return nullptr;
    using A = Detail::Adapter<TSession>;
    static const TinyProjectAPI api{TINY_PROJECT_ABI_VERSION, sizeof(TinyProjectAPI), info,
        &A::Load,&A::Unload,&A::Create,&A::Destroy,&A::Begin,&A::Update,&A::End,&A::Fixed,&A::Late};
    return &api;
}
}
