# TinyEngine

TinyEngine 是参考 Unreal Engine 5.3.2 模块边界、以 C++17 重新实现的轻量 3D 引擎。它不复制 UE 的 UObject/UHT、RHI 矩阵或大型编辑器框架，只保留一款小型引擎形成生产闭环所需的职责。

当前版本：0.3.0（P0 基础闭环）。

## 已实现

- 项目与运行时：`.tinyproject`、启动场景、`GameInstance`、`SceneManager`、Play/Stop 场景隔离。
- 世界：generation Entity、组件查询、父子层级、Active/Tag/Layer、生命周期通知、Prefab、TINYSCENE 3。
- 资产：稳定 GUID `.meta`、类型识别、依赖/导入参数、重导入判定、异步资源、Cook、TPAK、目录/Pak 优先级挂载。
- 渲染：SDL2 + OpenGL 3.3、Mesh/Material/Texture、相机与方向光、粗视锥裁剪、不透明/透明排序、Alpha 混合、运行时 UI 绘制层。
- 物理：固定步长、SAP 宽相位、Box/Sphere/Capsule、层过滤、Trigger、Enter/Stay/Exit、Raycast/SphereCast/Overlap、角色移动基础。
- 运行时 UI：Panel/Label/Button/Image 数据模型、点击、焦点、DrawList。
- 编辑器源码：Dear ImGui Docking、Hierarchy、Inspector、资产浏览、统计、场景保存、Play/Stop、Undo/Redo、复制/粘贴、组件添加。
- 基础设施：日志、配置、模块、文件、计时、内存池、帧分配器、优先级 Job、输入、SDL 音频、Transform 动画和原生脚本生命周期。

## 构建与测试

默认构建完全离线（已有 SDL 缓存或系统 SDL 时）：

```powershell
cmake -S . -B build -DTINYENGINE_BUILD_TESTS=ON -DTINYENGINE_BUILD_EDITOR=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

编辑器会从官方仓库下载 Dear ImGui 1.92.8：

```powershell
cmake -S . -B build-editor -DTINYENGINE_BUILD_EDITOR=ON
cmake --build build-editor --config Release
.\build-editor\Release\tinyengine_editor.exe TinyEngine.tinyproject
```

网络受限时可预先提供源码：

```powershell
cmake -S . -B build-editor -DTINYENGINE_BUILD_EDITOR=ON -DFETCHCONTENT_SOURCE_DIR_IMGUI=D:\deps\imgui
```

运行 Demo：

```powershell
.\build\Release\tinyengine_demo.exe
```

资产工具：

```powershell
.\build\Release\tinyengine_asset_tool.exe scan assets
.\build\Release\tinyengine_asset_tool.exe cook assets Saved\Cooked Saved\Game.tpak
.\build\Release\tinyengine_asset_tool.exe list Saved\Game.tpak
```

## 当前边界

P0 已建立可扩展骨架，但尚未达到商业发行质量。仍需继续完成 glTF/PNG/JPG/HDR 的正式导入器、编辑器视口 Gizmo、离屏 RenderTarget/PBR/阴影、成熟物理后端、字体字形渲染、崩溃报告和多平台 CI。详细状态见[技术设计](设计.md)与[架构说明](docs/ARCHITECTURE.md)。
