#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/Script/ScriptEngine.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>

using namespace MiniEngine;

namespace
{
void Check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

struct Probe final : ScriptInstance
{
    std::function<void(Probe &)> create, update, destroy;
    int *destructed = nullptr;
    ~Probe() override
    {
        if (destructed)
            ++*destructed;
    }
    void OnCreate() override
    {
        if (create)
            create(*this);
    }
    void OnUpdate(float) override
    {
        if (update)
            update(*this);
    }
    void OnDestroy() override
    {
        if (destroy)
            destroy(*this);
    }
};

struct Listener final : WorldListener
{
    ScriptEngine *scripts = nullptr;
    void OnEntityDestroying(World &, Entity entity) override
    {
        scripts->DestroyEntity(entity);
    }
};

void SelfDestructionWaitsForCallback()
{
    World world;
    ScriptEngine scripts;
    Listener listener;
    listener.scripts = &scripts;
    world.AddListener(&listener);
    int destructed = 0, destroyed = 0, completed = 0;
    scripts.RegisterFactory("self", [&] {
        auto script = std::make_unique<Probe>();
        script->destructed = &destructed;
        script->update = [&](Probe &self) {
            self.GetWorld()->DestroyEntity(self.GetEntity());
            Check(destructed == 0 && destroyed == 0, "destruction ran inside OnUpdate");
            ++completed;
        };
        script->destroy = [&](Probe &) { ++destroyed; };
        return script;
    });
    scripts.CreateInstance("self", world.CreateEntity(), &world);
    scripts.UpdateAll(1);
    Check(completed == 1 && destroyed == 1 && destructed == 1, "self destruction lifecycle counts");
    Check(scripts.GetInstanceCount() == 0, "self script remained alive");
    world.RemoveListener(&listener);
}

void CreateCanDestroyItself()
{
    ScriptEngine scripts;
    int destructed = 0, destroyed = 0;
    scripts.RegisterFactory("self", [&] {
        auto script = std::make_unique<Probe>();
        script->destructed = &destructed;
        script->create = [&](Probe &self) {
            scripts.DestroyInstance(&self);
            Check(destructed == 0, "OnCreate object deleted before return");
        };
        script->destroy = [&](Probe &) { ++destroyed; };
        return script;
    });
    Check(scripts.CreateInstance("self") == nullptr, "OnCreate returned a dangling script");
    Check(destroyed == 1 && destructed == 1, "OnCreate destruction counts");
}

void MutationsDoNotInvalidateFrame()
{
    ScriptEngine scripts;
    ScriptInstance *victim = nullptr;
    int removedUpdates = 0, addedUpdates = 0, survivorUpdates = 0, destroyed = 0;
    bool mutated = false;
    scripts.RegisterFactory("added", [&] {
        auto script = std::make_unique<Probe>();
        script->update = [&](Probe &) { ++addedUpdates; };
        return script;
    });
    scripts.RegisterFactory("mutator", [&] {
        auto script = std::make_unique<Probe>();
        script->update = [&](Probe &) {
            if (mutated)
                return;
            mutated = true;
            scripts.DestroyInstance(victim);
            for (int i = 0; i < 100; ++i)
                scripts.CreateInstance("added");
        };
        return script;
    });
    scripts.RegisterFactory("victim", [&] {
        auto script = std::make_unique<Probe>();
        script->update = [&](Probe &) { ++removedUpdates; };
        script->destroy = [&](Probe &self) {
            ++destroyed;
            scripts.DestroyInstance(&self);
        };
        return script;
    });
    scripts.RegisterFactory("survivor", [&] {
        auto script = std::make_unique<Probe>();
        script->update = [&](Probe &) { ++survivorUpdates; };
        return script;
    });
    scripts.CreateInstance("mutator");
    victim = scripts.CreateInstance("victim");
    scripts.CreateInstance("survivor");
    scripts.UpdateAll(1);
    Check(removedUpdates == 0 && addedUpdates == 0 && survivorUpdates == 1 && destroyed == 1,
          "frame must skip removed scripts and defer newly added updates");
    scripts.UpdateAll(1);
    Check(addedUpdates == 100 && survivorUpdates == 2, "next frame skipped scripts");
    scripts.Shutdown();
}

void ReloadFromUpdateWaitsForReturn()
{
    ScriptEngine scripts;
    int destructed = 0, created = 0, destroyed = 0;
    bool reload = true;
    scripts.RegisterFactory("reload", [&] {
        auto script = std::make_unique<Probe>();
        script->destructed = &destructed;
        script->create = [&](Probe &) { ++created; };
        script->destroy = [&](Probe &) { ++destroyed; };
        script->update = [&](Probe &) {
            if (!reload)
                return;
            reload = false;
            Check(scripts.ReloadScript("reload"), "reload rejected live script");
            Check(destructed == 0 && destroyed == 0, "reload destroyed active callback");
        };
        return script;
    });
    scripts.CreateInstance("reload");
    scripts.UpdateAll(1);
    Check(created == 2 && destroyed == 1 && destructed == 1, "reload lifecycle counts");
    scripts.Shutdown();
    Check(destroyed == 2 && destructed == 2, "shutdown lifecycle counts");
}

void ReloadDestructionCanClearInstances()
{
    ScriptEngine scripts;
    int created = 0, destroyed = 0;
    scripts.RegisterFactory("clear", [&] {
        auto script = std::make_unique<Probe>();
        script->create = [&](Probe &) { ++created; };
        script->destroy = [&](Probe &) {
            ++destroyed;
            scripts.ClearInstances();
        };
        return script;
    });
    scripts.CreateInstance("clear");
    scripts.ReloadScript("clear");
    Check(created == 1 && destroyed == 1 && scripts.GetInstanceCount() == 0, "reload resurrected a script removed by OnDestroy");
}

void ClearIsSafeDuringUpdateAndDestruction()
{
    ScriptEngine scripts;
    int destroyed = 0, destructed = 0, updates = 0;
    scripts.RegisterFactory("clear", [&] {
        auto script = std::make_unique<Probe>();
        script->destructed = &destructed;
        script->update = [&](Probe &) {
            ++updates;
            scripts.ClearInstances();
            Check(destructed == 0, "clear deleted an executing callback");
        };
        script->destroy = [&](Probe &) {
            ++destroyed;
            scripts.ClearInstances();
            Check(scripts.CreateInstance("clear") == nullptr, "clear permitted resurrection");
        };
        return script;
    });
    scripts.CreateInstance("clear");
    scripts.CreateInstance("clear");
    scripts.UpdateAll(1);
    Check(updates == 1 && destroyed == 2 && destructed == 2 && scripts.GetInstanceCount() == 0,
          "clear duplicated callbacks or left scripts alive");
}

void RebuildAllowsOnCreateToRemoveEntities()
{
    World world;
    ScriptEngine scripts;
    int created = 0;
    scripts.RegisterFactory("remove", [&] {
        auto script = std::make_unique<Probe>();
        script->create = [&](Probe &self) {
            ++created;
            world.DestroyEntity(self.GetEntity());
        };
        return script;
    });
    for (int i = 0; i < 20; ++i)
        world.AddComponent(world.CreateEntity(), ScriptComponent{"remove"});
    scripts.RebuildWorldInstances(world);
    Check(created == 20 && scripts.GetInstanceCount() == 0, "rebuild lost traversal after entity removal");
}

void ReloadCreateCanDestroyReplacement()
{
    ScriptEngine scripts;
    int created = 0, destroyed = 0, destructed = 0;
    scripts.RegisterFactory("reload", [&] {
        auto script = std::make_unique<Probe>();
        script->destructed = &destructed;
        script->create = [&](Probe &self) {
            if (++created == 2)
            {
                scripts.DestroyInstance(&self);
                Check(destructed == 1, "replacement was deleted inside OnCreate");
            }
        };
        script->destroy = [&](Probe &) { ++destroyed; };
        return script;
    });
    scripts.CreateInstance("reload");
    scripts.ReloadScript("reload");
    Check(created == 2 && destroyed == 2 && destructed == 2 && scripts.GetInstanceCount() == 0, "replacement destruction lifecycle counts");
}

void RecursiveReloadIsCoalesced()
{
    ScriptEngine scripts;
    int created = 0, destroyed = 0;
    bool recursive = false;
    scripts.RegisterFactory("recursive", [&] {
        auto script = std::make_unique<Probe>();
        script->create = [&](Probe &) {
            ++created;
            if (recursive)
                Check(!scripts.ReloadScript("recursive"), "OnCreate requeued an in-progress reload");
        };
        script->destroy = [&](Probe &) {
            ++destroyed;
            if (recursive)
                Check(!scripts.ReloadScript("recursive"), "OnDestroy requeued an in-progress reload");
        };
        return script;
    });
    scripts.CreateInstance("recursive");
    recursive = true;
    Check(scripts.ReloadScript("recursive"), "initial reload was not accepted");
    Check(created == 2 && destroyed == 1, "recursive reload was not coalesced");
    recursive = false;
    Check(scripts.ReloadScript("recursive"), "later independent reload was suppressed");
    Check(created == 3 && destroyed == 2, "later reload lifecycle counts");
    scripts.Shutdown();
}
} // namespace

int main()
{
    try
    {
        SelfDestructionWaitsForCallback();
        CreateCanDestroyItself();
        MutationsDoNotInvalidateFrame();
        ReloadFromUpdateWaitsForReturn();
        ReloadDestructionCanClearInstances();
        ClearIsSafeDuringUpdateAndDestruction();
        RebuildAllowsOnCreateToRemoveEntities();
        ReloadCreateCanDestroyReplacement();
        RecursiveReloadIsCoalesced();
        std::cout << "Script lifecycle tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
