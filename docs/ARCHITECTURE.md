# TinyEngine 架构

本页是架构速查；模块接口、数据格式、线程模型和验证基线以 [主技术设计文档](../设计.md) 为准。

## 依赖方向

```text
Runtime / Tools
  -> Script | Audio | Animation | Physics | Render
  -> Scene / ECS
  -> Resource / AssetRegistry / Pak
  -> Task | Memory | Core | Math
  -> Platform
```

上层可以依赖下层，下层不访问 Runtime。SDL 仅作为窗口、输入、音频和 OpenGL Context 后端；World、Resource 和 Task 不依赖 SDL。

## 与 UE 模块的对应

| TinyEngine | UE 参考边界 | 删减策略 |
|---|---|---|
| Core/Platform/Math | Core、ApplicationCore | 使用 STL 和 SDL，不复制 UE 容器/HAL 规模 |
| ECS/Scene | CoreUObject、Engine World/Actor | generation Entity + 组件，不做 UObject GC/UHT |
| Resource/AssetRegistry/Pak | AssetRegistry、PakFile、Engine Asset | 单进程注册表和 TPAK，不做 DDC/IoStore/Cook 集群 |
| RenderDevice/Renderer | RHI、RenderCore、Renderer | 单 OpenGL 3.3 后端，不做多 RHI、RenderGraph 和高级 GI |
| PhysicsWorld | PhysicsCore、Chaos | 基础刚体/碰撞接口，不做破坏、布料和流体 |
| AudioEngine | AudioMixer | 单设备队列混音基础，不做 MetaSounds |
| AnimationSystem | AnimationCore、AnimGraphRuntime | Transform Clip，不做完整骨骼图编辑器 |
| ScriptEngine | BlueprintRuntime | 原生工厂和实例热重建，不引入 Blueprint VM |

## 帧流程

```text
PumpEvents
-> InputSystem
-> Script/Gameplay/Animation variable update
-> Physics fixed update (1/60 accumulator)
-> RenderQueue build
-> OpenGL submit/present
-> frame allocator reset / periodic resource GC
```

## 资产流程

```text
source file
-> AssetRegistry scan
-> ResourceManager cache/async task
-> Mesh/Material/Texture parser
-> Renderer lazy GPU upload
-> reference-count GC

assets directory -> tinyengine_asset_tool -> TPAK
```

## 扩展点

- 新组件：结构体 + SceneSerializer 字段处理。
- 新系统：继承 `System`，在 World 初始化前注册。
- 新资源：继承 `Resource`，实现 `Load/Unload`。
- 新脚本：向 `ScriptEngine` 注册 Factory。
- 新模块：实现 `IModule` 并交给 `ModuleManager` 管理。
- 新渲染后端：实现 `RenderDevice`，Renderer 不需要修改。
