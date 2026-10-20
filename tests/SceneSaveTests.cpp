// 文件：tests/SceneSaveTests.cpp
// 作用：快速验证场景首次保存所依赖的原子写入、备份和失败保护。
// 约定：只读写 Saved/SceneSaveRegression 下的固定测试文件，不触碰正式 assets。
// 类型：测试实现。

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Project/Project.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace
{
int failures = 0;

void Check(const bool condition, const char *expression, const int line)
{
    if (!condition)
    {
        std::cerr << "FAILED line " << line << ": " << expression << '\n';
        ++failures;
    }
}
} // namespace

#define CHECK(value) Check((value), #value, __LINE__)

namespace
{
struct SceneListener final : MiniEngine::WorldListener
{
    int created = 0;
    int cleared = 0;

    void OnEntityCreated(MiniEngine::World &, MiniEngine::Entity) override
    {
        ++created;
    }

    void OnWorldCleared(MiniEngine::World &) override
    {
        ++cleared;
    }
};

void CheckClearInvalidatesEntityHandles()
{
    using namespace MiniEngine;
    World world;
    const Entity first = world.CreateEntity("First generation");
    world.DestroyEntity(first);
    const Entity second = world.CreateEntity("Second generation");
    const Entity other = world.CreateEntity("Other slot");
    world.Clear();
    world.Clear();
    const Entity replacement = world.CreateEntity("Replacement");
    const Entity replacementOther = world.CreateEntity("Replacement other");
    CHECK(!world.IsAlive(first));
    CHECK(!world.IsAlive(second));
    CHECK(!world.IsAlive(other));
    CHECK(world.GetComponent<NameComponent>(first) == nullptr);
    world.DestroyEntity(first);
    world.DestroyEntity(second);
    world.DestroyEntity(other);
    CHECK(world.IsAlive(replacement));
    CHECK(world.IsAlive(replacementOther));
}

void CheckMalformedScenesPreserveWorld()
{
    using namespace MiniEngine;
    struct RuntimeComponent
    {
        int value = 37;
    };
    const char *invalidScenes[] = {
        "TINYSCENE 11\nentity 1 0 \"Broken\"\ntransform 1 2\n",
        "TINYSCENE 11\nentity 1 0 \"Broken\"\nvelocity 1 2\nend\n",
        "TINYSCENE 11\nentity 1 0 \"Broken\"\nvelocity 1 invalid 3\nend\n",
        "TINYSCENE 11\nentity 1 0 \"Broken\"\n",
        "TINYSCENE 11\nentity 1 0 \"First\"\nentity 2 0 \"Nested\"\nend\n",
        "TINYSCENE 11\nentity 1 0 \"First\"\nend\nentity 1 0 \"Duplicate\"\nend\n",
        "TINYSCENE 11\nentity 0 0 \"Zero id\"\nend\n",
        "TINYSCENE 11\nentity 1 99 \"Missing parent\"\nend\n",
        "TINYSCENE 11\nentity 1 1 \"Self parent\"\nend\n",
        "TINYSCENE 11\nentity 1 2 \"Cycle A\"\nend\nentity 2 1 \"Cycle B\"\nend\n",
        "TINYSCENE 1\nentity 1 0 \"Missing joint target\"\njoint 1 99 1 1 0 0 0 0\nend\n",
    };
    for (const char *invalidScene : invalidScenes)
    {
        World world;
        const Entity original = world.CreateEntity("Unsaved work");
        world.AddComponent(original, RuntimeComponent{});
        world.GetComponent<TransformComponent>(original)->position = {3.0f, 4.0f, 5.0f};
        SceneListener listener;
        world.AddListener(&listener);
        CHECK(!SceneSerializer::Deserialize(world, invalidScene));
        CHECK(world.IsAlive(original));
        CHECK(world.GetEntityManager().GetAliveEntities().size() == 1);
        const auto *name = world.GetComponent<NameComponent>(original);
        CHECK(name && name->name == "Unsaved work");
        const auto *runtime = world.GetComponent<RuntimeComponent>(original);
        CHECK(runtime && runtime->value == 37);
        const auto *transform = world.GetComponent<TransformComponent>(original);
        CHECK(transform && transform->position.x == 3.0f && transform->position.y == 4.0f && transform->position.z == 5.0f);
        CHECK(listener.created == 0);
        CHECK(listener.cleared == 0);
    }
}

void CheckValidSceneRemapsReferences()
{
    using namespace MiniEngine;
    World world;
    const Entity original = world.CreateEntity("Replaced");
    SceneListener listener;
    world.AddListener(&listener);
    CHECK(SceneSerializer::Deserialize(world, "TINYSCENE 1\nentity 20 10 \"Child\"\njoint 1 10 2 1 0 0 0 0\nend\n"
                                              "entity 10 0 \"Parent\"\nend\n"));
    CHECK(!world.IsAlive(original));
    CHECK(listener.cleared == 1);
    CHECK(listener.created == 2);
    Entity parent = InvalidEntity, child = InvalidEntity;
    for (const Entity entity : world.GetEntityManager().GetAliveEntities())
    {
        const auto *name = world.GetComponent<NameComponent>(entity);
        if (name && name->name == "Parent")
            parent = entity;
        if (name && name->name == "Child")
            child = entity;
    }
    CHECK(parent != InvalidEntity && child != InvalidEntity);
    const auto *relationship = world.GetComponent<RelationshipComponent>(child);
    CHECK(relationship && relationship->parent == parent);
    const auto *joint = world.GetComponent<JointComponent>(child);
    CHECK(joint && joint->connectedEntity == parent && joint->restLength == 2.0f);
}

void CheckSnapshotOrderingSurvivesSlotReuse()
{
    using namespace MiniEngine;
    World world;
    const Entity retired = world.CreateEntity("Retired");
    world.CreateEntity("Unchanged slot");
    world.DestroyEntity(retired);
    world.CreateEntity("Reused slot");
    const std::string snapshot = SceneSerializer::Serialize(world);
    CHECK(SceneSerializer::Deserialize(world, snapshot));
    CHECK(SceneSerializer::Serialize(world) == snapshot);
}

void CheckVelocity(const MiniEngine::World &world, const MiniEngine::Entity entity)
{
    const auto *velocity = world.GetComponent<MiniEngine::VelocityComponent>(entity);
    CHECK(velocity && velocity->linear.x == 1.25f && velocity->linear.y == -2.5f && velocity->linear.z == 3.75f);
}

void CheckVelocityRoundTrips(const std::filesystem::path &root)
{
    using namespace MiniEngine;
    World world;
    const Entity entity = world.CreateEntity("Moving entity");
    world.AddComponent(entity, VelocityComponent{{1.25f, -2.5f, 3.75f}});

    const std::string snapshot = SceneSerializer::Serialize(world);
    world.GetComponent<VelocityComponent>(entity)->linear = {};
    CHECK(SceneSerializer::Deserialize(world, snapshot));
    CHECK(world.GetEntityManager().GetAliveEntities().size() == 1);
    for (const Entity restored : world.GetEntityManager().GetAliveEntities())
        CheckVelocity(world, restored);

    World source;
    const Entity moving = source.CreateEntity("Moving entity");
    source.AddComponent(moving, VelocityComponent{{1.25f, -2.5f, 3.75f}});
    const auto scenePath = root / "Velocity.scene";
    CHECK(SceneSerializer::Save(source, scenePath));
    World loaded;
    CHECK(SceneSerializer::Load(loaded, scenePath));
    CHECK(loaded.GetEntityManager().GetAliveEntities().size() == 1);
    for (const Entity restored : loaded.GetEntityManager().GetAliveEntities())
        CheckVelocity(loaded, restored);

    const auto prefabPath = root / "Velocity.prefab";
    CHECK(Prefab::Save(source, moving, prefabPath));
    World instance;
    const auto roots = Prefab::Instantiate(instance, prefabPath);
    CHECK(roots.size() == 1);
    for (const Entity restored : roots)
        CheckVelocity(instance, restored);

    World legacy;
    CHECK(SceneSerializer::Deserialize(legacy, "TINYSCENE 11\nentity 1 0 \"Stationary\"\nend\n"));
    CHECK(legacy.GetEntityManager().GetAliveEntities().size() == 1);
    for (const Entity restored : legacy.GetEntityManager().GetAliveEntities())
        CHECK(!legacy.HasComponent<VelocityComponent>(restored));
}
} // namespace

int main()
{
    using namespace MiniEngine;
    CheckClearInvalidatesEntityHandles();
    CheckMalformedScenesPreserveWorld();
    CheckValidSceneRemapsReferences();
    CheckSnapshotOrderingSurvivesSlotReuse();
    const std::filesystem::path root = "Saved/SceneSaveRegression";
    std::filesystem::create_directories(root);
    CheckVelocityRoundTrips(root);

    const auto atomicPath = root / "AtomicText.txt";
    std::filesystem::remove(atomicPath);
    std::filesystem::remove(atomicPath.string() + ".bak");
    std::filesystem::remove(atomicPath.string() + ".tmp");
    CHECK(PlatformFile::WriteAllTextAtomic(atomicPath, "before", true));
    CHECK(PlatformFile::WriteAllTextAtomic(atomicPath, "after", true));
    CHECK(PlatformFile::ReadAllText(atomicPath) == "after");
    CHECK(PlatformFile::ReadAllText(atomicPath.string() + ".bak") == "before");
    CHECK(!std::filesystem::exists(atomicPath.string() + ".tmp"));

    World world;
    world.CreateEntity("Untitled content");
    const auto scenePath = root / "Untitled.scene";
    CHECK(!SceneSerializer::Save(world, {}));
    CHECK(SceneSerializer::Save(world, scenePath));
    World loaded;
    CHECK(SceneSerializer::Load(loaded, scenePath));
    CHECK(loaded.GetEntityManager().GetAliveEntities().size() == 1);

    const auto invalidTarget = root / "Directory.scene";
    std::filesystem::create_directories(invalidTarget);
    CHECK(!SceneSerializer::Save(world, invalidTarget));
    CHECK(std::filesystem::is_directory(invalidTarget));
    CHECK(!std::filesystem::exists(invalidTarget.string() + ".tmp"));

    if (failures == 0)
        std::cout << "All scene save regression tests passed\n";
    return failures == 0 ? 0 : 1;
}
