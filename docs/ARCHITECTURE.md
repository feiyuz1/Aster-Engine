# TinyEngine 架构说明

本文对应 TinyEngine 0.5.0，最后同步于 2026-07-14。

## 依赖方向

```text
Editor / Demo / AssetTool
        |            |
   Dear ImGui   Assimp Importers
        |
Runtime / Project / UI
        |
Script | Audio | Physics | Renderer / RenderDevice | Input
        |
Scene / ECS
        |
Resource / AssetRegistry / Cooker / TPAK / VFS
        |
Task | Memory | Core | Math | Platform
```

上层可以依赖下层，下层不得访问 Runtime 或 Editor。Editor 是独立可执行目标；核心库不依赖 Dear ImGui。Assimp 只属于 Authoring，发布 Runtime 不链接 Assimp。

## UE 5.3.2 职责映射

| TinyEngine | UE 参考职责 | 删减策略 |
|---|---|---|
| Project/SceneManager/GameInstance | Projects、UGameInstance、UWorld | 小型配置和显式生命周期，无 UObject |
| ECS/Scene/Prefab | Actor、Component、Level | generation Entity + 稀疏组件存储 |
| AssimpImporter/Registry/Cooker/TPAK | Interchange、AssetRegistry、Cook、PakFile | Authoring 转换为轻量运行时格式 |
| Renderer/RenderDevice | Renderer、RenderCore、RHI | 当前仅 OpenGL 3.3 后端 |
| PhysicsWorld | Chaos/PhysicsCore | 自研基础后端，公开 API 保持可替换 |
| UISystem | UMG/Slate Runtime | 小型 Widget、命中和焦点模型 |
| Editor | UnrealEd/LevelEditor | Dear ImGui 独立工具，只覆盖高频工作流 |

## 关键数据流

```text
FBX/glTF/OBJ/... -> AssimpImporter
  -> .mesh/.mat/.prefab + .meta
  -> ResourceManager -> GPU upload
  -> Cooked directory -> TPAK -> VFS mount
```

材质导入保留 base color、opacity、metallic、roughness、emissive 和 base-color texture。PNG/JPG/BMP/TIFF 由 Windows WIC 解码；Radiance HDR 当前在资源加载阶段转为 RGBA8，因此还不是线性浮点 HDR 渲染管线。

```text
SDL events -> semantic input / ImGui
-> GameInstance / Script / Systems
-> fixed physics
-> render collection / culling / sort
-> OpenGL Scene FBO
-> Scene View texture + editor overlay
-> present
```

编辑态由 Renderer 的 camera override 使用编辑器相机；Play 时恢复场景 Primary Camera。Scene View 从离屏颜色纹理显示，拾取在 CPU 侧将屏幕坐标转为射线并检测 Mesh/Collider AABB；Gizmo 直接修改 Transform 并接入 Undo。

## 数据格式与约束

- `TINYSCENE 3`：场景和 Prefab 文本格式。
- `.mat`：`color`、`metallic`、`roughness`、`emissive`、`texture`、`blend`。
- `<asset>.meta`：32 位十六进制 GUID、类型、导入时间、设置和依赖。
- `TPAK 1`：路径与 payload 的简单归档。
- 新持久化组件必须同步 SceneSerializer 和测试。
- 新资源格式必须通过导入/资源层进入，渲染器不解析源模型文件。
- 新图形后端实现 `RenderDevice`；游戏世界不能直接依赖 SDL/OpenGL。
- 成熟物理后端应复用 Collider、查询和事件 API。
