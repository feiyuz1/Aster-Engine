/* Project gameplay extension ABI.
 *
 * 该表通过 TinyProjectHostAPI::query_interface("aster.gameplay", 1, &out) 取得，
 * 不改变 TinyProjectHostAPI 的版本、布局或既有成员，因此旧模块可以完全忽略它。
 *
 * 兼容策略：
 *  - 新增能力一律追加到结构体尾部，TINY_PROJECT_GAMEPLAY_VERSION 只在语义破坏时提升。
 *  - 使用方在调用任何成员前用 TINY_PROJECT_GAMEPLAY_HAS 检查 struct_size，
 *    因此新宿主 + 旧头文件、旧宿主 + 新模块都不会越界读取。
 *  - 字符串仍为借用 UTF-8 跨度，实体句柄仍带世界身份和代次。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "tinyengine/project_sdk/project_api.h"

#define TINY_PROJECT_GAMEPLAY_INTERFACE "aster.gameplay"
#define TINY_PROJECT_GAMEPLAY_VERSION 1u

/* 命中查询结果；has_hit 为 0 时其余字段保持零值。 */
typedef struct TinyProjectHit
{
    uint32_t has_hit;
    uint32_t reserved;
    TinyProjectEntity entity;
    TinyProjectVec3 point;
    TinyProjectVec3 normal;
    float distance;
} TinyProjectHit;

/* 角色移动的最终结果：调用方必须使用 applied 位移更新表现层。 */
typedef struct TinyProjectCharacterMove
{
    TinyProjectVec3 applied;
    TinyProjectVec3 ground_normal;
    uint32_t grounded;
    uint32_t hit_ceiling;
    uint32_t stepped;
} TinyProjectCharacterMove;

typedef struct TinyProjectUIRect
{
    float x;
    float y;
    float width;
    float height;
} TinyProjectUIRect;

typedef struct TinyProjectColor
{
    float r;
    float g;
    float b;
    float a;
} TinyProjectColor;

#define TINY_PROJECT_LIGHT_DIRECTIONAL 0u
#define TINY_PROJECT_LIGHT_POINT 1u
#define TINY_PROJECT_LIGHT_SPOT 2u

/* 宿主拥有该表；所有回调都在主线程调用且不抛异常。 */
typedef struct TinyProjectGameplayAPI
{
    uint32_t abi_version;
    uint32_t struct_size;
    void* host_context;

    /* 实体生命周期。position 为空时使用原点。 */
    TinyProjectResult(TINY_PROJECT_CALL* create_entity)(void*, TinyProjectString name, const TinyProjectTransform*,
                                                       TinyProjectEntity*);
    TinyProjectResult(TINY_PROJECT_CALL* destroy_entity)(void*, TinyProjectEntity);
    /* 渲染与碰撞表现。路径为项目资产相对路径；空跨度为未设置。 */
    TinyProjectResult(TINY_PROJECT_CALL* set_mesh)(void*, TinyProjectEntity, TinyProjectString mesh,
                                                   TinyProjectString material);
    TinyProjectResult(TINY_PROJECT_CALL* set_box_collider)(void*, TinyProjectEntity, TinyProjectVec3 half_extents,
                                                           uint32_t trigger);
    TinyProjectResult(TINY_PROJECT_CALL* set_light)(void*, TinyProjectEntity, uint32_t type, TinyProjectVec3 color,
                                                    float intensity, TinyProjectVec3 direction, float range);

    /* 场景查询与角色移动。ignored 用于排除射击者自身。 */
    TinyProjectResult(TINY_PROJECT_CALL* raycast)(void*, TinyProjectVec3 origin, TinyProjectVec3 direction,
                                                  float max_distance, TinyProjectEntity ignored, TinyProjectHit*);
    TinyProjectResult(TINY_PROJECT_CALL* move_character)(void*, TinyProjectEntity, TinyProjectVec3 displacement,
                                                         TinyProjectCharacterMove*);

    /* 输入。scancode 采用 SDL2 编号，与 InputState::kMaxScancodes 对应。 */
    TinyProjectResult(TINY_PROJECT_CALL* key_down)(void*, uint32_t scancode, uint32_t*);
    TinyProjectResult(TINY_PROJECT_CALL* key_pressed)(void*, uint32_t scancode, uint32_t*);
    TinyProjectResult(TINY_PROJECT_CALL* primary_down)(void*, uint32_t*);
    TinyProjectResult(TINY_PROJECT_CALL* primary_pressed)(void*, uint32_t*);
    TinyProjectResult(TINY_PROJECT_CALL* cursor_position)(void*, float* x, float* y);
    TinyProjectResult(TINY_PROJECT_CALL* viewport_size)(void*, float* width, float* height);
    TinyProjectResult(TINY_PROJECT_CALL* set_mouse_capture)(void*, uint32_t captured);

    /* 运行时 UI。label/panel 为“不存在则创建，存在则更新”。 */
    TinyProjectResult(TINY_PROJECT_CALL* ui_label)(void*, TinyProjectString id, TinyProjectString text,
                                                   TinyProjectUIRect rect, TinyProjectColor color, float font_size);
    TinyProjectResult(TINY_PROJECT_CALL* ui_panel)(void*, TinyProjectString id, TinyProjectUIRect rect,
                                                   TinyProjectColor color);
    TinyProjectResult(TINY_PROJECT_CALL* ui_set_text)(void*, TinyProjectString id, TinyProjectString text);
    TinyProjectResult(TINY_PROJECT_CALL* ui_set_rect)(void*, TinyProjectString id, TinyProjectUIRect rect);
    TinyProjectResult(TINY_PROJECT_CALL* ui_set_color)(void*, TinyProjectString id, TinyProjectColor color);
    TinyProjectResult(TINY_PROJECT_CALL* ui_set_visible)(void*, TinyProjectString id, uint32_t visible);
    TinyProjectResult(TINY_PROJECT_CALL* ui_remove)(void*, TinyProjectString id);

    /* 音频与特效。音效路径同样按项目资产相对路径解析。 */
    TinyProjectResult(TINY_PROJECT_CALL* play_sound)(void*, TinyProjectString path, float volume);
    /* 一次性粒子爆发，用于枪口闪光、命中火花与爆炸；由宿主按生命周期自动回收。 */
    TinyProjectResult(TINY_PROJECT_CALL* spawn_burst)(void*, TinyProjectVec3 position, TinyProjectColor start,
                                                      TinyProjectColor end, float radius, float lifetime,
                                                      uint32_t count);

    /* 复古像素化后处理（追加成员，旧宿主缺少时按 HAS 宏降级）。
     * block_size 为像素块边长（>=1），color_levels 为每通道色阶数（2..256）。
     * 该通道独立于 TAA/Bloom，只作用于色调映射之后的最终画面。 */
    TinyProjectResult(TINY_PROJECT_CALL* set_retro_post)(void*, uint32_t enabled, float block_size,
                                                         float color_levels);
} TinyProjectGameplayAPI;

/* 老宿主（struct_size 更小）缺少该成员时返回 0，调用方必须据此降级。 */
#define TINY_PROJECT_GAMEPLAY_HAS(api, field)                                                            \
    ((api) != NULL &&                                                                                    \
     ((const TinyProjectGameplayAPI*)(api))->struct_size >=                                              \
         (uint32_t)(offsetof(TinyProjectGameplayAPI, field) + sizeof(((TinyProjectGameplayAPI*)0)->field)) && \
     ((const TinyProjectGameplayAPI*)(api))->field != NULL)
