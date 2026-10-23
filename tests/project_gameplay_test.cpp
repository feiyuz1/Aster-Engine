// 文件：tests/project_gameplay_test.cpp
// 作用：验证 Project SDK 玩法扩展表的版本协商、实体生命周期、射线查询、角色移动与 UI 能力。
// 约定：不依赖图形设备；渲染设备缺失时相关能力必须明确返回 NOT_READY。
#include "tinyengine/core/logger.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/input/input_system.h"
#include "tinyengine/physics/physics_world.h"
#include "tinyengine/project/project_host_bridge.h"
#include "tinyengine/project_sdk/project_gameplay_api.h"
#include "tinyengine/project_sdk/project.h"
#include "tinyengine/ui/ui_system.h"
#include "tinyengine/vfx/particle_system.h"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace tinyengine;

namespace
{
int checks = 0;
void check(bool condition, const char* name)
{
    if (!condition)
        throw std::runtime_error(name);
    ++checks;
    std::cout << "PASS " << name << '\n';
}
TinyProjectString span(const std::string& text)
{
    return {text.data(), text.size()};
}
const TinyProjectGameplayAPI* query(ProjectHostBridge& bridge, const char* id, uint32_t version,
                                    TinyProjectResult expected)
{
    const void* extension = reinterpret_cast<const void*>(0x1);
    const TinyProjectResult result = bridge.GetAPI().query_interface(bridge.GetAPI().host_context, span(id), version,
                                                                    &extension);
    check(result == expected, "query_interface returns the expected result code");
    if (expected != TINY_PROJECT_OK)
        check(extension == nullptr, "failed query clears the out pointer");
    return expected == TINY_PROJECT_OK ? static_cast<const TinyProjectGameplayAPI*>(extension) : nullptr;
}
} // namespace

int main()
{
    try
    {
        Logger logger;
        World world;
        InputSystem input;
        PhysicsWorld physics;
        UISystem ui;
        VfxSystem vfx;
        ProjectHostBridge bridge(world, input, logger);
        bridge.setServices(&physics, &ui, &vfx, nullptr, nullptr);
        bridge.setWorldAccess(true, true);

        // ---- 版本协商 ----
        check(bridge.GetAPI().abi_version == TINY_PROJECT_ABI_VERSION, "base host ABI version is unchanged");
        query(bridge, "aster.gameplay", 0, TINY_PROJECT_INCOMPATIBLE);
        query(bridge, "aster.unknown", 1, TINY_PROJECT_UNSUPPORTED);
        query(bridge, "aster.gameplay", 2, TINY_PROJECT_INCOMPATIBLE);
        const TinyProjectGameplayAPI* api = query(bridge, TINY_PROJECT_GAMEPLAY_INTERFACE,
                                                  TINY_PROJECT_GAMEPLAY_VERSION, TINY_PROJECT_OK);
        check(api != nullptr, "gameplay extension is returned");
        check(api->struct_size == sizeof(TinyProjectGameplayAPI), "extension reports its real size");
        check(TINY_PROJECT_GAMEPLAY_HAS(api, create_entity), "size check accepts members the host provides");
        check(!TINY_PROJECT_GAMEPLAY_HAS(nullptr, create_entity), "size check rejects a missing table");

        // ---- 实体生命周期 ----
        const TinyProjectWorld hostWorld = bridge.getWorld();
        TinyProjectEntity entity{};
        const TinyProjectTransform spawn{{1.0f, 2.0f, -3.0f}, {0.0f, 45.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
        check(api->create_entity(api->host_context, span("Spawned"), &spawn, &entity) == TINY_PROJECT_OK,
              "create_entity succeeds during play");
        check(entity.world_id == hostWorld.id && entity.value != 0, "new entity carries the current world id");
        check(world.isAlive(entity.value), "created entity exists in the world");
        TinyProjectTransform read{};
        check(bridge.GetAPI().get_transform(bridge.GetAPI().host_context, entity, &read) == TINY_PROJECT_OK &&
                  std::fabs(read.position.y - 2.0f) < 0.0001f,
              "created entity keeps the requested transform");
        TinyProjectEntity found{};
        check(bridge.GetAPI().find_entity(bridge.GetAPI().host_context, hostWorld, span("Spawned"), &found) ==
                      TINY_PROJECT_OK &&
                  found.value == entity.value,
              "created entity is findable by name");

        // ---- 射线查询 ----
        TinyProjectEntity wall{};
        check(api->create_entity(api->host_context, span("Wall"), nullptr, &wall) == TINY_PROJECT_OK,
              "wall entity created");
        const TinyProjectVec3 wallCenter{0.0f, 1.0f, -5.0f};
        TinyProjectTransform wallTransform{{wallCenter.x, wallCenter.y, wallCenter.z}, {0.0f, 0.0f, 0.0f},
                                           {1.0f, 2.0f, 1.0f}};
        bridge.GetAPI().set_transform(bridge.GetAPI().host_context, wall, &wallTransform);
        check(api->set_box_collider(api->host_context, wall, {0.5f, 1.0f, 0.5f}, 0) == TINY_PROJECT_OK,
              "box collider attached to wall");
        TinyProjectHit hit{};
        check(api->raycast(api->host_context, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 20.0f, {}, &hit) ==
                      TINY_PROJECT_OK &&
                  hit.has_hit == 1 && hit.entity.value == wall.value,
              "raycast reports the wall hit");
        check(hit.distance > 4.0f && hit.distance < 4.6f, "hit distance lands on the wall face");
        TinyProjectHit ignored{};
        check(api->raycast(api->host_context, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 20.0f, wall, &ignored) !=
                  TINY_PROJECT_OK,
              "raycast honours the ignored entity and misses behind it");
        TinyProjectHit invalid{};
        check(api->raycast(api->host_context, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 20.0f, {}, &invalid) ==
                  TINY_PROJECT_INVALID_ARGUMENT,
              "degenerate ray direction is rejected");

        // ---- 角色移动 ----
        TinyProjectEntity mover{};
        check(api->create_entity(api->host_context, span("Walker"), nullptr, &mover) == TINY_PROJECT_OK,
              "walker entity created");
        TinyProjectTransform moverTransform{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
        bridge.GetAPI().set_transform(bridge.GetAPI().host_context, mover, &moverTransform);
        api->set_box_collider(api->host_context, mover, {0.4f, 1.0f, 0.4f}, 0);
        TinyProjectCharacterMove move{};
        check(api->move_character(api->host_context, mover, {0.0f, 0.0f, -2.0f}, &move) == TINY_PROJECT_OK,
              "move_character reports a result");
        check(move.applied.z < -1.9f, "free space movement is applied in full");
        TinyProjectCharacterMove blocked{};
        check(api->move_character(api->host_context, mover, {0.0f, 0.0f, -3.0f}, &blocked) == TINY_PROJECT_OK,
              "blocked move still reports a result");
        check(blocked.applied.z > -3.0f, "wall stops the character before its full displacement");
        TinyProjectTransform after{};
        bridge.GetAPI().get_transform(bridge.GetAPI().host_context, mover, &after);
        check(after.position.z > -3.0f, "character never ends up inside the wall");

        // ---- UI ----
        check(api->ui_label(api->host_context, span("hud.test"), span("HEALTH 100"), {10.0f, 10.0f, 120.0f, 24.0f},
                            {1.0f, 1.0f, 1.0f, 1.0f}, 18.0f) == TINY_PROJECT_OK,
              "ui_label creates a widget");
        const UIWidget* widget = ui.Find("hud.test");
        check(widget && widget->text == "HEALTH 100", "widget text is readable from the UI system");
        check(api->ui_set_text(api->host_context, span("hud.test"), span("HEALTH 70")) == TINY_PROJECT_OK &&
                  ui.Find("hud.test")->text == "HEALTH 70",
              "ui_set_text updates an existing widget");
        check(api->ui_set_text(api->host_context, span("hud.missing"), span("x")) == TINY_PROJECT_NOT_FOUND,
              "missing widget is reported instead of silently created");
        check(api->ui_set_visible(api->host_context, span("hud.test"), 0) == TINY_PROJECT_OK &&
                  !ui.Find("hud.test")->visible,
              "ui_set_visible hides the widget");
        check(api->ui_remove(api->host_context, span("hud.test")) == TINY_PROJECT_OK && !ui.Find("hud.test"),
              "ui_remove deletes the widget");

        // ---- 缺失渲染设备时的降级 ----
        uint32_t down = 1;
        check(api->key_down(api->host_context, 41u, &down) == TINY_PROJECT_NOT_READY && down == 0,
              "key queries report NOT_READY without a render device");
        check(api->key_down(api->host_context, InputState::kMaxScancodes, &down) == TINY_PROJECT_INVALID_ARGUMENT,
              "out-of-range scancodes are rejected");
        float width = 0.0f, height = 0.0f;
        check(api->viewport_size(api->host_context, &width, &height) == TINY_PROJECT_NOT_READY,
              "viewport queries report NOT_READY without a render device");

        // ---- 一次性特效生命周期 ----
        check(api->spawn_burst(api->host_context, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
                               {1.0f, 0.0f, 0.0f, 0.0f}, 0.3f, 0.2f, 6) == TINY_PROJECT_OK,
              "spawn_burst creates a one-shot effect");
        check(!vfx.BuildParticleList().empty(), "one-shot effect produced particles");
        bridge.update(1.0f);
        check(vfx.BuildParticleList().empty(), "one-shot emitter is recycled after its lifetime");

        // ---- 复古后处理：追加成员必须可被 size 检查识别，且无设备时明确降级 ----
        check(TINY_PROJECT_GAMEPLAY_HAS(api, set_retro_post), "retro post-process member is present in the table");
        check(api->set_retro_post(api->host_context, 1, 3.0f, 32.0f) == TINY_PROJECT_NOT_READY,
              "retro post-process reports NOT_READY without a render device");
        check(api->set_retro_post(api->host_context, 1, std::nanf(""), 32.0f) == TINY_PROJECT_INVALID_ARGUMENT,
              "non-finite retro parameters are rejected");

        // Ending write access is the common stop, failed-begin, and shutdown boundary.
        ParticleEmitterSettings hostSettings;
        hostSettings.emissionRate = 0.0f;
        const auto hostEmitter = vfx.createEmitter(hostSettings);
        vfx.findEmitter(hostEmitter)->burst(1);
        ui.AddLabel("host.hud", "Host", {0, 0, 40, 20});
        check(api->ui_label(api->host_context, span("host.hud"), span("Updated host"), {0, 0, 40, 20},
                            {1, 1, 1, 1}, 18) == TINY_PROJECT_OK, "project can update an existing host widget");
        check(api->ui_label(api->host_context, span("session.hud"), span("Session"), {0, 0, 40, 20},
                            {1, 1, 1, 1}, 18) == TINY_PROJECT_OK, "project creates a session widget");
        check(api->ui_panel(api->host_context, span("session.panel"), {0, 0, 40, 20}, {1, 1, 1, 1}) ==
                  TINY_PROJECT_OK, "project creates a session panel");
        check(api->ui_label(api->host_context, span("replaced.hud"), span("Session"), {0, 0, 40, 20},
                            {1, 1, 1, 1}, 18) == TINY_PROJECT_OK, "project creates a replaceable widget");
        ui.Remove("replaced.hud");
        ui.AddLabel("replaced.hud", "Host replacement", {0, 0, 40, 20});
        check(api->spawn_burst(api->host_context, {0, 0, 0}, {1, 1, 1, 1}, {1, 0, 0, 0}, 0.3f, 10.0f, 6) ==
                  TINY_PROJECT_OK, "long-lived burst starts before stop");
        bridge.setWorldAccess(true, false);
        check(vfx.findEmitter(hostEmitter) && vfx.BuildParticleList().size() == 1,
              "stop removes project bursts immediately while preserving host emitters");
        check(!ui.Find("session.hud") && !ui.Find("session.panel"), "stop removes project-created UI");
        check(ui.Find("host.hud") && ui.Find("replaced.hud") && ui.Find("replaced.hud")->text == "Host replacement",
              "stop preserves existing host widgets and replacements with the same id");
        check(api->spawn_burst(api->host_context, {0, 0, 0}, {1, 1, 1, 1}, {1, 0, 0, 0}, 0.3f, 1.0f, 1) ==
                  TINY_PROJECT_READ_ONLY, "edit mode rejects project burst creation");
        check(api->ui_remove(api->host_context, span("host.hud")) == TINY_PROJECT_READ_ONLY,
              "edit mode rejects project UI mutation");
        bridge.setWorldAccess(true, true);
        check(api->spawn_burst(api->host_context, {0, 0, 0}, {1, 1, 1, 1}, {1, 0, 0, 0}, 0.3f, 10.0f, 3) ==
                  TINY_PROJECT_OK, "next session can create a new burst");
        bridge.setWorldAccess(false, false);
        bridge.setWorldAccess(false, false);
        check(vfx.findEmitter(hostEmitter) && vfx.BuildParticleList().size() == 1,
              "shutdown cleanup is repeatable and preserves host emitters");
        bridge.setWorldAccess(true, true);

        // A shorter or partially populated SDK table must degrade without calling a null member.
        TinyProjectGameplayAPI partial{};
        partial.abi_version = TINY_PROJECT_GAMEPLAY_VERSION;
        partial.struct_size = sizeof(partial);
        check(!TINY_PROJECT_GAMEPLAY_HAS(&partial, set_retro_post), "missing optional callback is unavailable");
        partial.struct_size = offsetof(TinyProjectGameplayAPI, set_retro_post);
        check(!TINY_PROJECT_GAMEPLAY_HAS(&partial, set_retro_post), "older extension omits the appended member");
        TinyProjectHostAPI partialHost{};
        partialHost.host_context = &partial;
        partialHost.query_interface = [](void* p, TinyProjectString, uint32_t, const void** out) -> TinyProjectResult
        {
            *out = p;
            return TINY_PROJECT_OK;
        };
        partial.abi_version = TINY_PROJECT_GAMEPLAY_VERSION + 1;
        project_sdk::Context incompatible(partialHost, hostWorld);
        check(!incompatible.hasGameplay(), "SDK rejects an incompatible gameplay table version");
        partial.abi_version = TINY_PROJECT_GAMEPLAY_VERSION;
        partial.struct_size = offsetof(TinyProjectGameplayAPI, host_context);
        project_sdk::Context truncated(partialHost, hostWorld);
        check(!truncated.hasGameplay(), "SDK rejects an incomplete gameplay table header");
        partial.struct_size = sizeof(partial);
        project_sdk::Context optional(partialHost, hostWorld);
        check(!optional.setRetroPostProcess(true) && optional.getLastResult() == TINY_PROJECT_UNSUPPORTED,
              "SDK safely degrades a null optional callback");
        // ---- 销毁与句柄失效 ----
        check(api->destroy_entity(api->host_context, entity) == TINY_PROJECT_OK, "destroy_entity succeeds");
        uint32_t alive = 1;
        check(bridge.GetAPI().is_alive(bridge.GetAPI().host_context, entity, &alive) == TINY_PROJECT_NOT_FOUND &&
                  alive == 0,
              "destroyed entity handle is rejected");
        check(api->destroy_entity(api->host_context, entity) == TINY_PROJECT_NOT_FOUND,
              "destroying the same handle twice is rejected");

        std::cout << "ALL GAMEPLAY BRIDGE CHECKS PASSED (" << checks << " checks)\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cout << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
