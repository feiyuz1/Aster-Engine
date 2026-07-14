# TinyEngine 实现更新日志

本文记录本轮开发对 TinyEngine 做过的主要更新，以及各功能采用的简要实现方式。内容按 Git 提交顺序整理，方便以后定位代码、回溯设计和继续开发。

当前引擎版本：0.9.0

参考源码：Unreal Engine 5.3.2

最后同步：2026-07-14

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

## 11. 验证与文档维护

持续更新内容：

- 每一阶段都同步 README、技术设计和架构说明。
- 使用 Git 分阶段提交，当前工作区保持干净。
- Windows/MSVC Release 构建核心库、Demo、Editor、AssetTool、SceneTool 和 Tests。
- CTest 覆盖数学、ECS、场景、Prefab、项目、Job、资源、Assimp、PNG/HDR、PBR Material、Shader、Cook/TPAK/VFS、物理和 UI。
- 编辑器执行 OpenGL Shader、Shadow FBO、HDR/Tonemap FBO 和窗口响应冒烟检查。

## 当前仍未完成

- Skybox、IBL、sRGB Texture、多灯、级联阴影和 GPU profiler。
- Shader include/variant、磁盘缓存和完整错误面板。
- Jolt、Mesh Collider、CCD、Joint 和物理调试绘制。
- 骨骼动画、空间音频、字体 Atlas 和完整 UI 布局。
- 场景标签页、多选/框选、Gizmo local/world 和 Material/Shader 专用编辑器；反射 Inspector、资源拖放和场景恢复已完成。
- CI、崩溃报告、许可证清单和发布包冒烟测试。

继续开发时，应同时更新本文、`README.md`、`设计.md` 和 `docs/ARCHITECTURE.md`。
