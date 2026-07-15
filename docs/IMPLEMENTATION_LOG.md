# TinyEngine 实现更新日志

本文记录本轮开发对 TinyEngine 做过的主要更新，以及各功能采用的简要实现方式。内容按 Git 提交顺序整理，方便以后定位代码、回溯设计和继续开发。

当前引擎版本：0.21.0

参考源码：Unreal Engine 5.3.2

最后同步：2026-07-15

## 1. 初始化引擎工程

提交：`32fcfdd`

更新内容：

- 建立 CMake/C++17 工程、静态引擎库和 Demo。
- 划分 Core、Math、Memory、Task、Platform、Resource、ECS、Render、Script 和 Runtime 模块。
- 实现日志、事件、数学、内存分配器、JobSystem、ECS、资源管理和第一版 OpenGL 渲染。

实现方式：

- 参考 UE 的模块职责划分，但使用普通 C++ 类型和显式生命周期，未引入 UObject/UHT。
- 用 `RenderDevice` 隔离渲染接口，第一版后端使用 SDL2 创建窗口和 OpenGL Context。
- 用 generation + index 组成 Entity 句柄，避免销毁后的旧句柄继续访问新对象。

## 2. 完善运行时核心

提交：`4e8daf5`

更新内容：

- 增加 Config、Module、FileWatcher、Input、Audio、Physics 和场景序列化。
- JobSystem 增加优先级、依赖、等待和异常传递。
- ECS 增加生命周期通知和更完整的组件遍历。
- 建立 CTest 自动化测试。

实现方式：

- Job 通过依赖计数决定何时进入执行队列，任务异常保存后在 `Wait` 时重新抛出。
- 物理使用固定时间步，避免帧率变化直接改变模拟结果。
- 场景采用带版本号的文本格式，加载器保留旧版本兼容入口。

## 3. 材质、纹理和工具程序

提交：`7ed84d9`

更新内容：

- 增加 `.mat`、PPM Texture、UV 和 OpenGL 纹理采样。
- 增加 TPAK、AssetTool 和 SceneTool。

实现方式：

- Mesh、Material、Texture 分别由 ResourceManager 缓存，Renderer 只组合已经加载的资源。
- TPAK 保存规范化路径和二进制 payload，通过工具程序完成打包与查看。

## 4. 项目、场景与 Prefab

提交：`839ba7c`

更新内容：

- 增加 `.tinyproject`、ProjectSettings、GameInstance 和 SceneManager。
- 增加场景 New/Load/Save、Play/Stop 隔离、SaveGame 和 Prefab。
- 增加 Entity 父子层级。

实现方式：

- 所有项目相对路径以 `.tinyproject` 所在目录为基准解析。
- 进入 Play 前序列化编辑世界快照，停止运行后反序列化恢复，避免运行时修改污染场景。
- Prefab 保存根节点子树；实例化时重新生成 Entity 并重建内部父子映射。

## 5. 资产元数据、Cook 与 VFS

提交：`bcf7ae6`

更新内容：

- 增加稳定 GUID、`.meta`、导入设置、依赖和重新导入判断。
- 增加 AssetCooker、Cook 目录、TPAK 生成和虚拟挂载。

实现方式：

- 每个源资产使用 sidecar `.meta` 保存 GUID；移动资产时同时移动 meta 即可保持引用身份。
- AssetRegistry 比较文件时间戳判断是否需要重新导入。
- PlatformFile 按优先级查询物理目录和 TPAK，使上层 Resource 不关心实际存储位置。

## 6. P0 Runtime 与编辑器基础

提交：`c27c4b4`

更新内容：

- 物理增加 SAP、Box/Sphere/Capsule、Trigger、Raycast、SphereCast 和 Overlap。
- 增加基础 Runtime UI。
- 建立 Editor、Hierarchy、Inspector、Assets、Console、Play/Stop 和 Undo/Redo。

实现方式：

- 宽相位沿 X 轴排序包围盒端点，减少需要进入窄相位的对象对。
- UI 使用 Widget 数据模型生成简单 DrawList，再由 OpenGL 后端绘制矩形。
- Undo/Redo 保存场景序列化快照，以较低实现复杂度覆盖常用编辑操作。

## 7. Assimp 与 Dear ImGui

提交：`82bcaf5`

更新内容：

- 接入本地 Assimp 6.0.5 和 Dear ImGui 1.92.9 WIP。
- 支持 FBX、glTF/GLB、OBJ、DAE、3DS 等模型导入。
- 生成 `.mesh`、`.mat`、`.prefab` 和 `.meta`，支持导入与重导入。
- 编辑器切换到 ImGui + SDL2/OpenGL3 Backend。

实现方式：

- Assimp 只链接 Editor、AssetTool 和导入测试，发布 Runtime 不依赖 Assimp。
- 遍历 Assimp Node 保留层级和 Transform，再将 Mesh/Material 转换为轻量运行时格式。
- 外部纹理复制到资产目录；内嵌纹理提取后登记为依赖。
- 当前 ImGui master 没有 Docking API，因此编辑器采用固定多窗口布局。

## 8. 独立 Scene View、拾取与基础 PBR

提交：`c962963`

更新内容：

- 增加离屏 Scene RenderTarget 和 ImGui Scene View。
- 增加编辑器飞行相机、鼠标拾取和 Translate/Rotate/Scale Gizmo。
- 增加 PNG/JPG/BMP/TIFF、Radiance HDR 解码。
- 增加 metallic、roughness、emissive 和基础 Cook–Torrance PBR。

实现方式：

- 场景先渲染到 OpenGL FBO，再将颜色纹理交给 ImGui 显示。
- 编辑状态使用 Renderer camera override，Play 状态恢复场景 Primary Camera。
- 将鼠标坐标转换为世界射线，与 Mesh/Collider 的世界 AABB 求交并选择最近命中对象。
- Gizmo 将三轴投影到屏幕，通过鼠标到线段的距离判断操作轴。
- Windows 常见图片通过 WIC 转换为 RGBA8；RGBE/HDR 使用扫描线解码器读取。
- PBR Shader 使用 GGX Distribution、Schlick Fresnel 和 Smith Geometry 计算直接光照。

## 9. HDR、阴影、Tonemap 与 Shader 热重载

提交：`250d6f4`

更新内容：

- Scene RenderTarget 升级为 RGBA16F。
- 增加 ACES Tonemap、Gamma 和可调 Exposure。
- 增加 2048×2048 方向光 Shadow Map 和 3×3 PCF。
- 默认 PBR Shader 外置为 `.vert/.frag` 并支持自动、手动热重载。
- 增加资产拖入 Scene View 和 Gizmo snapping。

实现方式：

- 场景先写入浮点 HDR FBO，再用全屏三角形采样 HDR Texture，执行 ACES Filmic 映射后输出到 RGBA8 视口纹理。
- 阴影阶段用灯光正交投影只写深度；主渲染阶段将世界位置投影到 Shadow Map，并使用 Bias 和 3×3 PCF 降低阴影锯齿与痤疮。
- FileWatcher 检测 Shader 时间戳；新 Shader 编译链接成功后原子替换 Program，失败时继续使用上一份有效 Program。
- 资产浏览器通过 ImGui Drag/Drop Payload 传递资源路径，Scene View 根据类型实例化 Prefab、添加 Mesh 或设置 Material。
- Gizmo 累积鼠标移动后按 Translate/Rotate/Scale 各自步长量化，避免单帧增量小于吸附步长时操作失效。

## 10. 反射 Inspector 与场景恢复

更新内容：

- 新增运行时 `ReflectionRegistry`，登记全部可编辑 ECS 组件的字段、类型、范围、枚举和资产约束。
- Editor Inspector 改为反射驱动，可自动绘制、添加和删除组件。
- Mesh、Material、Animation、Audio 字段支持从资产浏览器拖入，并在写入前校验类型。
- 增加未保存星号、Ctrl+S、30 秒自动恢复快照、关闭前快照和 File > Restore Autosave。

实现方式：

- 反射使用普通 C++ 元数据和成员指针访问器，不引入代码生成器或 UObject 体系。
- 编辑器控件先编辑局部副本，在 ImGui 项目激活时记录原始场景快照，再将值写回组件，保持一次拖动对应一次 Undo 起点。
- 恢复快照独立于正式场景文件；正式保存成功后清除恢复文件。

## 11. 输入、空间音频、动画与轻量物理增强

更新内容：

- 新增 `TINYINPUT 1` 输入映射资产、运行时绑定/重绑定/存取，以及 SDL GameController 热插拔、摇杆死区和 UI 导航。
- 音频从顺序 Queue 升级为回调式多 Voice 混音器，支持循环、Bus 音量、距离衰减、左右声像和场景 AudioSource 生命周期。
- Transform Animation Clip 增加下一 Clip 交叉淡化、反向/循环时间处理和 Root Motion。
- 物理增加连续子步、线性阻尼/弹性、三轴最小穿透冲量响应、Convex/TriangleMesh 包围体、Fixed/Distance Joint、查询法线和调试包围体。
- 场景格式升级到 `TINYSCENE 5`，保留旧版读取并正确重映射 Prefab Joint 实体引用。

实现方式：

- 输入逻辑名可组合多个物理通道，GameController 数据在进入 InputSystem 前与键鼠值合并。
- 音频回调以 44.1 kHz 双声道 S16 混合所有 Voice，并在锁保护下实时更新实体声源增益、Bus、循环和声像。
- 轻量物理仍保持无第三方运行时依赖；Mesh Collider 当前使用资源包围体，精确三角形窄相位保留为后续可选后端。

## 12. Shader 管线、IBL、FXAA、LOD 与帧分析

更新内容：

- 增加递归 `#include`、循环检测、`#pragma variant` 宏组合、依赖列表和 FNV 内容哈希缓存。
- OpenGL 热重载跟踪主 Shader 与全部 include；编译/链接失败保留旧 Program，并在编辑器显示完整错误。
- PBR Shader 可采样 Radiance HDR equirect 环境，按法线和反射方向提供漫反射/粗糙度镜面 IBL。
- Tonemap Pass 增加可切换 FXAA；Mesh Entity 增加两级距离 LOD。
- 增加 CPU 阶段耗时和双缓冲 OpenGL Timer Query GPU 帧耗时。
- 场景格式升级为 `TINYSCENE 6`，新增 LOD 序列化。

实现方式：

- Shader 预处理器在 `#version` 后注入有序宏，展开相对路径 include，并给展开内容插入 `#line`。
- HDR 环境继续复用 TextureResource 的 RGBA16F 与 Mipmap，不增加运行时 Cubemap 资产格式。
- GPU Timer Query 延后一帧读取可用结果，避免同步等待 GPU。

## 13. 后台导入、增量 Cook 与 DPI/滚动 UI

更新内容：

- 增加 `AssetImportQueue`，Assimp 导入/重导入可在 JobSystem 后台执行，编辑器显示进度、失败信息并可取消。
- AssetCooker 增加 `TINYCOOK 1` 清单，统计 changed/skipped/removed，支持进度回调和协作式取消。
- Runtime UI 增加 DPI 缩放、滚动容器、滚动偏移和父级裁剪矩形传递。

实现方式：

- 导入任务用原子状态/进度和互斥结果分离高频轮询与大对象写入；任务完成后统一重扫 AssetRegistry。
- 增量 Cook 比较源文件时间戳与大小，删除只限清单记录的 Cook 相对路径，避免越界清理。
- UI 先在逻辑坐标完成布局和滚动，再整体乘 DPI，最终沿父链求裁剪交集交给 OpenGL scissor。

## 14. 多场景/材质编辑与发布工程

更新内容：

- Editor 增加多场景文档标签，每个标签保留路径、序列化快照和脏状态；Scene 资产可双击打开。
- Inspector 增加 Material 资产编辑，可修改 PBR 因子、透明模式、Shader 与六类贴图路径并热重载。
- Logger 增加 JSONL 结构化输出；CrashReporter 在 Windows 写 MiniDump 与文本报告，并覆盖 `std::terminate`。
- 增加 Windows GitHub Actions、`ReleaseSmoke.ps1`、第三方许可清单。

实现方式：

- 场景标签切换继续复用单个 ECS World，通过快照捕获/恢复避免运行时系统多实例化。
- Material 编辑保存仍使用稳定的文本 `.mat` 格式，写入成功后让现有缓存 Resource 原位 Reload。
- CI 使用无本地 Assimp/ImGui 依赖的 Runtime 配置；本地脚本额外验证完整发布包三帧启动。

## 15. 验证与文档维护

持续更新内容：

- 每一阶段都同步 README、技术设计和架构说明。
- 使用 Git 分阶段提交，当前工作区保持干净。
- Windows/MSVC Release 构建核心库、Demo、Editor、AssetTool、SceneTool 和 Tests。
- CTest 覆盖数学、ECS、场景、Prefab、项目、Job、资源、Assimp、PNG/HDR、PBR Material、Shader、Cook/TPAK/VFS、物理和 UI。
- 编辑器执行 OpenGL Shader、Shadow FBO、HDR/Tonemap FBO 和窗口响应冒烟检查。

## 16. 可维护性基础与平台 Cook

更新内容：

- 新增 `Serialization`：小端 BinaryWriter/Reader、版本化文本 Archive、原子保存/备份和逐版本 MigrationRegistry。
- 新增 `AssetStreamingManager`：类型化异步 AssetHandle、`.meta` 依赖闭包、Pin、最近访问与内存预算淘汰。
- 新增 `GameplayFramework`：GameMode、TimerManager、Pawn 和 CharacterController；场景升级为 `TINYSCENE 7`。
- 新增 `DiagnosticsService`：类型化 CVar、控制台命令和线程安全 StatRegistry，并接入时间缩放、FXAA、资源预算与帧统计。
- 新增 `Automation` 和 `tinyengine_commandlet`：资产/场景无头校验、场景备份迁移和平台 Cook。
- 新增 `TargetPlatform`：Windows/Linux Profile、生产期扩展名过滤、Shader variant 缓存、Game.tpak 与版本化 Cook 清单。

实现方式：

- 保持 Engine 为唯一组合根，新服务只通过明确指针访问 World、Input、Physics 和 ResourceManager。
- AssetHandle 持有资源强引用；预算淘汰只移除没有外部强引用且未 Pin 的缓存对象。
- CharacterController 使用语义输入和现有 `MoveCharacter`，避免在 Engine 主循环中继续保留 Demo 专用移动代码。
- Commandlet 不创建 SDL/OpenGL 窗口，适合 CI、内容审计和批量迁移；平台 Cook 复用 AssetCooker、ShaderPipeline 与 PakArchive。

## 17. 平台资产编译、GPU 预算与内容系统基础

更新内容：

- 新增 `PlatformAssetCompiler`，在 TargetPlatform Cook 中把 Texture 和 Mesh 转换为可直接加载的 `TNYTEX1` / `TNYMSH1` 二进制，并保持其他资产的复制行为。
- `Resource` 加载器同时兼容编辑期源格式与 Cook 后二进制格式；测试覆盖独立编译和 TPAK 挂载后的资源读取。
- `RenderDevice` 增加 GPU 驻留统计、预算和主动 Trim；OpenGL 后端统计 Mesh/Texture 缓存，Engine 提供 CVar、命令和 Stat。
- CharacterController 增加水平移动、台阶尝试、垂直碰撞、接地和碰顶结果。
- 新增骨骼层级、关键帧采样、蒙皮矩阵生成和条件状态机基础 API。
- 新增严格 UTF-8 编解码、本地化字符串表、locale 回退、参数替换和 TTF/OTF 容器加载校验。
- 新增 CPU 粒子发射器和 `VfxSystem`，支持连续发射、Burst、容量限制、重力、颜色/尺寸生命周期，并接入 Engine Tick/Shutdown。

实现方式：

- Cook 转换以 `AssetCooker::transform` 回调插入现有增量 Cook，不让资源层依赖平台命令行工具。
- GPU 预算只回收可从 CPU Resource 重建的 OpenGL 缓存，不销毁调用方持有的 CPU 资源。
- 新内容模块保持后端无关：动画只产出矩阵，VFX 只产出粒子数据，本地化只产出 Unicode 文本，后续由 Renderer/UI 消费。
- 当前骨骼数据仍由程序构造，FontFace 尚未栅格化 glyph，粒子尚未形成 DrawCommand；这些边界继续登记在未完成清单。

## 18. 导航、AI、世界分区与模块生命周期

更新内容：

- 新增 `NavigationGrid`：世界坐标/Cell 转换、可走性、Traversal Cost、AABB 障碍标记和四/八邻域 A*。
- 新增 AI Blackboard 与行为树：Action、Condition、Sequence、Selector、Wait 和跨帧运行状态。
- 新增 `WorldPartition`：Cell 注册、距离流送、加载/卸载迟滞、统一卸载和稳定的已加载 Cell 查询。
- 扩展 `ModuleManager`：依赖声明、缺失/循环检查、拓扑启动、失败回滚、逆序关闭和安全注销。
- EngineContext 暴露 Navigation、WorldPartition、ModuleManager；Engine 管理模块和分区的生命周期。

实现方式：

- 导航搜索使用数组化 g-score/前驱表和优先队列，禁止对角穿角；当前目标是小型动态关卡，不构造多边形 NavMesh。
- 行为组合节点只保存当前子节点索引，终态统一 Reset，避免每帧从根节点重复执行已经完成的 Wait 前置步骤。
- WorldPartition 通过回调保持对 Scene/Resource 实现的解耦，加载成功才修改 Cell 状态。
- 模块按注册顺序处理同层依赖，保证结果可复现；依赖决定的先后关系优先于注册顺序。

## 19. 网络快照、回放、Sequencer、地形与媒体基础

更新内容：

- 新增 `SnapshotCodec`、`SnapshotBuffer` 和 `ReplayStream`：网络状态二进制编码、时间插值、64 帧抖动缓冲和回放文件。
- 新增 Transform Track 与 `SequencePlayer`：关键帧采样、事件派发、播放/暂停/Seek/Stop/Loop。
- 新增 `HeightField`：规则高度数据、双线性高度、有限差分法线和带 UV/Tangent 的三角网格生成。
- 新增 `ImageSequencePlayer`：帧率、播放、暂停、Seek、循环和当前帧路径。

实现方式：

- Replay 直接封装 `TNYNET1` 快照，线上和离线状态表示保持一致；SnapshotBuffer 只插值两帧都存在的实体。
- Sequencer 和媒体播放器不持有 Renderer/World，游戏可将采样结果应用到 ECS，编辑器也可复用同一时间轴。
- HeightField 输出 `MeshVertex`/Index 数据但不直接创建 GPU 资源，保留 RenderDevice 边界。
- 当前联网层不含 UDP/RPC/Prediction，媒体不含视频 Codec，地形不含 Chunk LOD；这些能力仍登记在未完成清单。

## 20. P0 材质实例

更新内容：

- `MaterialResource` 支持 `parent <path>`，继承父材质的 Shader、PBR 参数、透明模式和所有贴图。
- 子材质只覆盖自身出现的字段；父材质可以继续继承，形成轻量实例链。
- 使用线程局部规范化路径集合检测循环依赖，失败时返回明确资源错误。
- Material Inspector 增加 Parent 路径编辑，ResourceManager Reload 后立即生效。

实现方式：

- 先扫描 parent 并加载基值，再用同一解析器应用子文件字段，Renderer 无需区分普通材质和实例。
- 实例继续使用 `.mat` 和现有 ResourceManager/AssetRegistry/Cook 流程，不引入第二套资产类型。
- 测试验证父级 Metallic/Normal Map 继承与子级 Color/Roughness 覆盖。

## 21. P0 粒子 Billboard 渲染

更新内容：

- `VfxSystem` 可汇总所有发射器的活动粒子，Renderer 在场景几何后统一提交。
- RenderDevice 增加粒子绘制契约，Null 与 OpenGL 后端均实现。
- OpenGL 增加世界空间 Billboard Shader：由 View Matrix 反推相机 Right/Up，按粒子位置和尺寸生成六顶点 Quad。
- Fragment Shader 提供圆形裁剪和软边 Alpha；绘制阶段保留深度测试、关闭深度写入并使用 Alpha Blend。
- Demo 场景增加火花发射器，测试验证三粒子会形成三次后端绘制计数。

## 22. Unicode 字形、坡度判定与编辑器多选

- `FontRasterizer` 在 Windows 使用 GDI TrueType 灰度轮廓生成中文 BMP 字形像素、Bearing 和 Advance。
- CharacterController 返回 GroundNormal，并按可配置最大坡度角拒绝不可行走表面。
- Hierarchy 与 Scene View 支持 Ctrl 增删选择集，Inspector 标识主实体，Delete 对选择集批量删除。
- 测试覆盖微软雅黑中文“引”字栅格化；Editor 和 Tests 构建通过。

## 23. Assimp 权重与 GPU Skinning 数据通路

- Assimp 为每个顶点筛选权重最高的四根骨骼并归一化，写入 `vw` 记录。
- MeshVertex、文本加载器和 `TNYMSH2` Cook 格式保留 BoneIndices/BoneWeights，同时兼容 `TNYMSH1`。
- OpenGL VAO 增加 location 4/5，默认 Shader 在 GPU 计算四骨骼混合矩阵。
- 当前上传 64 个 Identity Bind Pose，保证旧网格和权重网格兼容；下一步把 AnimationStateMachine 产出的 Skin Matrices 接入 RenderItem。

## 当前仍未完成

- Cubemap 离线预过滤/BRDF LUT、级联阴影、实例化和遮挡裁剪；HDR IBL、FXAA、LOD 和 GPU profiler 已完成。
- 精确 Mesh 窄相位/可选 Jolt、完整斜坡 Sweep/Slide CharacterController。
- Assimp 骨骼导入/GPU 蒙皮、OGG 流式音频、字体栅格化与复杂 UI 排版。
- 多选/框选、Gizmo local/world 和 Shader 文本编辑器；多场景与 Material 编辑已完成。
- 格式 CI 检查、渲染截图对比和逐分配内存追踪；基础无头 Commandlet、数据迁移、CI、崩溃报告、许可证清单和发布包冒烟已完成。

继续开发时，应同时更新本文、`README.md`、`设计.md` 和 `docs/ARCHITECTURE.md`。
