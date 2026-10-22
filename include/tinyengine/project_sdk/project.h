#pragma once
#include "tinyengine/project_sdk/project_api.h"
#include <exception>
#include <string_view>
#include <type_traits>

namespace tinyengine::project_sdk
{
class Context
{
  public:
    Context(const TinyProjectHostAPI& host, TinyProjectWorld world) noexcept : m_host(host), m_world(world) {}
    TinyProjectEntity findEntity(std::string_view);
    bool isAlive(TinyProjectEntity);
    bool getTransform(TinyProjectEntity, TinyProjectTransform&);
    bool setTransform(TinyProjectEntity, const TinyProjectTransform&);
    float getAxis(std::string_view);
    bool isDown(std::string_view);
    void log(std::string_view) const noexcept;
    TinyProjectResult getLastResult() const noexcept
    {
        return m_result;
    }

  private:
    const TinyProjectHostAPI& m_host;
    TinyProjectWorld m_world;
    TinyProjectResult m_result = TINY_PROJECT_OK;
};
class Session
{
  public:
    virtual ~Session() = default;
    virtual void onBeginPlay(Context&) {}
    virtual void onUpdate(Context&, float) {}
    virtual void onFixedUpdate(Context&, float) {}
    virtual void onLateUpdate(Context&, float) {}
    virtual void onEndPlay(Context&) {}
};
namespace detail
{
template <class T> struct Adapter
{
    struct Module
    {
        const TinyProjectHostAPI* host;
    };
    struct Instance
    {
        Context context;
        T session;
        Instance(Module& m, TinyProjectWorld w) : context(*m.host, w) {}
    };
    template <class F> static TinyProjectResult invoke(void* p, F&& f) noexcept
    {
        if (!p)
            return TINY_PROJECT_INVALID_ARGUMENT;
        auto& i = *static_cast<Instance*>(p);
        try
        {
            f(i.session, i.context);
            return TINY_PROJECT_OK;
        }
        catch (const std::exception& e)
        {
            i.context.log("Project session exception");
            i.context.log(e.what());
        }
        catch (...)
        {
            i.context.log("Unknown project session exception");
        }
        return TINY_PROJECT_FAILED;
    }
    static TinyProjectResult TINY_PROJECT_CALL load(const TinyProjectHostAPI* host, void** out) noexcept
    {
        if (out)
            *out = nullptr;
        if (!out || !host || host->abi_version != TINY_PROJECT_ABI_VERSION ||
            host->struct_size < sizeof(TinyProjectHostAPI))
            return TINY_PROJECT_INCOMPATIBLE;
        try
        {
            *out = new Module{host};
            return TINY_PROJECT_OK;
        }
        catch (...)
        {
            return TINY_PROJECT_FAILED;
        }
    }
    static void TINY_PROJECT_CALL Unload(void* p) noexcept
    {
        delete static_cast<Module*>(p);
    }
    static TinyProjectResult TINY_PROJECT_CALL Create(void* p, TinyProjectWorld world, void** out) noexcept
    {
        if (out)
            *out = nullptr;
        if (!p || !out || !world.id)
            return TINY_PROJECT_INVALID_ARGUMENT;
        try
        {
            *out = new Instance(*static_cast<Module*>(p), world);
            return TINY_PROJECT_OK;
        }
        catch (...)
        {
            return TINY_PROJECT_FAILED;
        }
    }
    static void TINY_PROJECT_CALL Destroy(void* p) noexcept
    {
        delete static_cast<Instance*>(p);
    }
    static TinyProjectResult TINY_PROJECT_CALL Begin(void* p) noexcept
    {
        return invoke(p, [](T& s, Context& c) { s.onBeginPlay(c); });
    }
    static TinyProjectResult TINY_PROJECT_CALL Update(void* p, float dt) noexcept
    {
        return invoke(p, [dt](T& s, Context& c) { s.onUpdate(c, dt); });
    }
    static TinyProjectResult TINY_PROJECT_CALL Fixed(void* p, float dt) noexcept
    {
        return invoke(p, [dt](T& s, Context& c) { s.onFixedUpdate(c, dt); });
    }
    static TinyProjectResult TINY_PROJECT_CALL Late(void* p, float dt) noexcept
    {
        return invoke(p, [dt](T& s, Context& c) { s.onLateUpdate(c, dt); });
    }
    static void TINY_PROJECT_CALL End(void* p) noexcept
    {
        (void)invoke(p, [](T& s, Context& c) { s.onEndPlay(c); });
    }
};
} // namespace detail
template <class TSession>
const TinyProjectAPI* ExportProject(uint32_t requestedAbi, const TinyProjectModuleInfo& info) noexcept
{
    static_assert(std::is_base_of<Session, TSession>::value, "Project session must derive from SDK Session");
    if (requestedAbi != TINY_PROJECT_ABI_VERSION)
        return nullptr;
    using A = detail::Adapter<TSession>;
    static const TinyProjectAPI api{TINY_PROJECT_ABI_VERSION,
                                    sizeof(TinyProjectAPI),
                                    info,
                                    &A::load,
                                    &A::Unload,
                                    &A::Create,
                                    &A::Destroy,
                                    &A::Begin,
                                    &A::Update,
                                    &A::End,
                                    &A::Fixed,
                                    &A::Late};
    return &api;
}
} // namespace tinyengine::project_sdk
