// 文件：tests/scene_save_test.cpp
// 作用：快速验证场景首次保存所依赖的原子写入、备份和失败保护。
// 约定：只读写 saved/SceneSaveRegression 下的固定测试文件，不触碰正式 assets。
// 类型：测试实现。

#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/project/project.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace
{
int failures = 0;

void check(const bool condition, const char* expression, const int line)
{
    if (!condition)
    {
        std::cerr << "FAILED line " << line << ": " << expression << '\n';
        ++failures;
    }
}
} // namespace

#define CHECK(value) check((value), #value, __LINE__)

namespace
{
struct SceneListener final : tinyengine::WorldListener
{
    int created = 0;
    int cleared = 0;

    void onEntityCreated(tinyengine::World&, tinyengine::Entity) override
    {
        ++created;
    }

    void onWorldCleared(tinyengine::World&) override
    {
        ++cleared;
    }
};

void checkClearInvalidatesEntityHandles()
{
    using namespace tinyengine;
    World world;
    const Entity first = world.createEntity("First generation");
    world.destroyEntity(first);
    const Entity second = world.createEntity("Second generation");
    const Entity other = world.createEntity("Other slot");
    world.clear();
    world.clear();
    const Entity replacement = world.createEntity("Replacement");
    const Entity replacementOther = world.createEntity("Replacement other");
    CHECK(!world.isAlive(first));
    CHECK(!world.isAlive(second));
    CHECK(!world.isAlive(other));
    CHECK(world.GetComponent<NameComponent>(first) == nullptr);
    world.destroyEntity(first);
    world.destroyEntity(second);
    world.destroyEntity(other);
    CHECK(world.isAlive(replacement));
    CHECK(world.isAlive(replacementOther));
}

void checkMalformedScenesPreserveWorld()
{
    using namespace tinyengine;
    struct RuntimeComponent
    {
        int value = 37;
    };
    const char* invalidScenes[] = {
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
    for (const char* invalidScene : invalidScenes)
    {
        World world;
        const Entity original = world.createEntity("Unsaved work");
        world.AddComponent(original, RuntimeComponent{});
        world.GetComponent<TransformComponent>(original)->position = {3.0f, 4.0f, 5.0f};
        SceneListener listener;
        world.addListener(&listener);
        CHECK(!SceneSerializer::deserialize(world, invalidScene));
        CHECK(world.isAlive(original));
        CHECK(world.GetEntityManager().GetAliveEntities().size() == 1);
        const auto* name = world.GetComponent<NameComponent>(original);
        CHECK(name && name->name == "Unsaved work");
        const auto* runtime = world.GetComponent<RuntimeComponent>(original);
        CHECK(runtime && runtime->value == 37);
        const auto* transform = world.GetComponent<TransformComponent>(original);
        CHECK(transform && transform->position.x == 3.0f && transform->position.y == 4.0f &&
              transform->position.z == 5.0f);
        CHECK(listener.created == 0);
        CHECK(listener.cleared == 0);
    }
}

void checkValidSceneRemapsReferences()
{
    using namespace tinyengine;
    World world;
    const Entity original = world.createEntity("Replaced");
    SceneListener listener;
    world.addListener(&listener);
    CHECK(SceneSerializer::deserialize(world, "TINYSCENE 1\nentity 20 10 \"Child\"\njoint 1 10 2 1 0 0 0 0\nend\n"
                                              "entity 10 0 \"Parent\"\nend\n"));
    CHECK(!world.isAlive(original));
    CHECK(listener.cleared == 1);
    CHECK(listener.created == 2);
    Entity parent = kInvalidEntity, child = kInvalidEntity;
    for (const Entity entity : world.GetEntityManager().GetAliveEntities())
    {
        const auto* name = world.GetComponent<NameComponent>(entity);
        if (name && name->name == "Parent")
            parent = entity;
        if (name && name->name == "Child")
            child = entity;
    }
    CHECK(parent != kInvalidEntity && child != kInvalidEntity);
    const auto* relationship = world.GetComponent<RelationshipComponent>(child);
    CHECK(relationship && relationship->parent == parent);
    const auto* joint = world.GetComponent<JointComponent>(child);
    CHECK(joint && joint->connectedEntity == parent && joint->restLength == 2.0f);
}

void checkSnapshotOrderingSurvivesSlotReuse()
{
    using namespace tinyengine;
    World world;
    const Entity retired = world.createEntity("Retired");
    world.createEntity("Unchanged slot");
    world.destroyEntity(retired);
    world.createEntity("Reused slot");
    const std::string snapshot = SceneSerializer::serialize(world);
    CHECK(SceneSerializer::deserialize(world, snapshot));
    CHECK(SceneSerializer::serialize(world) == snapshot);
}

void checkVelocity(const tinyengine::World& world, const tinyengine::Entity entity)
{
    const auto* velocity = world.GetComponent<tinyengine::VelocityComponent>(entity);
    CHECK(velocity && velocity->linear.x == 1.25f && velocity->linear.y == -2.5f && velocity->linear.z == 3.75f);
}

void checkVelocityRoundTrips(const std::filesystem::path& root)
{
    using namespace tinyengine;
    World world;
    const Entity entity = world.createEntity("Moving entity");
    world.AddComponent(entity, VelocityComponent{{1.25f, -2.5f, 3.75f}});

    const std::string snapshot = SceneSerializer::serialize(world);
    world.GetComponent<VelocityComponent>(entity)->linear = {};
    CHECK(SceneSerializer::deserialize(world, snapshot));
    CHECK(world.GetEntityManager().GetAliveEntities().size() == 1);
    for (const Entity restored : world.GetEntityManager().GetAliveEntities())
        checkVelocity(world, restored);

    World source;
    const Entity moving = source.createEntity("Moving entity");
    source.AddComponent(moving, VelocityComponent{{1.25f, -2.5f, 3.75f}});
    const auto scenePath = root / "Velocity.scene";
    CHECK(SceneSerializer::Save(source, scenePath));
    World loaded;
    CHECK(SceneSerializer::load(loaded, scenePath));
    CHECK(loaded.GetEntityManager().GetAliveEntities().size() == 1);
    for (const Entity restored : loaded.GetEntityManager().GetAliveEntities())
        checkVelocity(loaded, restored);

    const auto prefabPath = root / "Velocity.prefab";
    CHECK(Prefab::Save(source, moving, prefabPath));
    World instance;
    const auto roots = Prefab::Instantiate(instance, prefabPath);
    CHECK(roots.size() == 1);
    for (const Entity restored : roots)
        checkVelocity(instance, restored);

    World legacy;
    CHECK(SceneSerializer::deserialize(legacy, "TINYSCENE 11\nentity 1 0 \"Stationary\"\nend\n"));
    CHECK(legacy.GetEntityManager().GetAliveEntities().size() == 1);
    for (const Entity restored : legacy.GetEntityManager().GetAliveEntities())
        CHECK(!legacy.HasComponent<VelocityComponent>(restored));
}
} // namespace

int main()
{
    using namespace tinyengine;
    checkClearInvalidatesEntityHandles();
    checkMalformedScenesPreserveWorld();
    checkValidSceneRemapsReferences();
    checkSnapshotOrderingSurvivesSlotReuse();
    const std::filesystem::path root = "saved/SceneSaveRegression";
    std::filesystem::create_directories(root);
    checkVelocityRoundTrips(root);

    const auto atomicPath = root / "AtomicText.txt";
    std::filesystem::remove(atomicPath);
    std::filesystem::remove(atomicPath.string() + ".bak");
    std::filesystem::remove(atomicPath.string() + ".tmp");
    CHECK(PlatformFile::writeAllTextAtomic(atomicPath, "before", true));
    CHECK(PlatformFile::writeAllTextAtomic(atomicPath, "after", true));
    CHECK(PlatformFile::readAllText(atomicPath) == "after");
    CHECK(PlatformFile::readAllText(atomicPath.string() + ".bak") == "before");
    CHECK(!std::filesystem::exists(atomicPath.string() + ".tmp"));

    World world;
    world.createEntity("Untitled content");
    const auto scenePath = root / "Untitled.scene";
    CHECK(!SceneSerializer::Save(world, {}));
    CHECK(SceneSerializer::Save(world, scenePath));
    World loaded;
    CHECK(SceneSerializer::load(loaded, scenePath));
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
