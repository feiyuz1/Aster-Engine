// 文件：tests/physics_acceleration_test.cpp
// 作用：验证碰撞空间加速的查询结果、缓存失效和工作量。
// 约定：使用自行生成的几何与字面量预期；耗时仅报告，不作为通过阈值。
// 类型：实现文件。

#include "tinyengine/ecs/ecs.h"
#include "tinyengine/physics/physics_world.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/resource/pak_archive.h"
#include "tinyengine/runtime/fixed_step.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace tinyengine;
namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::filesystem::path fixtureRoot()
{
    const auto root = std::filesystem::current_path() / "Saved/Tests/PhysicsAcceleration";
    std::filesystem::create_directories(root);
    return root;
}

std::filesystem::path writeMesh(const std::string& name, int triangles = 1, float height = 0.0f)
{
    std::ostringstream mesh;
    for (int i = 0; i < triangles; ++i)
    {
        mesh << "v " << i * 3 << ' ' << height << " 0\n"
             << "v " << i * 3 + 2 << ' ' << height << " 0\n"
             << "v " << i * 3 << ' ' << height << " 2\n"
             << "f " << i * 3 + 1 << ' ' << i * 3 + 2 << ' ' << i * 3 + 3 << '\n';
    }
    const auto path = fixtureRoot() / name;
    check(PlatformFile::writeAllText(path, mesh.str()), "write collision mesh fixture");
    return path;
}

Entity addCollider(World& world, const Vec3& position, ColliderComponent collider = {})
{
    const Entity entity = world.createEntity("Collider");
    world.GetComponent<TransformComponent>(entity)->position = position;
    world.AddComponent(entity, collider);
    return entity;
}

void rotatedMeshContact()
{
    World world;
    PhysicsWorld physics;
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    mesh.meshPath = writeMesh("rotated.obj").string();
    const Entity wall = addCollider(world, {3.0f, 0.0f, 0.0f}, mesh);
    world.GetComponent<TransformComponent>(wall)->rotation.z = 90.0f;
    ColliderComponent sphere;
    sphere.shape = ColliderShape::Sphere;
    sphere.radius = 0.25f;
    sphere.trigger = true;
    addCollider(world, {3.1f, 0.5f, 0.5f}, sphere);
    physics.step(world, 0.0f);
    check(physics.GetEvents().size() == 1, "rotated offset mesh must not be culled before sphere contact");
    check(physics.GetEvents().front().type == CollisionEventType::Enter, "rotated mesh contact enters");
}

void meshCacheAndTriangleWork()
{
    World world;
    PhysicsWorld physics;
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    const auto path = writeMesh("cache.obj", 4096);
    mesh.meshPath = path.string();
    const Entity first = addCollider(world, {}, mesh);
    const Entity second = addCollider(world, {0.0f, -10.0f, 0.0f}, mesh);
    RaycastHit hit;
    const Ray ray{{0.25f, 3.0f, 0.25f}, {0.0f, -1.0f, 0.0f}};
    check(physics.raycast(world, ray, 20.0f, hit), "cached mesh first ray hits");
    check(hit.entity == first && std::abs(hit.distance - 3.0f) < 0.0001f, "nearest shared mesh hit is correct");
    const auto warm = physics.GetStatistics();
    check(warm.meshLoads == 1, "colliders sharing one path load collision geometry once");
    for (int i = 0; i < 10; ++i)
        check(physics.raycast(world, ray, 20.0f, hit), "cached mesh repeated ray hits");
    const auto repeated = physics.GetStatistics();
    check(repeated.meshLoads == warm.meshLoads, "unchanged mesh queries do not reload geometry");
    check(repeated.triangleTests - warm.triangleTests < 80, "mesh BVH prunes distant triangles");

    writeMesh("cache.obj", 1, 1.125f);
    check(physics.raycast(world, ray, 20.0f, hit) && std::abs(hit.distance - 1.875f) < 0.0001f,
          "editing mesh invalidates geometry and bounds");
    check(physics.GetStatistics().meshLoads == warm.meshLoads + 1, "edited mesh is loaded once");
    physics.clear();
    check(physics.GetStatistics().meshLoads == 0, "clear resets profiling counters");
    check(physics.raycast(world, ray, 20.0f, hit) && physics.GetStatistics().meshLoads == 1,
          "clear releases collision mesh cache");

    const auto missing = fixtureRoot() / "created_later.obj";
    std::filesystem::remove(missing);
    world.GetComponent<ColliderComponent>(first)->meshPath = missing.string();
    world.destroyEntity(second);
    check(!physics.raycast(world, ray, 20.0f, hit), "missing mesh does not hit");
    const auto failed = physics.GetStatistics().meshLoads;
    check(!physics.raycast(world, ray, 20.0f, hit) && physics.GetStatistics().meshLoads == failed,
          "unchanged failed mesh is not loaded every query");
    writeMesh("created_later.obj");
    check(physics.raycast(world, ray, 20.0f, hit) && std::abs(hit.distance - 3.0f) < 0.0001f,
          "previously missing mesh recovers when file appears");
}

void mountedMeshInvalidation()
{
    PlatformFile::clearMounts();
    const auto root = fixtureRoot();
    const auto low = root / "MountLow", high = root / "MountHigh";
    std::filesystem::create_directories(low);
    std::filesystem::create_directories(high);
    check(PlatformFile::writeAllText(low / "mounted.obj", PlatformFile::readAllText(writeMesh("low.obj"))),
          "write low priority mounted mesh");
    check(PlatformFile::writeAllText(high / "mounted.obj", PlatformFile::readAllText(writeMesh("high.obj", 1, 1.125f))),
          "write high priority mounted mesh");
    World world;
    PhysicsWorld physics;
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    mesh.meshPath = "mounted.obj";
    addCollider(world, {}, mesh);
    PlatformFile::mountDirectory(low);
    PlatformFile::setReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
    RaycastHit hit;
    const Ray ray{{0.25f, 3.0f, 0.25f}, {0.0f, -1.0f, 0.0f}};
    check(physics.raycast(world, ray, 10.0f, hit) && std::abs(hit.distance - 3.0f) < 0.0001f,
          "mesh loads from mounted-only directory");
    PlatformFile::mountDirectory(high, 10);
    check(physics.raycast(world, ray, 10.0f, hit) && std::abs(hit.distance - 1.875f) < 0.0001f,
          "higher priority mount invalidates mesh cache");
    check(PlatformFile::writeAllText(high / "mounted.obj",
                                     PlatformFile::readAllText(writeMesh("high_edit.obj", 1, 2.25f))),
          "edit mounted mesh");
    check(physics.raycast(world, ray, 10.0f, hit) && std::abs(hit.distance - 0.75f) < 0.0001f,
          "editing a file inside mounted directory invalidates mesh cache");
    const auto archive = root / "collision.tpak";
    check(PakArchive::packDirectory(low, archive), "pack collision fixture");
    PlatformFile::clearMounts();
    check(PlatformFile::mountPak(archive), "mount collision archive");
    PlatformFile::setReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
    check(physics.raycast(world, ray, 10.0f, hit) && std::abs(hit.distance - 3.0f) < 0.0001f,
          "switching to archive reloads collision mesh");
    const auto warm = physics.GetStatistics().meshLoads;
    check(physics.raycast(world, ray, 10.0f, hit) && physics.GetStatistics().meshLoads == warm,
          "archive mesh is cached between queries");
    PlatformFile::clearMounts();
    check(!physics.raycast(world, ray, 10.0f, hit), "unmounting source invalidates mesh cache");
}

void workingDirectoryInvalidation()
{
    const auto original = std::filesystem::current_path();
    const auto root = fixtureRoot();
    const auto low = root / "CwdLow", high = root / "CwdHigh";
    std::filesystem::create_directories(low);
    std::filesystem::create_directories(high);
    const auto lowText = PlatformFile::readAllText(writeMesh("cwd_low.obj"));
    const auto highText = PlatformFile::readAllText(writeMesh("cwd_high.obj", 1, 1.125f));
    check(PlatformFile::writeAllText(low / "relative.obj", lowText) &&
              PlatformFile::writeAllText(high / "relative.obj", highText),
          "write relative path fixtures");
    World world;
    PhysicsWorld physics;
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    mesh.meshPath = "relative.obj";
    addCollider(world, {}, mesh);
    RaycastHit hit;
    const Ray ray{{0.25f, 3.0f, 0.25f}, {0.0f, -1.0f, 0.0f}};
    try
    {
        std::filesystem::current_path(low);
        check(physics.raycast(world, ray, 10.0f, hit) && std::abs(hit.distance - 3.0f) < 0.0001f,
              "relative mesh loads in first working directory");
        std::filesystem::current_path(high);
        check(physics.raycast(world, ray, 10.0f, hit) && std::abs(hit.distance - 1.875f) < 0.0001f,
              "changing working directory invalidates relative mesh cache");
    }
    catch (...)
    {
        std::filesystem::current_path(original);
        throw;
    }
    std::filesystem::current_path(original);
}

void reflectedAndDegenerateMeshes()
{
    World world;
    PhysicsWorld physics;
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    mesh.meshPath = writeMesh("scaled.obj").string();
    const Entity entity = addCollider(world, {5.0f, 1.0f, 0.0f}, mesh);
    auto* transform = world.GetComponent<TransformComponent>(entity);
    transform->scale = {-2.0f, 0.0f, 3.0f};
    RaycastHit hit;
    check(physics.raycast(world, {{4.5f, 4.0f, 0.5f}, {0.0f, -1.0f, 0.0f}}, 10.0f, hit),
          "reflected mesh with zero normal-axis scale remains queryable");
    check(std::abs(hit.distance - 3.0f) < 0.0001f && hit.normal.y > 0.99f, "scaled mesh ray distance and normal");
    transform->scale = {0.0f, 0.0f, 0.0f};
    check(!physics.raycast(world, {{5.0f, 4.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, 10.0f, hit),
          "fully collapsed mesh does not produce a triangle ray hit");
}

void transformedMeshRays()
{
    World world;
    PhysicsWorld physics;
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    mesh.meshPath = writeMesh("transformed_rays.obj", 64).string();
    const Entity entity = addCollider(world, {}, mesh);
    auto* transform = world.GetComponent<TransformComponent>(entity);
    std::mt19937 random(9987);
    std::uniform_real_distribution<float> angle(-180.0f, 180.0f);
    const auto point = [](const Mat4& matrix, Vec3 local)
    {
        return Vec3{matrix.at(0, 0) * local.x + matrix.at(0, 1) * local.y + matrix.at(0, 2) * local.z + matrix.at(0, 3),
                    matrix.at(1, 0) * local.x + matrix.at(1, 1) * local.y + matrix.at(1, 2) * local.z + matrix.at(1, 3),
                    matrix.at(2, 0) * local.x + matrix.at(2, 1) * local.y + matrix.at(2, 2) * local.z +
                        matrix.at(2, 3)};
    };
    for (int i = 0; i < 128; ++i)
    {
        transform->position = {5.0f, -3.0f, 8.0f};
        transform->rotation = {angle(random), angle(random), angle(random)};
        transform->scale = {i % 2 ? -2.0f : 0.5f, 0.0f, i % 3 ? 3.0f : -0.25f};
        const Mat4 matrix = transformMatrix(transform->position, transform->rotation, transform->scale);
        const float x = static_cast<float>(i % 64 * 3);
        const Vec3 a = point(matrix, {x, 0.0f, 0.0f});
        const Vec3 b = point(matrix, {x + 2.0f, 0.0f, 0.0f});
        const Vec3 c = point(matrix, {x, 0.0f, 2.0f});
        const Vec3 inside = point(matrix, {x + 0.5f, 0.0f, 0.5f});
        const Vec3 normal = normalize(cross(b - a, c - a));
        RaycastHit hit;
        check(physics.raycast(world, {inside + normal * 3.0f, normal * -1.0f}, 6.0f, hit),
              "randomly rotated/reflected BVH preserves known interior triangle hits");
        check(std::abs(hit.distance - 3.0f) < 0.001f, "transformed triangle hit distance remains in world units");
    }
}

void cachePathResolution()
{
    World world;
    PhysicsWorld physics;
    const auto path = writeMesh("path_resolution.obj");
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    mesh.meshPath = (path.parent_path() / "directory_that_does_not_exist" / ".." / path.filename()).string();
    const auto entity = addCollider(world, {}, mesh);
    const Ray ray{{0.25f, 3.0f, 0.25f}, {0.0f, -1.0f, 0.0f}};
    RaycastHit hit;
    // Win32 可以先折叠 ..；POSIX 上中间目录不存在时打开会失败。两种解析结果都不能污染有效路径。
    physics.raycast(world, ray, 10.0f, hit);
    world.GetComponent<ColliderComponent>(entity)->meshPath = path.string();
    check(physics.raycast(world, ray, 10.0f, hit), "failed unresolved alias must not poison a valid mesh path");
}

void longNearParallelMeshRay()
{
    const auto path = fixtureRoot() / "near_parallel.obj";
    check(PlatformFile::writeAllText(path, "v 0.005 0 0\nv 0.02 0 0\nv 0.005 0 1\nf 1 2 3\n"),
          "write near-parallel triangle fixture");
    World world;
    PhysicsWorld physics;
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    mesh.meshPath = path.string();
    addCollider(world, {}, mesh);
    RaycastHit hit;
    check(physics.raycast(world, {{0.0f, 20000.0f, 0.25f}, {0.0000005f, -1.0f, 0.0f}}, 30000.0f, hit),
          "tiny nonzero ray direction reaches mesh over long distance");
    check(std::abs(hit.distance - 20000.0f) < 0.01f, "near-parallel mesh hit distance");
}

void meshBoundaryRays()
{
    for (const char* width : {"0.8", "10.2"})
    {
        const auto path = fixtureRoot() / "boundary.obj";
        check(PlatformFile::writeAllText(path, std::string("v 0.1 0 0\nv ") + width + " 0 0\nv 0.1 0 1\nf 1 2 3\n"),
              "write edge ray fixture");
        World world;
        PhysicsWorld physics;
        ColliderComponent mesh;
        mesh.shape = ColliderShape::TriangleMesh;
        mesh.meshPath = path.string();
        addCollider(world, {}, mesh);
        RaycastHit hit;
        check(physics.raycast(world, {{0.1f, 1.0f, 0.25f}, {0.0f, -1.0f, 0.0f}}, 2.0f, hit),
              "BVH bounds do not round inward at a triangle edge");
    }
}

void equalXResponseOrder()
{
    World world;
    PhysicsWorld physics;
    addCollider(world, {});
    const Entity dynamic = addCollider(world, {});
    RigidBodyComponent body;
    body.useGravity = false;
    world.AddComponent(dynamic, body);
    // 独立捕获旧宽阶段的收集/排序顺序，验证空间树不能改变重合物体的分离方向。
    std::vector<Entity> originalOrder;
    world.GetComponentManager().ForEach<ColliderComponent>([&](Entity entity, ColliderComponent&)
                                                           { originalOrder.push_back(entity); });
    std::sort(originalOrder.begin(), originalOrder.end(), [](Entity, Entity) { return false; });
    const float expectedX = originalOrder[0] == dynamic ? 1.0f : -1.0f;
    physics.step(world, 0.0f);
    check(std::abs(world.GetComponent<TransformComponent>(dynamic)->position.x - expectedX) < 0.0001f,
          "equal-X candidates preserve legacy collision response order");
}

void meshSphereResponse()
{
    World world;
    PhysicsWorld physics;
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    mesh.meshPath = writeMesh("contact.obj", 4096).string();
    addCollider(world, {}, mesh);
    ColliderComponent sphere;
    sphere.shape = ColliderShape::Sphere;
    sphere.radius = 0.5f;
    const Entity ball = addCollider(world, {0.5f, 0.2f, 0.5f}, sphere);
    RigidBodyComponent body;
    body.useGravity = false;
    body.velocity = {0.0f, -1.0f, 0.0f};
    world.AddComponent(ball, body);
    physics.step(world, 0.0f);
    check(std::abs(world.GetComponent<TransformComponent>(ball)->position.y - 0.5f) < 0.0001f,
          "mesh sphere response resolves triangle penetration");
    check(std::abs(world.GetComponent<RigidBodyComponent>(ball)->velocity.y) < 0.0001f,
          "mesh sphere response removes inward normal velocity");
    check(physics.GetStatistics().meshLoads == 1 && physics.GetStatistics().triangleTests < 12,
          "mesh sphere contact prunes triangles and reuses loaded geometry");
}

void sceneTreeWorkAndMutations()
{
    World world;
    PhysicsWorld physics;
    std::vector<Entity> entities;
    for (int i = 0; i < 1024; ++i)
        entities.push_back(addCollider(world, {0.0f, i * 3.0f, 0.0f}));
    physics.step(world, 0.0f);
    const auto first = physics.GetStatistics();
    check(first.staticTreeBuilds == 1 && first.sceneBoundsTests > 0, "scene builds and queries static spatial tree");
    check(first.sceneBoundsTests < 1024 * 128, "same-X scene prunes separated colliders in three dimensions");
    physics.step(world, 0.0f);
    check(physics.GetStatistics().staticTreeBuilds == first.staticTreeBuilds, "unchanged static scene reuses tree");

    const Entity moving = entities[1];
    RigidBodyComponent body;
    body.type = BodyType::Kinematic;
    world.AddComponent(moving, body);
    const Aabb origin{{-0.6f, -0.6f, -0.6f}, {0.6f, 0.6f, 0.6f}};
    check(physics.Overlap(world, origin).size() == 1, "body type change migrates between scene trees");
    check(physics.GetStatistics().dynamicTreeBuilds == 1, "non-static collider gets a dynamic tree");
    world.GetComponent<TransformComponent>(moving)->position.y = 0.25f;
    check(physics.Overlap(world, origin).size() == 2, "direct transform edit refits dynamic bounds before query");
    world.GetComponent<TransformComponent>(entities[0])->position.y = -10.0f;
    check(physics.Overlap(world, origin).size() == 1, "direct static transform edit updates static tree");
    world.destroyEntity(moving);
    check(physics.Overlap(world, origin).empty(), "destroyed entity is removed from tree");
    const Entity replacement = addCollider(world, {});
    const auto found = physics.Overlap(world, origin);
    check(found.size() == 1 && found[0] == replacement, "reused entity index does not retain old generation");
    world.GetComponentManager().Remove<TransformComponent>(replacement);
    check(physics.Overlap(world, origin).empty(), "collider without transform is not a phantom at origin");

    World second;
    const Entity secondEntity = addCollider(second, {});
    check(physics.Overlap(second, origin) == std::vector<Entity>{secondEntity},
          "switching worlds replaces spatial snapshot");
    PhysicsWorld copy = physics;
    physics.clear();
    check(copy.Overlap(second, origin).size() == 1, "copy keeps independent collision cache lifetime");
    second.GetComponent<ColliderComponent>(secondEntity)->halfExtents = {5.0f, 0.5f, 0.5f};
    check(copy.Overlap(second, {{4.0f, -0.1f, -0.1f}, {4.5f, 0.1f, 0.1f}}).size() == 1,
          "direct collider dimension edit updates scene bounds");
}

void eventLifecycleAndFiltering()
{
    World world;
    PhysicsWorld physics;
    ColliderComponent trigger;
    trigger.trigger = true;
    const Entity first = addCollider(world, {}, trigger);
    const Entity second = addCollider(world, {0.25f, 0.0f, 0.0f});
    physics.step(world, 0.0f);
    check(physics.GetEvents().size() == 1 && physics.GetEvents()[0].type == CollisionEventType::Enter &&
              physics.GetEvents()[0].trigger,
          "static-static trigger still generates Enter");
    physics.step(world, 0.0f);
    check(physics.GetEvents().size() == 1 && physics.GetEvents()[0].type == CollisionEventType::Stay,
          "persistent pair generates Stay");
    world.GetComponent<ColliderComponent>(second)->layer = 2;
    world.GetComponent<ColliderComponent>(first)->mask = 1;
    physics.step(world, 0.0f);
    check(physics.GetEvents().size() == 1 && physics.GetEvents()[0].type == CollisionEventType::Exit,
          "mask edit ends previous collision pair");
    check(physics.Overlap(world, {{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}, {2, false, kInvalidEntity}) ==
              std::vector<Entity>{second},
          "query layer and trigger filters still apply");
    world.GetComponent<ColliderComponent>(first)->mask = 0xffffffffu;
    JointComponent joint;
    joint.connectedEntity = second;
    joint.collideConnected = false;
    world.AddComponent(first, joint);
    physics.step(world, 0.0f);
    check(physics.GetEvents().empty(), "joint-connected bodies respect collision exclusion");
    world.GetComponent<JointComponent>(first)->collideConnected = true;
    physics.step(world, 0.0f);
    check(physics.GetEvents().size() == 1 && physics.GetEvents()[0].type == CollisionEventType::Enter,
          "joint filter edit enables collision without rebuilding geometry");
    world.destroyEntity(second);
    physics.step(world, 0.0f);
    check(physics.GetEvents().size() == 1 && physics.GetEvents()[0].type == CollisionEventType::Exit,
          "entity deletion emits one Exit");
}

void characterBoundsAfterPositionRestore()
{
    for (const bool controller : {false, true})
        for (const bool kinematic : {false, true})
        {
            World world;
            PhysicsWorld physics;
            const Entity entity = addCollider(world, {});
            if (kinematic)
            {
                RigidBodyComponent body;
                body.type = BodyType::Kinematic;
                world.AddComponent(entity, body);
            }
            const Vec3 moved =
                controller
                    ? physics.moveCharacterController(world, entity, {10.0f, 0.0f, 0.0f}, 0.0f).appliedDisplacement
                    : physics.moveCharacter(world, entity, {10.0f, 0.0f, 0.0f});
            check(std::abs(moved.x - 10.0f) < 0.0001f, "unobstructed character reaches the destination");
            world.GetComponent<TransformComponent>(entity)->position = {};
            check(physics.Overlap(world, {{-0.25f, -0.25f, -0.25f}, {0.25f, 0.25f, 0.25f}}) ==
                      std::vector<Entity>{entity},
                  "restoring a moved character's position updates its scene bounds before overlap");
            RaycastHit hit;
            check(physics.raycast(world, {{-2.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}, 4.0f, hit) &&
                      hit.entity == entity && std::abs(hit.distance - 1.5f) < 0.0001f,
                  "raycast sees the restored character at the original position");
            check(physics.Overlap(world, {{9.75f, -0.25f, -0.25f}, {10.25f, 0.25f, 0.25f}}).empty(),
                  "restoring a character does not leave phantom bounds at the destination");
        }
}

void controllerReloadsMeshBeforeMoving()
{
    World world;
    PhysicsWorld physics;
    const auto path = fixtureRoot() / "controller_reload.obj";
    check(PlatformFile::writeAllText(path, "v 21 -2 -2\nv 21 2 -2\nv 21 0 2\nf 1 2 3\n"),
          "write distant controller obstacle");
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    mesh.meshPath = path.string();
    const Entity wall = addCollider(world, {}, mesh);
    const Entity character = addCollider(world, {});
    check(physics.Overlap(world, {{20.0f, -3.0f, -3.0f}, {22.0f, 3.0f, 3.0f}}) == std::vector<Entity>{wall},
          "warm the mesh cache before moving the controller");
    const auto before = physics.GetStatistics();
    check(PlatformFile::writeAllText(path, "v 1 -2 -2\nv 1 2 -2\nv 1 0 2\nf 1 2 3\n# reloaded nearby wall\n"),
          "reload the mesh into the character's path");
    const auto result = physics.moveCharacterController(world, character, {1.0f, 0.0f, 0.0f}, 0.0f);
    check(std::abs(result.appliedDisplacement.x) < 0.0001f,
          "the first controller move after mesh reload is blocked by the new wall bounds");
    const auto after = physics.GetStatistics();
    check(after.meshLoads == before.meshLoads + 1, "controller reloads changed mesh geometry once");
    check(after.sceneSyncs == before.sceneSyncs + 1, "controller submoves share one full scene synchronization");
}

void staticContactEvents()
{
    for (const bool explicitBodies : {false, true})
    {
        World world;
        PhysicsWorld physics;
        const Entity first = addCollider(world, {});
        const Entity second = addCollider(world, {0.25f, 0.0f, 0.0f});
        if (explicitBodies)
        {
            RigidBodyComponent body;
            body.type = BodyType::Static;
            world.AddComponent(first, body);
            world.AddComponent(second, body);
        }
        physics.step(world, 0.0f);
        check(physics.GetEvents().size() == 1 && physics.GetEvents()[0].type == CollisionEventType::Enter &&
                  !physics.GetEvents()[0].trigger,
              "static non-trigger contact generates Enter");
        physics.step(world, 0.0f);
        check(physics.GetEvents().size() == 1 && physics.GetEvents()[0].type == CollisionEventType::Stay,
              "static non-trigger contact generates Stay");
        world.GetComponent<TransformComponent>(second)->position.x = 10.0f;
        physics.step(world, 0.0f);
        check(physics.GetEvents().size() == 1 && physics.GetEvents()[0].type == CollisionEventType::Exit,
              "moving a static collider out of contact generates Exit");
        physics.step(world, 0.0f);
        check(physics.GetEvents().empty(), "separated static contacts emit Exit only once");
    }
}

void contactSurvivesStaticTypeChange()
{
    World world;
    PhysicsWorld physics;
    const Entity first = addCollider(world, {});
    addCollider(world, {0.25f, 0.0f, 0.0f});
    RigidBodyComponent body;
    body.type = BodyType::Kinematic;
    world.AddComponent(first, body);
    physics.step(world, 0.0f);
    check(physics.GetEvents().size() == 1 && physics.GetEvents()[0].type == CollisionEventType::Enter,
          "kinematic contact initially enters");
    world.GetComponent<RigidBodyComponent>(first)->type = BodyType::Static;
    physics.step(world, 0.0f);
    check(physics.GetEvents().size() == 1 && physics.GetEvents()[0].type == CollisionEventType::Stay,
          "changing a touching body to Static preserves contact instead of emitting Exit");
}

void randomizedSceneReference()
{
    World world;
    PhysicsWorld physics;
    std::mt19937 random(7341);
    std::uniform_real_distribution<float> position(-10.0f, 10.0f);
    struct Box
    {
        Entity entity;
        Aabb bounds;
    };
    std::vector<Box> boxes;
    for (int i = 0; i < 180; ++i)
    {
        const Vec3 center{position(random), position(random), position(random)};
        ColliderComponent collider;
        collider.trigger = true;
        const auto entity = addCollider(world, center, collider);
        boxes.push_back({entity, {center - Vec3{0.5f, 0.5f, 0.5f}, center + Vec3{0.5f, 0.5f, 0.5f}}});
        if (i % 3 == 0)
        {
            RigidBodyComponent body;
            body.type = BodyType::Kinematic;
            world.AddComponent(entity, body);
        }
    }
    physics.step(world, 0.0f);
    std::set<std::pair<Entity, Entity>> referencePairs, actualPairs;
    for (std::size_t i = 0; i < boxes.size(); ++i)
        for (std::size_t j = i + 1; j < boxes.size(); ++j)
            if (boxes[i].bounds.intersects(boxes[j].bounds))
                referencePairs.emplace(std::min(boxes[i].entity, boxes[j].entity),
                                       std::max(boxes[i].entity, boxes[j].entity));
    for (const auto& event : physics.GetEvents())
        actualPairs.emplace(std::min(event.first, event.second), std::max(event.first, event.second));
    check(referencePairs == actualPairs, "scene broadphase agrees with exhaustive independent AABB pairs");
    for (int i = 0; i < 40; ++i)
    {
        const Vec3 center{position(random), position(random), position(random)};
        const Aabb query{center - Vec3{2.0f, 2.0f, 2.0f}, center + Vec3{2.0f, 2.0f, 2.0f}};
        std::set<Entity> expected;
        for (const auto& box : boxes)
            if (box.bounds.intersects(query))
                expected.insert(box.entity);
        const auto result = physics.Overlap(world, query);
        check(std::set<Entity>(result.begin(), result.end()) == expected,
              "tree overlap agrees with exhaustive AABB query");
        for (const float radius : {0.0f, 0.3f})
        {
            const Ray ray{{center.x, 20.0f, center.z}, {0.0f, -1.0f, 0.0f}};
            float nearest = 100.0f;
            Entity expectedEntity = kInvalidEntity;
            for (const auto& box : boxes)
                if (center.x >= box.bounds.minimum.x - radius && center.x <= box.bounds.maximum.x + radius &&
                    center.z >= box.bounds.minimum.z - radius && center.z <= box.bounds.maximum.z + radius)
                {
                    const float distance = 20.0f - box.bounds.maximum.y - radius;
                    if (distance < nearest)
                    {
                        nearest = distance;
                        expectedEntity = box.entity;
                    }
                }
            RaycastHit hit;
            const bool found = radius == 0.0f ? physics.raycast(world, ray, 100.0f, hit)
                                              : physics.sphereCast(world, ray, radius, 100.0f, hit);
            check(found == (expectedEntity != kInvalidEntity), "tree ray/cast hit presence agrees with reference");
            if (found)
                check(hit.entity == expectedEntity && std::abs(hit.distance - nearest) < 0.0001f,
                      "tree ray/cast nearest hit agrees with reference");
        }
    }
}

void largeStaticCollider()
{
    World world;
    PhysicsWorld physics;
    ColliderComponent floor;
    floor.halfExtents = {1000000.0f, 0.5f, 1.0f};
    floor.trigger = true;
    const Entity ground = addCollider(world, {0.0f, -0.5f, 0.0f}, floor);
    for (int i = 0; i < 512; ++i)
        addCollider(world, {0.0f, 2.0f + i * 3.0f, 0.0f});
    physics.step(world, 0.0f);
    check(physics.GetEvents().empty(), "very wide static box does not create false candidate contacts");
    check(physics.GetStatistics().sceneBoundsTests < 513 * 128, "large X extent does not destroy spatial pruning");
    RaycastHit hit;
    check(physics.raycast(world, {{500000.0f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, 10.0f, hit) && hit.entity == ground &&
              std::abs(hit.distance - 3.0f) < 0.0001f,
          "ray hits distant part of oversized static box");
    check(physics.sphereCast(world, {{500000.0f, 3.0f, 0.0f}, {0.0f, -1.0f, 0.0f}}, 0.5f, 10.0f, hit) &&
              std::abs(hit.distance - 2.5f) < 0.0001f,
          "sphere cast retains expanded AABB semantics for oversized box");
}

void sceneTests()
{
    sceneTreeWorkAndMutations();
    eventLifecycleAndFiltering();
    randomizedSceneReference();
    largeStaticCollider();
}

void debugBoxesFollowEverySubstep()
{
    World world;
    PhysicsWorld physics;
    const Entity entity = addCollider(world, {});
    RigidBodyComponent body;
    body.useGravity = false;
    body.linearDamping = 0.0f;
    body.velocity = {1.0f, 0.0f, 0.0f};
    world.AddComponent(entity, body);
    physics.step(world, 0.0f);
    check(physics.GetDebugBoxes().empty(), "debug collection is disabled by default");
    physics.step(world, 0.0f);
    check(physics.GetDebugBoxes().empty(), "reading debug boxes does not enable collection");
    physics.setDebugBoxesEnabled(true);
    physics.step(world, 1.0f);
    physics.step(world, 1.0f);
    const auto& boxes = physics.GetDebugBoxes();
    check(boxes.size() == 1, "enabled debug boxes include the moving body");
    check(std::abs((boxes[0].bounds.minimum.x + boxes[0].bounds.maximum.x) * 0.5f - 2.0f) < 0.0001f,
          "debug boxes reflect the last substep when several steps run before rendering");
    physics.setDebugBoxesEnabled(false);
    check(physics.GetDebugBoxes().empty(), "disabling debug collection clears the snapshot");
    physics.step(world, 1.0f);
    check(physics.GetDebugBoxes().empty(), "debug reads keep collection disabled across steps");
    physics.setDebugBoxesEnabled(true);
    physics.step(world, 1.0f);
    const auto& resumed = physics.GetDebugBoxes();
    check(resumed.size() == 1 &&
              std::abs((resumed[0].bounds.minimum.x + resumed[0].bounds.maximum.x) * 0.5f - 4.0f) < 0.0001f,
          "reenabling debug collection produces a fresh snapshot");
    physics.clear();
    physics.step(world, 1.0f);
    check(physics.GetDebugBoxes().empty(), "clear disables debug collection for the next session");
}

void fixedStepBudgets()
{
    for (const float step : {0.016f, 0.001f, 1.0f / 60.0f})
    {
        FixedStepAccumulator accumulator;
        for (int frame = 0; frame < 20; ++frame)
            check(accumulator.advance(step * 10.0f, step, 4) == 4,
                  "capped fixed-step frames execute all four budgeted steps without losing one to rounding");
        check(accumulator.advance(step * 0.25f, step, 4) == 0, "capped frames discard excess time debt");
        check(accumulator.advance(0.0f, step, 4) == 0, "zero frame time does not advance simulation");
        accumulator.reset();
        for (int quarter = 0; quarter < 3; ++quarter)
            check(accumulator.advance(step * 0.25f, step, 4) == 0, "fractional fixed-step time is retained");
        check(accumulator.advance(step * 0.25f, step, 4) == 1, "four quarter steps produce one full update");
        check(accumulator.advance(step * 0.25f, step, 4) == 0, "partial time starts next accumulation");
        accumulator.reset();
        check(accumulator.advance(step * 0.75f, step, 4) == 0, "reset removes fractional time from prior session");
        check(accumulator.advance(step * 10.0f, step, 1) == 1, "caller-selected one-step budget is preserved");
    }
    FixedStepAccumulator accumulator;
    check(accumulator.advance(-1.0f, 0.016f, 4) == 0 && accumulator.advance(1.0f, 0.0f, 4) == 0 &&
              accumulator.advance(1.0f, 0.016f, 0) == 0,
          "invalid fixed-step input does not produce updates");
}

void benchmark()
{
    using Clock = std::chrono::steady_clock;
    World meshWorld;
    PhysicsWorld meshPhysics;
    ColliderComponent mesh;
    mesh.shape = ColliderShape::TriangleMesh;
    mesh.meshPath = writeMesh("benchmark.obj", 4096).string();
    addCollider(meshWorld, {}, mesh);
    RaycastHit hit;
    check(meshPhysics.raycast(meshWorld, {{0.25f, 3.0f, 0.25f}, {0.0f, -1.0f, 0.0f}}, 10.0f, hit),
          "benchmark mesh ray hits");
    const auto meshStart = Clock::now();
    for (int i = 0; i < 40; ++i)
        check(meshPhysics.raycast(meshWorld, {{0.25f, 3.0f, 0.25f}, {0.0f, -1.0f, 0.0f}}, 10.0f, hit),
              "repeated benchmark mesh ray hits");
    const double meshMs = std::chrono::duration<double, std::milli>(Clock::now() - meshStart).count();

    World scene;
    PhysicsWorld physics;
    for (int i = 0; i < 1024; ++i)
        addCollider(scene, {0.0f, i * 3.0f, 0.0f});
    physics.step(scene, 0.0f);
    const auto sceneStart = Clock::now();
    for (int i = 0; i < 5; ++i)
        physics.step(scene, 0.0f);
    const double sceneMs = std::chrono::duration<double, std::milli>(Clock::now() - sceneStart).count();
    check(physics.GetEvents().empty(), "X-aligned separated colliders must not collide");
    std::cout << "BENCH mesh_4096_triangles_40_rays_ms=" << meshMs << " same_x_1024_colliders_5_steps_ms=" << sceneMs
              << '\n';
    const auto meshStats = meshPhysics.GetStatistics(), sceneStats = physics.GetStatistics();
    std::cout << "WORK mesh_loads=" << meshStats.meshLoads << " mesh_triangle_tests=" << meshStats.triangleTests
              << " scene_bounds_tests=" << sceneStats.sceneBoundsTests
              << " static_tree_builds=" << sceneStats.staticTreeBuilds << '\n';
    ColliderComponent large;
    large.halfExtents = {1000000.0f, 0.5f, 1.0f};
    addCollider(scene, {0.0f, -10.0f, 0.0f}, large);
    const auto before = physics.GetStatistics();
    const auto largeStart = Clock::now();
    for (int i = 0; i < 5; ++i)
        physics.step(scene, 0.0f);
    const double largeMs = std::chrono::duration<double, std::milli>(Clock::now() - largeStart).count();
    check(physics.GetEvents().empty(), "large-object benchmark does not create contacts");
    std::cout << "BENCH same_x_1024_plus_large_box_5_steps_ms=" << largeMs
              << " bounds_tests=" << physics.GetStatistics().sceneBoundsTests - before.sceneBoundsTests << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc > 1 && std::string(argv[1]) == "--benchmark")
        {
            benchmark();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--scene")
        {
            sceneTests();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--paths")
        {
            cachePathResolution();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--pruning")
        {
            longNearParallelMeshRay();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--bounds")
        {
            meshBoundaryRays();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--order")
        {
            equalXResponseOrder();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--debug-boxes")
        {
            debugBoxesFollowEverySubstep();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--fixed-step")
        {
            fixedStepBudgets();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--character-restore")
        {
            characterBoundsAfterPositionRestore();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--controller-reload")
        {
            controllerReloadsMeshBeforeMoving();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--static-events")
        {
            staticContactEvents();
            return 0;
        }
        if (argc > 1 && std::string(argv[1]) == "--static-type-change")
        {
            contactSurvivesStaticTypeChange();
            return 0;
        }
        rotatedMeshContact();
        meshCacheAndTriangleWork();
        mountedMeshInvalidation();
        workingDirectoryInvalidation();
        reflectedAndDegenerateMeshes();
        transformedMeshRays();
        cachePathResolution();
        longNearParallelMeshRay();
        meshBoundaryRays();
        equalXResponseOrder();
        meshSphereResponse();
        sceneTests();
        debugBoxesFollowEverySubstep();
        fixedStepBudgets();
        characterBoundsAfterPositionRestore();
        controllerReloadsMeshBeforeMoving();
        staticContactEvents();
        contactSurvivesStaticTypeChange();
        std::cout << "Physics acceleration regressions passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
