# TinyEngine

TinyEngine 是参考 Unreal Engine 5.3.2 模块职责、以 C++17 实现的轻量 3D 引擎。目标不是复制 UE 的 UObject/UHT、完整 RHI 或大型编辑器，而是形成“项目—资产—场景—运行时—渲染—打包—编辑器”的最小可用闭环。

当前版本：0.27.0（完成 Render Graph、GPU HZB、Motion Vector TAA、独立 Bloom/GTAO、Forward+ Clustered Lighting 与 GPU Driven Indirect Draw）。

文档入口：[文档中心](docs/README.md) · [构建指南](docs/Guides/构建指南.md) · [使用指南](docs/Guides/使用指南.md) · [项目工作流](docs/Guides/项目工作流.md) · [技术设计](docs/Architecture/设计.md) · [项目架构](docs/Architecture/项目架构.md) · [工具参考](docs/Reference/工具参考.md)

本文中的“已实现”表示功能进入可编译、可测试、可运行的代码路径，不等同于 UE 同名系统的商业级规模。具体算法和质量边界记录在对应专题文档中。

## 已实现

- 项目与内容生产：项目配置、场景与 Prefab、多场景编辑、反射 Inspector、Undo/Redo、自动恢复、后台模型导入、资产维护、Cook、TPAK 和独立游戏打包。
- 世界与 Gameplay：ECS、父子层级、常用 3D 组件、GameInstance/GameMode、SaveGame、C++ 脚本、模块生命周期和稳定 C 插件 ABI。
- 渲染与材质：OpenGL 4.3、九 Pass Render Graph、Forward+、GPU HZB/Indirect Draw、PBR、CSM、GTAO、Bloom、Motion Vector TAA、材质实例、Material Graph 和 Shader 工具链。
- 角色与交互：骨骼动画、Blend Tree、Animation Graph、重定向、Two Bone IK、Root Motion、VFX、空间音频、输入映射和手柄支持。
- 仿真与世界系统：轻量刚体/碰撞/约束、角色移动、可选 Jolt、导航、Crowd、EQS、行为树、World Partition 协调器、地形和 Sequencer。
- UI 与本地化：Runtime Widget、动态 Unicode 字形、基础 BiDi/连字/换行/富文本，以及中英文编辑器界面。
- 联网与回放：IPv4 UDP、可靠通道、RPC、Ownership、快照、预测纠错和 Replay；定位为局域网与机制验证能力。
- 工程工具：Asset/Scene/Package Tool、Commandlet、Windows CI、结构化日志、崩溃报告和构建兼容性门禁。

具体能力、入口和边界见[文档中心](docs/README.md)。

## 快速开始

需要 CMake 3.21+、C++17 编译器、OpenGL 4.3 驱动。可选模型导入功能需要本地 Assimp 源码。Dear ImGui 使用固定 docking 版本；SDL2 和 stb 在本机不可用时由 CMake 获取；完整依赖与离线构建方式见[构建指南](docs/Guides/构建指南.md)。

```powershell
cmake --preset editor
cmake --build --preset editor-debug
.\Intermediate\Build\Editor\Debug\tinyengine_editor.exe
```

不带项目参数会打开项目启动页。填写名称和位置，点击“创建并打开”，编辑器会生成场景、基础素材、`Source/Game.cpp`、VS 工程并首次编译项目 DLL；打开后点击运行即可执行项目逻辑。双击 SDK 的 `Programs/ProjectTool/TinyEngineProjectTool.exe` 注册一次菜单，之后也可以右键 `.tinyproject` 生成 `.sln`。详细步骤见[项目代码开发](docs/Guides/项目代码开发.md)。

生成并运行独立游戏包：

```powershell
cmake --build --preset editor-release --target package_game
.\Intermediate\Build\Editor\Package\TinyEngineDemo\TinyEngineDemo.exe `
  .\Intermediate\Build\Editor\Package\TinyEngineDemo\TinyEngineDemo.tinyproject
```

无头校验和平台 Cook：

```powershell
.\Intermediate\Build\Editor\Release\tinyengine_commandlet.exe ValidateAssets assets
.\Intermediate\Build\Editor\Release\tinyengine_commandlet.exe ValidateScenes Saved
.\Intermediate\Build\Editor\Release\tinyengine_commandlet.exe MigrateScenes Saved
.\Intermediate\Build\Editor\Release\tinyengine_commandlet.exe CookProject TinyEngine.tinyproject Saved\PlatformCook Windows Shipping
```

Scene View 操作：点击视图后使用 WASD/QE 飞行，按住鼠标右键转向，Shift 加速；左键选择、拖动框选、Ctrl 追加；1/2/3 切换移动、旋转、缩放工具，并可选择 Local/World 与 Center/Active/Individual Pivot。F11 放大或还原场景视图。

资产导入、环境烘焙、VFX/动画/行为树编辑和打包流程见[使用指南](docs/Guides/使用指南.md)；全部 CLI 参数见[工具参考](docs/Reference/工具参考.md)。

## 当前边界

0.27 指定渲染架构已进入可运行路径。继续向生产级升级的重点是跨平台 RHI/Shader 编译链、异步 Compute 与多线程命令录制、完整 OpenType/HarfBuzz、正式 Recast/Detour 烘焙、联网加密/会话服务和目标平台认证。详见版本完成记录（本地历史文档）与后续工作（本地历史文档）。
