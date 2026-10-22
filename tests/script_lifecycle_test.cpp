#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/script/script_engine.h"

#include <functional>
#include <iostream>
#include <stdexcept>

using namespace tinyengine;

namespace
{
void check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

struct Probe final : ScriptInstance
{
    std::function<void(Probe&)> create, update, destroy;
    int* destructed = nullptr;
    ~Probe() override
    {
        if (destructed)
            ++*destructed;
    }
    void onCreate() override
    {
        if (create)
            create(*this);
    }
    void onUpdate(float) override
    {
        if (update)
            update(*this);
    }
    void onDestroy() override
    {
        if (destroy)
            destroy(*this);
    }
};

struct Listener final : WorldListener
{
    ScriptEngine* scripts = nullptr;
    void onEntityDestroying(World&, Entity entity) override
    {
        scripts->destroyEntity(entity);
    }
};

void selfDestructionWaitsForCallback()
{
    World world;
    ScriptEngine scripts;
    Listener listener;
    listener.scripts = &scripts;
    world.addListener(&listener);
    int destructed = 0, destroyed = 0, completed = 0;
    scripts.registerFactory("self",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->destructed = &destructed;
                                script->update = [&](Probe& self)
                                {
                                    self.getWorld()->destroyEntity(self.getEntity());
                                    check(destructed == 0 && destroyed == 0, "destruction ran inside OnUpdate");
                                    ++completed;
                                };
                                script->destroy = [&](Probe&) { ++destroyed; };
                                return script;
                            });
    scripts.createInstance("self", world.createEntity(), &world);
    scripts.updateAll(1);
    check(completed == 1 && destroyed == 1 && destructed == 1, "self destruction lifecycle counts");
    check(scripts.getInstanceCount() == 0, "self script remained alive");
    world.removeListener(&listener);
}

void createCanDestroyItself()
{
    ScriptEngine scripts;
    int destructed = 0, destroyed = 0;
    scripts.registerFactory("self",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->destructed = &destructed;
                                script->create = [&](Probe& self)
                                {
                                    scripts.destroyInstance(&self);
                                    check(destructed == 0, "OnCreate object deleted before return");
                                };
                                script->destroy = [&](Probe&) { ++destroyed; };
                                return script;
                            });
    check(scripts.createInstance("self") == nullptr, "OnCreate returned a dangling script");
    check(destroyed == 1 && destructed == 1, "OnCreate destruction counts");
}

void mutationsDoNotInvalidateFrame()
{
    ScriptEngine scripts;
    ScriptInstance* victim = nullptr;
    int removedUpdates = 0, addedUpdates = 0, survivorUpdates = 0, destroyed = 0;
    bool mutated = false;
    scripts.registerFactory("added",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->update = [&](Probe&) { ++addedUpdates; };
                                return script;
                            });
    scripts.registerFactory("mutator",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->update = [&](Probe&)
                                {
                                    if (mutated)
                                        return;
                                    mutated = true;
                                    scripts.destroyInstance(victim);
                                    for (int i = 0; i < 100; ++i)
                                        scripts.createInstance("added");
                                };
                                return script;
                            });
    scripts.registerFactory("victim",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->update = [&](Probe&) { ++removedUpdates; };
                                script->destroy = [&](Probe& self)
                                {
                                    ++destroyed;
                                    scripts.destroyInstance(&self);
                                };
                                return script;
                            });
    scripts.registerFactory("survivor",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->update = [&](Probe&) { ++survivorUpdates; };
                                return script;
                            });
    scripts.createInstance("mutator");
    victim = scripts.createInstance("victim");
    scripts.createInstance("survivor");
    scripts.updateAll(1);
    check(removedUpdates == 0 && addedUpdates == 0 && survivorUpdates == 1 && destroyed == 1,
          "frame must skip removed scripts and defer newly added updates");
    scripts.updateAll(1);
    check(addedUpdates == 100 && survivorUpdates == 2, "next frame skipped scripts");
    scripts.shutdown();
}

void reloadFromUpdateWaitsForReturn()
{
    ScriptEngine scripts;
    int destructed = 0, created = 0, destroyed = 0;
    bool reload = true;
    scripts.registerFactory("reload",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->destructed = &destructed;
                                script->create = [&](Probe&) { ++created; };
                                script->destroy = [&](Probe&) { ++destroyed; };
                                script->update = [&](Probe&)
                                {
                                    if (!reload)
                                        return;
                                    reload = false;
                                    check(scripts.reloadScript("reload"), "reload rejected live script");
                                    check(destructed == 0 && destroyed == 0, "reload destroyed active callback");
                                };
                                return script;
                            });
    scripts.createInstance("reload");
    scripts.updateAll(1);
    check(created == 2 && destroyed == 1 && destructed == 1, "reload lifecycle counts");
    scripts.shutdown();
    check(destroyed == 2 && destructed == 2, "shutdown lifecycle counts");
}

void reloadDestructionCanClearInstances()
{
    ScriptEngine scripts;
    int created = 0, destroyed = 0;
    scripts.registerFactory("clear",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->create = [&](Probe&) { ++created; };
                                script->destroy = [&](Probe&)
                                {
                                    ++destroyed;
                                    scripts.clearInstances();
                                };
                                return script;
                            });
    scripts.createInstance("clear");
    scripts.reloadScript("clear");
    check(created == 1 && destroyed == 1 && scripts.getInstanceCount() == 0,
          "reload resurrected a script removed by OnDestroy");
}

void clearIsSafeDuringUpdateAndDestruction()
{
    ScriptEngine scripts;
    int destroyed = 0, destructed = 0, updates = 0;
    scripts.registerFactory("clear",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->destructed = &destructed;
                                script->update = [&](Probe&)
                                {
                                    ++updates;
                                    scripts.clearInstances();
                                    check(destructed == 0, "clear deleted an executing callback");
                                };
                                script->destroy = [&](Probe&)
                                {
                                    ++destroyed;
                                    scripts.clearInstances();
                                    check(scripts.createInstance("clear") == nullptr, "clear permitted resurrection");
                                };
                                return script;
                            });
    scripts.createInstance("clear");
    scripts.createInstance("clear");
    scripts.updateAll(1);
    check(updates == 1 && destroyed == 2 && destructed == 2 && scripts.getInstanceCount() == 0,
          "clear duplicated callbacks or left scripts alive");
}

void rebuildAllowsOnCreateToRemoveEntities()
{
    World world;
    ScriptEngine scripts;
    int created = 0;
    scripts.registerFactory("remove",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->create = [&](Probe& self)
                                {
                                    ++created;
                                    world.destroyEntity(self.getEntity());
                                };
                                return script;
                            });
    for (int i = 0; i < 20; ++i)
        world.AddComponent(world.createEntity(), ScriptComponent{"remove"});
    scripts.rebuildWorldInstances(world);
    check(created == 20 && scripts.getInstanceCount() == 0, "rebuild lost traversal after entity removal");
}

void reloadCreateCanDestroyReplacement()
{
    ScriptEngine scripts;
    int created = 0, destroyed = 0, destructed = 0;
    scripts.registerFactory("reload",
                            [&]
                            {
                                auto script = std::make_unique<Probe>();
                                script->destructed = &destructed;
                                script->create = [&](Probe& self)
                                {
                                    if (++created == 2)
                                    {
                                        scripts.destroyInstance(&self);
                                        check(destructed == 1, "replacement was deleted inside OnCreate");
                                    }
                                };
                                script->destroy = [&](Probe&) { ++destroyed; };
                                return script;
                            });
    scripts.createInstance("reload");
    scripts.reloadScript("reload");
    check(created == 2 && destroyed == 2 && destructed == 2 && scripts.getInstanceCount() == 0,
          "replacement destruction lifecycle counts");
}

void recursiveReloadIsCoalesced()
{
    ScriptEngine scripts;
    int created = 0, destroyed = 0;
    bool recursive = false;
    scripts.registerFactory(
        "recursive",
        [&]
        {
            auto script = std::make_unique<Probe>();
            script->create = [&](Probe&)
            {
                ++created;
                if (recursive)
                    check(!scripts.reloadScript("recursive"), "OnCreate requeued an in-progress reload");
            };
            script->destroy = [&](Probe&)
            {
                ++destroyed;
                if (recursive)
                    check(!scripts.reloadScript("recursive"), "OnDestroy requeued an in-progress reload");
            };
            return script;
        });
    scripts.createInstance("recursive");
    recursive = true;
    check(scripts.reloadScript("recursive"), "initial reload was not accepted");
    check(created == 2 && destroyed == 1, "recursive reload was not coalesced");
    recursive = false;
    check(scripts.reloadScript("recursive"), "later independent reload was suppressed");
    check(created == 3 && destroyed == 2, "later reload lifecycle counts");
    scripts.shutdown();
}
} // namespace

int main()
{
    try
    {
        selfDestructionWaitsForCallback();
        createCanDestroyItself();
        mutationsDoNotInvalidateFrame();
        reloadFromUpdateWaitsForReturn();
        reloadDestructionCanClearInstances();
        clearIsSafeDuringUpdateAndDestruction();
        rebuildAllowsOnCreateToRemoveEntities();
        reloadCreateCanDestroyReplacement();
        recursiveReloadIsCoalesced();
        std::cout << "Script lifecycle tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
