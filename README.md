# TinyEngine

TinyEngine 是参考 Unreal Engine 5.3.2 模块职责、以 C++17 实现的轻量 3D 引擎。目标不是复制 UE 的 UObject/UHT、完整 RHI 或大型编辑器，而是形成“项目—资产—场景—运行时—渲染—打包—编辑器”的最小可用闭环。

当前版本：0.6.0（P0 渲染与编辑器增强）。

## 已实现

- 项目与运行时：`.tinyproject`、启动场景、`GameInstance`、`SceneManager`、Play/Stop 隔离和 SaveGame。
- 世界：generation Entity、组件查询、父子层级、Active/Tag/Layer、生命周期、Prefab、`TINYSCENE 3`。
- 资产：Assimp 6.0.5 模型导入、稳定 GUID/`.meta`、依赖和导入参数、异步资源、Cook、TPAK、VFS。
- 图片：PPM，以及 Windows WIC 的 PNG/JPG/BMP/TIFF；Radiance HDR 解码并转为运行时 RGBA8。
- 渲染：SDL2 + OpenGL 3.3、RGBA16F Scene RenderTarget、ACES Tonemap/曝光、2048² 方向光 PCF 阴影、基础 PBR、粗裁剪、透明排序和 Alpha 混合。
- 物理：固定步长、SAP、Box/Sphere/Capsule、层过滤、Trigger、Raycast/SphereCast/Overlap、基础角色移动。
- 编辑器：Dear ImGui、独立 Scene View、编辑器飞行相机、鼠标拾取、带 snapping 的 Translate/Rotate/Scale Gizmo、资源拖入视口、Shader/曝光控制、Hierarchy、Inspector、Play/Stop、Undo/Redo。
- Shader：`assets/shaders/default.vert/.frag` 外部资产，文件变化自动热重载；编译失败时保留上一份有效程序。
- 工程化：Git、CTest、CPack、AssetTool 和 SceneTool。

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

Scene View 操作：按住鼠标右键并使用 WASD/QE 飞行，Shift 加速；左键选择实体；W/E/R 切换移动、旋转、缩放 Gizmo。

## 当前边界

要达到可发布的通用小型引擎，还缺少 Skybox/IBL、sRGB 纹理、多灯和 GPU 优化；Shader include/variant 与完整错误面板；成熟物理后端；骨骼动画；空间音频；字体 Atlas 和完整 UI 布局；属性反射、场景标签页；CI、崩溃报告和发布冒烟测试。优先级见[技术设计](设计.md)，模块约束见[架构说明](docs/ARCHITECTURE.md)，历次实现方式见[实现更新日志](docs/IMPLEMENTATION_LOG.md)。
