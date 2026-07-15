# TinyEngine 架构说明

本文对应 TinyEngine 0.24.0，最后同步于 2026-07-16。

## 依赖方向

```text
Editor / Demo / AssetTool
        |            |
   Dear ImGui   Assimp Importers
        |
Runtime / Project / UI
        |
Gameplay | Script | Audio | Physics | Renderer / RenderDevice | Input
        |
Scene / ECS
        |
AssetStreaming / Resource / AssetRegistry / TargetPlatform Cooker / TPAK / VFS
        |
Automation | Diagnostics | Serialization | Task | Memory | Core | Math | Platform
```

上层可以依赖下层，下层不得访问 Runtime 或 Editor。Editor 是独立可执行目标；核心库不依赖 Dear ImGui。Assimp 只属于 Authoring，发布 Runtime 不链接 Assimp。

0.14 将跨系统的数据读写、诊断、平台 Cook 和自动化从具体业务类中抽出。`Engine` 仍是组合根，新增 `GameplayFramework`、`AssetStreamingManager` 与 `DiagnosticsService` 由 Engine 显式持有，初始化和销毁顺序保持可检查。

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
| Serialization/Migration | Archive、Custom Version | 小端二进制 Archive、版本化键值文档和逐版本迁移注册表 |
| Gameplay/Diagnostics/Automation | Gameplay Framework、Console/Stats、Commandlet | ECS 角色控制、Timer/GameMode、CVar/Stat 与无头批处理 |
| TargetPlatform | TargetPlatform、CookCommandlet | Windows/Linux Profile、资产过滤、Shader 变体缓存和平台清单 |

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

编辑器的场景标签保存各自的序列化快照、路径和脏状态；切换标签时捕获当前 World，再恢复目标 World，不让多个 World 同时占用运行时系统。Material 资产在 Inspector 直接编辑 PBR 因子、混合模式、Shader 与贴图路径，保存后通过 ResourceManager 原位 Reload。

Logger 可同时输出控制台和逐行 JSON；CrashReporter 在 Windows 注册 Structured Exception Filter 并写 MiniDump/文本上下文，在 `std::terminate` 路径至少写文本报告。CI 使用无 Editor/Assimp 的最小运行时配置验证公共核心，完整本地发布通过 `tools/ReleaseSmoke.ps1` 构建、CTest、打包并运行三帧。

默认 PBR Shader 是独立 `.vert/.frag` 资产。RenderDevice 监测依赖修改并尝试原子替换 OpenGL Program；编译或链接失败时继续使用上一份有效 Program。每帧最多收集四盏方向光，第一盏灯使用三段 2048² CSM 和 3×3 PCF；透明阴影通过 Alpha 覆盖率抖动。离线 Cubemap 六面 Atlas 提供 Irradiance、六级 GGX Prefilter 和 BRDF LUT；HDR 合成执行 Bloom、SSAO、TAA、FXAA 与 ACES。

Runtime UI 在提交前根据 Viewport 解析锚点和父子容器布局，生成裁剪后的矩形、图片与文本命令。TextLayout 负责 UTF-8、BiDi Run、连字、自动断行和富文本样式；OpenGL 后端按需把 Codepoint 栅格化进 1024² 动态 Atlas，Windows 使用 GDI，非 Windows 使用 FreeType。InputField、List、Dropdown、ProgressBar 与 Button 共用焦点和回调模型。

Assimp 的 CPU/文件转换通过 `AssetImportQueue` 提交到 JobSystem，编辑器只轮询原子状态与受锁保护的结果，不阻塞 UI 帧。取消是协作式的：排队阶段立即终止，Assimp 已进入解析后在产物提交点转为 Cancelled。AssetCooker 用 `TINYCOOK 1` 清单比较相对路径、时间戳和大小，仅复制变化文件、删除清单中的陈旧产物，再重建 TPAK。

`AssetStreamingManager` 在 `ResourceManager` 之上返回类型化 `AssetHandle<T>`，用 `.meta` 依赖构造传递闭包，并按最近访问、Pin 状态和字节预算淘汰只剩缓存引用的资源。它不越过 ResourceManager 直接销毁仍被调用方持有的对象。

`GameplayFramework` 管理 GameMode、Timer 和玩家 Pawn；`CharacterControllerComponent` 将语义输入转换为速度，再调用 `PhysicsWorld::MoveCharacterController` 得到台阶、接地和碰顶结果。通用运行逻辑不再硬编码在 `Engine::Tick`。Diagnostics 提供类型化 CVar、命令和线程安全 Stat；Engine 注册时间缩放、FXAA、CPU/GPU 资源预算和资源回收，并逐帧写入渲染、资源和 Job 统计。

无头 `tinyengine_commandlet` 当前提供 `ValidateAssets`、`ValidateScenes`、`MigrateScenes` 和 `CookProject`。TargetPlatform Cook 对源资产执行平台过滤，将 Mesh/Texture 编译为确定性的运行时二进制，离线展开 Shader variant，并生成 `Game.tpak` 和版本化 `PlatformCook.manifest`。TextureResource 可直接消费 DDS/KTX2 的 BC1–BC7 层级并原样上传 GPU；离线编码质量由目标平台工具决定。

`RenderDevice` 统一暴露 GPU 驻留字节数、预算设置和缓存回收。OpenGL 后端统计 Mesh buffer 与 Texture payload；超预算时释放可重建的缓存对象，Diagnostics 通过 `r.GpuBudgetMB`、`r.TrimGpu` 和 `render.gpu_resident_bytes` 提供运行时控制与观测。

骨骼动画层由 Skeleton、SkeletalClip、独立 Position/Rotation/Scale 压缩曲线、事件游标、1D Blend Tree、Animation Graph Player 和蒙皮矩阵生成组成，不依赖渲染后端。Assimp 在 Authoring 阶段导出 Skeleton/Clip，运行时只读取轻量资产；AnimationSystem 采样并向 RenderItem 提交逐实体 Skin Matrices，OpenGL 完成四权重 GPU Skinning。VfxSystem 由 Engine 统一驱动 CPU 生命周期仿真，Renderer 汇总粒子，OpenGL 后端用相机 Right/Up 构造世界空间 Billboard，并以软边圆形透明片元提交。本地化层严格校验 UTF-8，使用 `TINYLOC 1` 字符串表完成语言回退和参数替换。

`NavigationGrid` 使用有代价 Cell 上的 A* 搜索，并在对角移动时检查相邻正交 Cell，避免穿过障碍角。AI 行为树保存运行中的组合节点游标，因此 Wait/Action 可跨帧继续；Blackboard 是类型受限的运行时共享状态。它们当前是游戏可直接使用的轻量实现，不等同于 Recast NavMesh、EQS 或可视化 Behavior Tree 编辑器。

`WorldPartition` 只负责依据 XZ 距离和迟滞阈值决定 Cell 加载/卸载，具体场景合并通过回调交给游戏或资产层。`ModuleManager` 根据显式依赖拓扑启动，启动失败会逆序回滚；Engine 在核心服务就绪后启动模块，在子系统关闭前逆序停止模块。

联网数据层使用 `TNYNET1` 小端快照保留完整 generation Entity、位置、旋转和速度。`SnapshotBuffer` 按服务器时间排序并对相同实体插值；`ReplayStream` 复用完全相同的快照编码写入 `TNYRPLY1`，避免回放另造一套状态格式。当前没有 Socket/RPC/Ownership/客户端预测，不能据此宣称完整联机后端。

Sequencer 的 Transform Track 与事件 Player 是后端无关的时间采样层；高度场负责采样、法线与静态 Mesh 数据生成；ImageSequencePlayer 只推进帧路径，不在媒体层直接依赖 OpenGL。视频解码、音画同步、地形 LOD/植被和 Sequencer 编辑器仍由后续模块承接。

编辑器属性面板不直接依赖具体组件类型。`ReflectionRegistry` 登记组件存在性、增删回调、字段访问器、枚举项、数值范围和资产类型，Inspector 据此生成通用控件。资产浏览器拖放路径时先经 `AssetRegistry` 校验类型。编辑操作进入序列化快照 Undo 栈并设置脏标记；未保存场景每 30 秒写入项目 `Saved/EditorAutosave.scene`，启动后可显式恢复，正常保存会删除恢复文件。

## 数据格式与约束

- `TINYSCENE 10`：RigidBody 增加角速度、摩擦、惯量和旋转锁，Joint 增加 Hinge/Spring、限制与 Break Force；继续读取 1–9 版。
- `TINY_SKELETON 1` / `TINY_SKANIM 2`：骨骼层级与 Inverse Bind；Clip v2 支持 raw 完整 Pose、逐骨骼独立 P/R/S 压缩曲线和事件，并兼容读取 v1。
- `TINY_BLENDTREE 1`：按单个浮点参数排列阈值样本，在相邻 SkeletalClip 间插值。
- `TINY_ANIMGRAPH 1`：参数、状态、Motion 路径、比较条件、转换和交叉淡化时间。
- `TINYARCHIVE`：带格式名、版本号和有序字段的通用文本 Archive；二进制 Archive 固定使用小端编码和有界字符串。
- `TinyEnginePlatformCook 1`：平台、配置、纹理/Shader 格式、启动场景和变体数量清单。
- `TNYTEX1` / `TNYMSH2`：平台 Cook 产出的 Texture/Mesh 小端运行时二进制；Mesh v2 增加四骨骼索引和归一化权重，并兼容读取 v1。
- `TINYLOC 1`：按 locale/key 组织、支持回退和参数替换的本地化字符串表。
- `TNYNET1` / `TNYRPLY1`：网络实体快照与复用同一快照格式的离线回放流。
- `TINYINPUT 1`：逻辑 Axis/Action 到物理输入通道的多重映射。
- `.mat`：`color`、`metallic`、`roughness`、`emissive`、`texture`、`packedTexture`、`packedLayout ORM|RMA`、`parameter`、`blend`。
- `.mat` 可选 `parent <path>` 与 `override <property> 0|1`：先递归加载父材质，再仅应用启用的实例覆盖；规范化路径集合阻止继承环。
- `TINY_ENV 1`：Cubemap 六面辐照度 Atlas、六级 GGX 预过滤六面 Atlas 和 BRDF LUT 的相对路径清单。
- `TINY_MATGRAPH 1`：材质节点、位置和值以及有向输入连线。
- `TINYCHUNK 1`：大型资产总大小、块大小、块文件名和每块长度。
- `<asset>.meta`：32 位十六进制 GUID、类型、导入时间、设置和依赖。
- `TPAK 1`：路径与 payload 的简单归档。
- 新持久化组件必须同步 SceneSerializer 和测试。
- 新资源格式必须通过导入/资源层进入，渲染器不解析源模型文件。
- 新图形后端实现 `RenderDevice`；游戏世界不能直接依赖 SDL/OpenGL。
- 成熟物理后端应复用 Collider、查询和事件 API。
