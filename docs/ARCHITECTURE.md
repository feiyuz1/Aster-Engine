# TinyEngine 架构说明

本文对应 TinyEngine 0.4.0，最后同步于 2026-07-14。

## 依赖方向

```text
Editor / Demo / AssetTool
        |            |
   Dear ImGui   Assimp Importers
        |
Runtime / Project / UI
        |
Script | Audio | Physics | Render | Input
        |
Scene / ECS
        |
Resource / AssetRegistry / Cooker / TPAK / VFS
        |
Task | Memory | Core | Math | Platform
```

上层可以依赖下层，下层不得访问 Runtime 或 Editor。编辑器是单独可执行目标；`tinyengine` 核心库不依赖 Dear ImGui。

## UE 5.3.2 职责映射

| TinyEngine | UE 参考职责 | 删减策略 |
|---|---|---|
| Project/SceneManager/GameInstance | Projects、UGameInstance、World | 小型配置与显式生命周期，无 UObject |
| ECS/Scene/Prefab | Actor、Component、Level | generation Entity + 稀疏组件存储 |
| AssimpImporter/AssetRegistry/Cooker/TPAK/VFS | Interchange、AssetRegistry、Cook、PakFile | Authoring 时转换，Runtime 不链接 Assimp |
| Renderer/RenderDevice | Renderer、RenderCore、RHI | 单 OpenGL 3.3 后端 |
| PhysicsWorld | Chaos/PhysicsCore | 自研基础后端，API 可替换 Jolt |
| UISystem | UMG/Slate Runtime | 小型 Widget、命中和焦点模型 |
| Editor | UnrealEd/LevelEditor | Dear ImGui 独立目标，只覆盖常用场景工作流 |

## 关键流程

编辑态与运行态：

```text
Edit World -> Serialize snapshot -> EnterPlay
          -> rebuild script instances -> runtime update
Stop      -> destroy runtime instances -> restore snapshot -> Edit World
```

资产：

```text
FBX/glTF/OBJ/... -> AssimpImporter -> .mesh/.mat/.prefab + .meta GUID/dependencies
                                    -> cooked directory
             -> TPAK -> PlatformFile mount -> ResourceManager -> GPU upload
```

每帧：

```text
SDL events -> semantic input + UI hit test
-> GameInstance/Script/Systems
-> fixed physics (SAP broadphase)
-> render collection/culling/sort
-> opaque + transparent + runtime UI + editor overlay
-> present / frame-memory reset / resource GC
```

## 数据格式

- `TINYSCENE 3`：文本场景和 Prefab，保存层级及可持久组件。
- `.tinyproject`：项目名、资产目录、启动场景、Cook 目录、固定步长。
- `<asset>.meta`：32 位十六进制 GUID、类型、导入时间、设置和依赖。
- `TPAK 1`：路径与 payload 的简单二进制归档。

## 扩展约束

- 新持久组件必须同步 SceneSerializer 与测试。
- 新资源格式通过导入/资源层进入，不允许渲染器直接解析源文件。
- `tinyengine_importers` 只允许被 Editor、AssetTool 和导入测试依赖，Runtime 发布不携带 Assimp。
- 新图形后端实现 `RenderDevice`；游戏世界不接触 SDL/OpenGL。
- 成熟物理后端应复用 Collider、PhysicsQuery 与事件 API。
- Editor 代码不得链接回核心静态库。
