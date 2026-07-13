# TinyEngine

TinyEngine 是参考 Unreal Engine 5.3.2 模块边界、以 C++17 重新实现的轻量 3D 引擎。它不复制 UE 的 UObject/UHT、RHI 矩阵或大型编辑器框架，只保留一款小型引擎形成生产闭环所需的职责。

当前版本：0.4.0（P0/P0.1 生产工具闭环）。

## 已实现

- 项目与运行时：`.tinyproject`、启动场景、`GameInstance`、`SceneManager`、Play/Stop 场景隔离。
- 世界：generation Entity、组件查询、父子层级、Active/Tag/Layer、生命周期通知、Prefab、TINYSCENE 3。
- 资产：Assimp 6.0.5 模型导入、稳定 GUID `.meta`、依赖/导入参数、异步资源、Cook、TPAK、目录/Pak 优先级挂载。
- 渲染：SDL2 + OpenGL 3.3、Mesh/Material/Texture、相机与方向光、粗视锥裁剪、不透明/透明排序、Alpha 混合、运行时 UI 绘制层。
- 物理：固定步长、SAP 宽相位、Box/Sphere/Capsule、层过滤、Trigger、Enter/Stay/Exit、Raycast/SphereCast/Overlap、角色移动基础。
- 运行时 UI：Panel/Label/Button/Image 数据模型、点击、焦点、DrawList。
- 编辑器：本地 Dear ImGui 1.92.9 WIP、Hierarchy、Inspector、资产浏览和模型导入、统计、场景保存、Play/Stop、Undo/Redo、复制/粘贴、组件添加。
- 基础设施：日志、配置、模块、文件、计时、内存池、帧分配器、优先级 Job、输入、SDL 音频、Transform 动画和原生脚本生命周期。

## 构建与测试

本机完整构建使用本地依赖：

```powershell
cmake -S . -B build-full `
  -DTINYENGINE_BUILD_EDITOR=ON `
  -DTINYENGINE_BUILD_IMPORTERS=ON `
  -DTINYENGINE_ASSIMP_DIR=D:\OpenSourceProject\assimp-master `
  -DTINYENGINE_IMGUI_DIR=D:\OpenSourceProject\imgui-master
cmake --build build-full --config Release -j 8
ctest --test-dir build-full -C Release --output-on-failure
```

```powershell
.\build-full\Release\tinyengine_editor.exe TinyEngine.tinyproject
```

运行 Demo：

```powershell
.\build\Release\tinyengine_demo.exe
```

资产工具：

```powershell
.\build-full\Release\tinyengine_asset_tool.exe scan assets
.\build-full\Release\tinyengine_asset_tool.exe import D:\Models\robot.glb assets\Imported\robot assets\Imported\robot
.\build-full\Release\tinyengine_asset_tool.exe cook assets Saved\Cooked Saved\Game.tpak
.\build-full\Release\tinyengine_asset_tool.exe list Saved\Game.tpak
```

## 当前边界

Assimp 已支持 glTF/GLB、FBX、OBJ、DAE、3DS 等模型格式并转换为 `.mesh/.mat/.prefab`。压缩纹理会被提取或复制，但运行时仍只正式解码 PPM；PNG/JPG/HDR 解码、Viewport Gizmo、RenderTarget/PBR/阴影、成熟物理后端和字体 Atlas 仍待完成。详细状态见[技术设计](设计.md)与[架构说明](docs/ARCHITECTURE.md)。
