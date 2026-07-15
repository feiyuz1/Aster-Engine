# TinyEngine

TinyEngine 是参考 Unreal Engine 5.3.2 模块职责、以 C++17 实现的轻量 3D 引擎。目标不是复制 UE 的 UObject/UHT、完整 RHI 或大型编辑器，而是形成“项目—资产—场景—运行时—渲染—打包—编辑器”的最小可用闭环。

当前版本：0.25.0（完成渲染、材质、资产、物理、Unicode UI 与编辑器核心增强批次）。

## 已实现

- 项目与运行时：`.tinyproject`、启动场景、`GameInstance`、`SceneManager`、Play/Stop 隔离、SaveGame、独立 Shipping Game 和 TPAK 启动挂载。
- 世界：generation Entity、组件查询、父子层级、Active/Tag/Layer、生命周期、Prefab、`TINYSCENE 10`。
- 资产：Assimp 6.0.5 后台模型导入/重导入、稳定 GUID/`.meta`、DDS/KTX2 BC1–BC7、三类缩略图、依赖诊断/修复、移动引用更新、大文件分块区间读取、增量 Cook、TPAK、VFS。
- 图片：PPM，以及 Windows WIC 的 PNG/JPG/BMP/TIFF；Radiance HDR 保持线性浮点数据，GPU 使用 RGBA16F。
- 渲染：SDL2 + OpenGL 3.3、离线 Cubemap 六面 IBL/BRDF LUT、三段 CSM、透明覆盖率阴影、正确法线矩阵、GPU 实例化、保守遮挡剔除、Bloom/SSAO/TAA/FXAA/ACES、各向异性过滤、LOD 和 GPU Profiler。
- 材质：显式实例 Override、六类独立贴图与 ORM/RMA、Shader 参数反射、可视化 Material Graph、Shader 源码编辑和实时错误行定位。
- 物理：固定步长、SAP、Convex GJK、Triangle Mesh 三角形 Ray/Narrowphase、通用 Shape Sweep、摩擦/角速度/惯量/旋转锁、Fixed/Distance/Hinge/Spring Joint、Character Slide/Step/Slope，以及可选 Jolt 构建入口。
- 动画与音频：Assimp 四骨骼权重、层级/Inverse Bind、30 Hz Clip 重采样、逐通道误差压缩、动画事件、1D Blend Tree、Animation Graph 状态机/交叉淡化、Prefab 自动绑定和逐实体 GPU Skinning；多 Voice 混音与空间音频。
- 内容系统：严格 UTF-8 编解码、本地化字符串表/语言回退/参数替换、TTF/OTF 容器校验，以及带发射率、Burst、重力、生命周期曲线和 OpenGL 世界空间 Billboard 的粒子系统。
- 导航与 AI：可配置代价/障碍的网格 A*、斜向防穿角、Blackboard、Sequence/Selector、Condition/Action 和跨帧 Wait 行为节点。
- 大世界与模块：带加载/卸载迟滞距离的 World Cell 流送协调器；模块依赖校验、拓扑启动、失败回滚和逆序关闭，并接入 Engine 生命周期。
- 联网与回放：版本化网络快照编解码、generation Entity 保留、按时间排序的状态缓冲、Transform 插值和二进制 Replay 保存/加载。
- 影视与场景：Transform Sequencer Track、事件时间线、播放/暂停/循环；高度场双线性采样、法线和网格生成；可播放/Seek/循环的图片序列。
- 输入：`.input` 映射资产、运行时重绑定、键鼠与 SDL GameController 热插拔和死区处理。
- 编辑器：Dear ImGui、独立 Scene View、多场景标签、框选/Ctrl 多选、批量 Gizmo、Local/World 与 Pivot 模式、Shader/Material Graph 编辑、缩略图与依赖修复、反射 Inspector、Undo/Redo 与自动恢复。
- Shader：`#include`、`#pragma variant`、宏组合、依赖热重载、预处理磁盘缓存和完整编译/链接错误；失败时保留上一份有效程序。
- 分析：CPU Queue/Shadow/Scene 分段耗时、OpenGL `GL_TIME_ELAPSED` GPU 帧耗时、Draw/Cull/LOD 计数在编辑器实时展示。
- Runtime UI：动态 Unicode Glyph Atlas、Windows GDI/非 Windows FreeType、BiDi Run、连字、自动断行、富文本，以及 Panel/Label/Button/Image/InputField/List/Dropdown/ProgressBar、布局、裁剪、滚动、DPI 与导航。
- 工程化：Git、CTest、CPack、AssetTool、SceneTool、PackageTool、Editor 一键打包、Windows CI、JSONL 结构化日志、Windows MiniDump/文本崩溃报告、第三方许可清单和发布包冒烟脚本。
- 可维护性基础：通用二进制/版本化文本 Archive、顺序数据迁移、异步 AssetHandle、依赖闭包、CPU/GPU 资源驻留预算、Gameplay/Timer/GameMode/CharacterController、CVar/命令控制台/Stat、无头 Commandlet 和 Windows/Linux TargetPlatform Cook Profile。

## 构建和运行

```powershell
cmake -S . -B build-full `
  -DTINYENGINE_BUILD_EDITOR=ON `
  -DTINYENGINE_BUILD_IMPORTERS=ON `
  -DTINYENGINE_ASSIMP_DIR=D:\OpenSourceProject\assimp-master `
  -DTINYENGINE_IMGUI_DIR=D:\OpenSourceProject\imgui-master
cmake --build build-full --config Release -j 8
ctest --test-dir build-full -C Release --output-on-failure
.\build-full\Release\tinyengine_editor.exe TinyEngine.tinyproject
```

生成并运行独立游戏包：

```powershell
cmake --build build-full --config Release --target package_game
.\build-full\Package\TinyEngineDemo\TinyEngineDemo.exe `
  .\build-full\Package\TinyEngineDemo\TinyEngineDemo.tinyproject
```

无头校验和平台 Cook：

```powershell
.\build-full\Release\tinyengine_commandlet.exe ValidateAssets assets
.\build-full\Release\tinyengine_commandlet.exe ValidateScenes Saved
.\build-full\Release\tinyengine_commandlet.exe MigrateScenes Saved
.\build-full\Release\tinyengine_commandlet.exe CookProject TinyEngine.tinyproject Saved\PlatformCook Windows Shipping
```

Scene View 操作：按住鼠标右键并使用 WASD/QE 飞行，Shift 加速；左键选择、拖动框选、Ctrl 追加；W/E/R 切换 Gizmo，并可选择 Local/World 与 Center/Active/Individual Pivot。

## 当前边界

0.25 指定功能已进入可运行路径。继续向生产级升级的重点是 GPU HZB/Motion Vector TAA、完整 OpenType/HarfBuzz、引入 Jolt 后的目标平台认证、异步文件 I/O、动画 IK/重定向、Recast、完整网络传输和流式音频。详见[0.25 完成说明](docs/ENGINE_COMPLETION_025.md)与[后续工作](docs/REMAINING_WORK.md)。
