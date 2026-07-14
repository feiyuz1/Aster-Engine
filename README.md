# TinyEngine

TinyEngine 是参考 Unreal Engine 5.3.2 模块职责、以 C++17 实现的轻量 3D 引擎。目标不是复制 UE 的 UObject/UHT、完整 RHI 或大型编辑器，而是形成“项目—资产—场景—运行时—渲染—打包—编辑器”的最小可用闭环。

当前版本：0.13.0（多场景/材质编辑与发布工程闭环）。

## 已实现

- 项目与运行时：`.tinyproject`、启动场景、`GameInstance`、`SceneManager`、Play/Stop 隔离、SaveGame、独立 Shipping Game 和 TPAK 启动挂载。
- 世界：generation Entity、组件查询、父子层级、Active/Tag/Layer、生命周期、Prefab、`TINYSCENE 6`。
- 资产：Assimp 6.0.5 后台模型导入/重导入、进度与取消、稳定 GUID/`.meta`、依赖和导入参数、异步资源、清单增量 Cook、TPAK、VFS。
- 图片：PPM，以及 Windows WIC 的 PNG/JPG/BMP/TIFF；Radiance HDR 保持线性浮点数据，GPU 使用 RGBA16F。
- 渲染：SDL2 + OpenGL 3.3、RGBA16F Scene RenderTarget、ACES Tonemap/曝光、FXAA、2048² 方向光 PCF 阴影、最多四盏方向光、程序化天空与 HDR equirect 环境 IBL、完整 PBR 贴图、sRGB/Mipmap、距离 LOD、粗裁剪和透明队列。
- 物理：固定步长、SAP、Box/Sphere/Capsule、Convex/TriangleMesh 包围体、层过滤、Trigger、Raycast/SphereCast/Overlap、连续子步、三轴冲量响应、Distance/Fixed Joint、基础角色移动和调试包围体。
- 动画与音频：Transform Clip 插值、Clip 交叉淡化与 Root Motion；多 Voice 实时混音、Master/SFX/Music/UI Bus、循环播放、距离衰减和左右声像。
- 输入：`.input` 映射资产、运行时重绑定、键鼠与 SDL GameController 热插拔和死区处理。
- 编辑器：Dear ImGui、独立 Scene View、多场景文档标签、材质资产编辑、编辑器飞行相机、鼠标拾取、带 snapping 的 Translate/Rotate/Scale Gizmo、资源拖入视口和 Inspector、反射自动 Inspector、组件增删、Shader/曝光控制、Hierarchy、Play/Stop、Undo/Redo、场景脏标记与自动恢复。
- Shader：`#include`、`#pragma variant`、宏组合、依赖热重载、预处理磁盘缓存和完整编译/链接错误；失败时保留上一份有效程序。
- 分析：CPU Queue/Shadow/Scene 分段耗时、OpenGL `GL_TIME_ELAPSED` GPU 帧耗时、Draw/Cull/LOD 计数在编辑器实时展示。
- Runtime UI：Panel/Label/Button/Image、ASCII 字体 Atlas、真实文本/图片采样、父子容器、锚点、横向/纵向布局、祖先裁剪、滚动偏移、DPI 缩放、鼠标与 Tab/Enter 导航。
- 工程化：Git、CTest、CPack、AssetTool、SceneTool、PackageTool、Editor 一键打包、Windows CI、JSONL 结构化日志、Windows MiniDump/文本崩溃报告、第三方许可清单和发布包冒烟脚本。

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

要达到更高质量的通用小型引擎，还缺少 Cubemap 离线预过滤/BRDF LUT、级联阴影和实例化/遮挡裁剪；材质实例；精确 Mesh 窄相位或 Jolt 后端；骨骼蒙皮与状态机；OGG 流式音频；Unicode 字体资产；编辑器多选/框选和 Local Gizmo。完整边界见[未完成工作](docs/REMAINING_WORK.md)。
