# TinyEngine 架构说明

本文对应 TinyEngine 0.10.0，最后同步于 2026-07-14。

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
| UISystem | UMG/Slate Runtime | Widget、字体 Atlas、布局、命中和焦点模型 |
| Editor | UnrealEd/LevelEditor | Dear ImGui 独立工具，只覆盖高频工作流 |

## 关键数据流

```text
FBX/glTF/OBJ/... -> AssimpImporter
  -> .mesh/.mat/.prefab + .meta
  -> ResourceManager -> GPU upload
  -> Cooked directory -> TPAK -> VFS mount
```

发布流程：

```text
.tinyproject + assets + startup scene + tinyengine_game
-> ProjectPackager safety validation
-> Content/Game.tpak + renamed game executable + shipping .tinyproject + manifest
-> game mounts TPAK at startup -> bounded or interactive Engine loop
```

材质导入保留 base color、opacity、metallic、roughness、emissive，以及 base-color/normal/metallic/roughness/AO/emissive 贴图。MeshResource 从三角形 UV 生成正交 Tangent。PNG/JPG/BMP/TIFF 由 Windows WIC 解码；Radiance HDR 保持线性 float RGBA，上传为 RGBA16F。颜色贴图使用 sRGB，数据贴图保持线性，全部生成 Mipmap。

```text
SDL events -> semantic input / ImGui
-> GameInstance / Script / Systems
-> fixed physics
-> render collection / culling / sort
-> directional shadow depth pass
-> OpenGL RGBA16F Scene FBO
-> ACES Tonemap to Viewport texture
-> Scene View texture + editor overlay
-> present
```

编辑态由 Renderer 的 camera override 使用编辑器相机；Play 时恢复场景 Primary Camera。Scene View 显示 Tonemap 后的颜色纹理，拾取在 CPU 侧将屏幕坐标转为射线并检测 Mesh/Collider AABB；Gizmo 修改 Transform、支持步长吸附并接入 Undo。资产浏览器通过 ImGui payload 将 Prefab、Mesh 或 Material 拖入 Scene View。

默认 PBR Shader 是独立 `.vert/.frag` 资产。RenderDevice 监测修改时间并尝试原子替换 OpenGL program；编译或链接失败时继续使用上一份有效 program。每帧最多收集四盏方向光，第一盏灯使用固定 2048² 深度图和 3×3 PCF；其余灯只计算直接 PBR 光照。Tonemap Pass 在无几何覆盖的像素绘制程序化天空渐变，并为物体提供半球环境近似。

Runtime UI 在提交前根据 Viewport 解析锚点和父子容器布局，生成裁剪后的矩形、图片与文本命令。OpenGL UI Program 使用动态 VBO 批量提交四边形；Windows 运行时用 GDI 构建固定 ASCII Atlas，Label/Button 采样 Atlas，Image 复用 TextureResource。Tab/Enter/Space 使用按下沿驱动焦点和激活。

编辑器属性面板不直接依赖具体组件类型。`ReflectionRegistry` 登记组件存在性、增删回调、字段访问器、枚举项、数值范围和资产类型，Inspector 据此生成通用控件。资产浏览器拖放路径时先经 `AssetRegistry` 校验类型。编辑操作进入序列化快照 Undo 栈并设置脏标记；未保存场景每 30 秒写入项目 `Saved/EditorAutosave.scene`，启动后可显式恢复，正常保存会删除恢复文件。

## 数据格式与约束

- `TINYSCENE 5`：场景和 Prefab 文本格式，增加 Joint、物理材质参数、动画过渡和空间音频字段，并继续读取 1–4 版。
- `TINYINPUT 1`：逻辑 Axis/Action 到物理输入通道的多重映射。
- `.mat`：`color`、`metallic`、`roughness`、`emissive`、`texture`、`blend`。
- `<asset>.meta`：32 位十六进制 GUID、类型、导入时间、设置和依赖。
- `TPAK 1`：路径与 payload 的简单归档。
- 新持久化组件必须同步 SceneSerializer 和测试。
- 新资源格式必须通过导入/资源层进入，渲染器不解析源模型文件。
- 新图形后端实现 `RenderDevice`；游戏世界不能直接依赖 SDL/OpenGL。
- 成熟物理后端应复用 Collider、查询和事件 API。
