# TinyEngine

TinyEngine 是参考 Unreal Engine 5.3.2 模块职责、以 C++17 实现的轻量 3D 引擎。目标不是复制 UE 的 UObject/UHT、完整 RHI 或大型编辑器，而是形成“项目—资产—场景—运行时—渲染—打包—编辑器”的最小可用闭环。

当前版本：0.27.0（完成 Render Graph、GPU HZB、Motion Vector TAA、独立 Bloom/GTAO、Forward+ Clustered Lighting 与 GPU Driven Indirect Draw）。

文档入口：[文档中心](docs/文档中心.md) · [构建指南](docs/构建指南.md) · [使用指南](docs/使用指南.md) · [技术设计](设计.md) · [项目架构](docs/项目架构.md) · [工具参考](docs/工具参考.md)

本文中的“已实现”表示功能进入可编译、可测试、可运行的代码路径，不等同于 UE 同名系统的商业级规模。具体算法和质量边界记录在对应专题文档中。

## 已实现

- 项目与运行时：`.tinyproject`、启动场景、`GameInstance`、`SceneManager`、Play/Stop 隔离、SaveGame、独立 Shipping Game 和 TPAK 启动挂载。
- 世界：generation Entity、组件查询、父子层级、Active/Tag/Layer、生命周期、Prefab、`TINYSCENE 11`。
- 资产：Assimp 6.0.5 后台模型导入/重导入、稳定 GUID/`.meta`、DDS/KTX2 BC1–BC7、三类缩略图、依赖诊断/修复、移动引用更新、大文件分块区间读取、增量 Cook、TPAK、VFS。
- 图片：PPM，以及 Windows WIC 的 PNG/JPG/BMP/TIFF；Radiance HDR 保持线性浮点数据，GPU 使用 RGBA16F。
- 渲染：SDL2 + OpenGL 4.3、资源依赖 Render Graph、三段 CSM、64 灯 Forward+（16×9×24 Cluster）、GPU HZB/Compute 可见实例压缩/Indirect Draw、Motion Vector TAA、独立五级 Bloom、半分辨率 GTAO、FXAA/ACES、离线 IBL/BRDF LUT、各向异性过滤、LOD 和 GPU Profiler。
- 材质：显式实例 Override、六类独立贴图与 ORM/RMA、Shader 参数反射、可视化 Material Graph、Shader 源码编辑和实时错误行定位。
- 物理：固定步长、SAP、Convex GJK、Triangle Mesh 三角形 Ray/Narrowphase、通用 Shape Sweep、摩擦/角速度/惯量/旋转锁、Fixed/Distance/Hinge/Spring Joint、Character Slide/Step/Slope，以及可选 Jolt 构建入口。
- 动画与音频：Assimp 四骨骼权重、层级/Inverse Bind、误差压缩、事件、1D/2D Blend、Animation Graph、重定向、双骨骼 IK、Root Motion、分层混合、调试快照和 GPU Skinning；多 Voice、空间音频与 OGG 长音频分块流。
- 内容系统：严格 UTF-8、本地化、动态字形，以及带纹理 Flipbook、CPU/顶点 Shader GPU 仿真、实例合批和 `.vfx` 编辑器的粒子系统。
- 导航与 AI：网格 A*、多边形 NavMesh、动态障碍、Crowd、EQS、Blackboard、运行时行为树和可视化行为树图编辑器。
- 大世界与模块：带加载/卸载迟滞距离的 World Cell 流送协调器；模块依赖校验、拓扑启动、失败回滚和逆序关闭，并接入 Engine 生命周期。
- 联网与回放：快照/抖动缓冲/Replay、IPv4 UDP、Sequence/Ack 可靠通道、白名单 RPC、Ownership、客户端预测、服务器纠错、包长和限流边界。
- 影视与场景：Transform Sequencer Track、事件时间线、播放/暂停/循环；高度场双线性采样、法线和网格生成；可播放/Seek/循环的图片序列。
- 输入：`.input` 映射资产、运行时重绑定、键鼠与 SDL GameController 热插拔/死区、平台 Glyph 和双马达震动。
- 编辑器：Dear ImGui、独立 Scene View、多场景标签、框选/Ctrl 多选、批量 Gizmo、Local/World 与 Pivot 模式、Shader/Material Graph 编辑、缩略图与依赖修复、反射 Inspector、Undo/Redo 与自动恢复。
- Shader：`#include`、`#pragma variant`、宏组合、依赖热重载、预处理磁盘缓存和完整编译/链接错误；失败时保留上一份有效程序。
- 分析：CPU Queue/Shadow/Scene 分段耗时、OpenGL `GL_TIME_ELAPSED` GPU 帧耗时、Draw/Cull/LOD 计数在编辑器实时展示。
- Runtime UI：动态 Unicode Glyph Atlas、Windows GDI/非 Windows FreeType、BiDi Run、连字、自动断行、富文本，以及 Panel/Label/Button/Image/InputField/List/Dropdown/ProgressBar、布局、裁剪、滚动、DPI 与导航。
- 工程化：Git、CPack、AssetTool、SceneTool、PackageTool、Editor 一键打包、Windows CI、JSONL 结构化日志、Windows MiniDump/文本崩溃报告、第三方许可清单和发布包冒烟脚本。
- 可维护性基础：稳定 C 原生插件 ABI、通用 Archive/迁移、异步 AssetHandle、依赖闭包、资源预算、Gameplay、CVar/Stat、无头 Commandlet 和 Windows/Linux Cook Profile。

## 快速开始

需要 CMake 3.20+、C++17 编译器、OpenGL 4.3 驱动，以及本地 Assimp/ImGui 源码。SDL2 和 stb 在本机不可用时由 CMake 获取；完整依赖与离线构建方式见[构建指南](docs/构建指南.md)。

```powershell
cmake -S . -B build `
  -DTINYENGINE_BUILD_EDITOR=ON `
  -DTINYENGINE_BUILD_IMPORTERS=ON `
  -DTINYENGINE_ASSIMP_DIR=D:\OpenSourceProject\assimp-master `
  -DTINYENGINE_IMGUI_DIR=D:\OpenSourceProject\imgui-master
cmake --build build --config Debug -j 8
.\build\Debug\tinyengine_editor.exe TinyEngine.tinyproject
```

生成并运行独立游戏包：

```powershell
cmake --build build --config Release --target package_game
.\build\Package\TinyEngineDemo\TinyEngineDemo.exe `
  .\build\Package\TinyEngineDemo\TinyEngineDemo.tinyproject
```

无头校验和平台 Cook：

```powershell
.\build\Release\tinyengine_commandlet.exe ValidateAssets assets
.\build\Release\tinyengine_commandlet.exe ValidateScenes Saved
.\build\Release\tinyengine_commandlet.exe MigrateScenes Saved
.\build\Release\tinyengine_commandlet.exe CookProject TinyEngine.tinyproject Saved\PlatformCook Windows Shipping
```

Scene View 操作：按住鼠标右键并使用 WASD/QE 飞行，Shift 加速；左键选择、拖动框选、Ctrl 追加；W/E/R 切换 Gizmo，并可选择 Local/World 与 Center/Active/Individual Pivot。

资产导入、环境烘焙、VFX/动画/行为树编辑和打包流程见[使用指南](docs/使用指南.md)；全部 CLI 参数见[工具参考](docs/工具参考.md)。

## 当前边界

0.27 指定渲染架构已进入可运行路径。继续向生产级升级的重点是跨平台 RHI/Shader 编译链、异步 Compute 与多线程命令录制、完整 OpenType/HarfBuzz、正式 Recast/Detour 烘焙、联网加密/会话服务和目标平台认证。详见[版本完成记录](docs/版本完成记录.md)与[后续工作](docs/后续工作.md)。
