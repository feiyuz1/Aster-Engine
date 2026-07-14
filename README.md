# TinyEngine

TinyEngine 是参考 Unreal Engine 5.3.2 模块职责、以 C++17 实现的轻量 3D 引擎。目标不是复制 UE 的 UObject/UHT、完整 RHI 或大型编辑器，而是形成“项目—资产—场景—运行时—渲染—打包—编辑器”的最小可用闭环。

当前版本：0.10.0（输入、音频、动画与轻量物理增强）。

## 已实现

- 项目与运行时：`.tinyproject`、启动场景、`GameInstance`、`SceneManager`、Play/Stop 隔离、SaveGame、独立 Shipping Game 和 TPAK 启动挂载。
- 世界：generation Entity、组件查询、父子层级、Active/Tag/Layer、生命周期、Prefab、`TINYSCENE 5`。
- 资产：Assimp 6.0.5 模型导入、稳定 GUID/`.meta`、依赖和导入参数、异步资源、Cook、TPAK、VFS。
- 图片：PPM，以及 Windows WIC 的 PNG/JPG/BMP/TIFF；Radiance HDR 保持线性浮点数据，GPU 使用 RGBA16F。
- 渲染：SDL2 + OpenGL 3.3、RGBA16F Scene RenderTarget、ACES Tonemap/曝光、2048² 方向光 PCF 阴影、最多四盏方向光、程序化天空环境、完整 PBR 贴图、sRGB/Mipmap、粗裁剪和透明队列。
- 物理：固定步长、SAP、Box/Sphere/Capsule、Convex/TriangleMesh 包围体、层过滤、Trigger、Raycast/SphereCast/Overlap、连续子步、三轴冲量响应、Distance/Fixed Joint、基础角色移动和调试包围体。
- 动画与音频：Transform Clip 插值、Clip 交叉淡化与 Root Motion；多 Voice 实时混音、Master/SFX/Music/UI Bus、循环播放、距离衰减和左右声像。
- 输入：`.input` 映射资产、运行时重绑定、键鼠与 SDL GameController 热插拔和死区处理。
- 编辑器：Dear ImGui、独立 Scene View、编辑器飞行相机、鼠标拾取、带 snapping 的 Translate/Rotate/Scale Gizmo、资源拖入视口和 Inspector、反射自动 Inspector、组件增删、Shader/曝光控制、Hierarchy、Play/Stop、Undo/Redo、场景脏标记与自动恢复。
- Shader：`assets/shaders/default.vert/.frag` 外部资产，文件变化自动热重载；编译失败时保留上一份有效程序。
- Runtime UI：Panel/Label/Button/Image、ASCII 字体 Atlas、真实文本/图片采样、父子容器、锚点、横向/纵向布局、裁剪、鼠标与 Tab/Enter 导航。
- 工程化：Git、CTest、CPack、AssetTool、SceneTool、PackageTool 和 Editor 一键打包。

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

Scene View 操作：按住鼠标右键并使用 WASD/QE 飞行，Shift 加速；左键选择实体；W/E/R 切换移动、旋转、缩放 Gizmo。

## 当前边界

要达到可发布的通用小型引擎，还缺少 Cubemap Skybox/IBL、级联阴影和 GPU 优化；Shader include/variant 与完整错误面板；精确 Mesh 窄相位或 Jolt 后端；骨骼蒙皮与状态机；OGG 流式音频；Unicode 字体资产、滚动和 DPI；场景标签页与多选；CI、崩溃报告和发布冒烟测试。完整停止点见[未完成工作](docs/REMAINING_WORK.md)。
