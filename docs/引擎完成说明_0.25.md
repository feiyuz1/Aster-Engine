# TinyEngine 0.25 渲染、资产、物理、UI 与编辑器完成说明

记录日期：2026-07-16
对应提交：`0b8ae87`、`b081777`、`9f0be94`

本文记录 0.25 版本新增能力、主要实现路径、使用入口和当前边界。目标是提供轻量但可直接使用的完整闭环，不复制 UE 的 UObject、Render Graph 或编辑器框架。

## 渲染

- `EnvironmentBaker` 将 HDR/普通环境图离线卷积为六面 Cubemap Atlas：16×16 辐照度六面、六级粗糙度的 32×32 GGX 预过滤六面，以及 128×128 split-sum BRDF LUT。`.env` 清单把三份运行时纹理组合为一个环境资产。
- Renderer 使用三段 practical split CSM；OpenGL 后端维护三套 2048² 深度目标和 3×3 PCF。透明材质在阴影 Pass 读取 Albedo Alpha，并用屏幕空间抖动形成半透明覆盖率阴影。
- Vertex Shader 对 Model/Skin 组合矩阵使用逆转置法线矩阵，非等比缩放不再破坏 Normal/Tangent。
- 相同 Mesh+Material 的非蒙皮不透明对象合并为一个实例批次，实例矩阵通过 location 8–11 和 divisor 上传。
- RenderQueue 在视锥、LOD 后执行保守球体遮挡测试；透明物体不参与遮挡源或被遮挡，避免错误破坏混合顺序。
- HDR Scene Target 的合成 Pass 提供 Bloom、深度邻域 SSAO、历史缓冲 TAA、FXAA、ACES 和曝光控制；编辑器可逐项开关。
- 检测 `GL_EXT_texture_filter_anisotropic` 后，对所有 Mipmap 纹理设置设备允许的最大各向异性等级。

离线环境命令：

```text
tinyengine_asset_tool bake-env <source-hdr> <output-directory>
```

## Shader 与材质

- 有父材质时，`override <property> 0|1` 显式决定实例是否覆盖父值；没有父材质的旧 `.mat` 保持原有行为。
- `ShaderPipeline::ReflectParameters` 反射 `float/vec2/vec3/vec4/sampler2D`，支持行内 `//@range` 与 `//@default`，Inspector 自动生成控件，运行时按反射类型上传参数。
- `.matgraph` 保存 Constant、Texture、Add、Multiply、PBR Output 节点和连线；编辑器提供可拖动节点、Shift 连线、保存和 GLSL 编译。
- `packedTexture` 配合 `packedLayout ORM|RMA` 在 Shader 内解释 AO/Roughness/Metallic 通道，独立贴图存在时优先使用独立贴图。
- Shader Source Editor 使用多行源码控件；每次编辑调用真实 OpenGL 编译器验证，保留上一份有效 Program，并从驱动日志提取错误行号。

## 资产管线

- `TextureResource` 解析 DDS、KTX2 的 BC1–BC7 层级和 sRGB 标志；OpenGL 使用 `glCompressedTexImage2D` 原样上传压缩块，不在 CPU 解压回 RGBA。
- `AssetMaintenance::GenerateThumbnail` 为 Texture 生成缩放预览、为 Material 生成受光球预览、为 Mesh 生成归一化顶点预览；编辑器缓存到 `Saved/Thumbnails`。
- `FindMissingDependencies` 返回 Owner、丢失路径和同名/同 Stem 候选；编辑器可选择候选并同时修复源文件引用与 `.meta` 依赖。
- `MoveAsset` 同步移动资产和 `.meta`，扫描所有已登记文本资产并更新路径与依赖。
- `ChunkAsset` 生成 `TINYCHUNK 1` 清单和固定大小块；`ChunkedAssetStream::Read` 只打开覆盖请求区间的块，并支持跨块读取。

工具入口：

```text
tinyengine_asset_tool thumbnail <asset> <output.ppm>
tinyengine_asset_tool missing <asset-directory>
tinyengine_asset_tool chunk <asset> <output.chunkmanifest> [chunk-bytes]
```

## 物理

- Convex Hull 使用 Mesh 顶点作为 Support Map，并通过三维 GJK 做窄相位相交判定。
- Triangle Mesh 在 Broadphase AABB 后逐三角形检查候选，Raycast 对实际世界空间三角形执行 Möller–Trumbore，相交法线来自三角形面。
- `ShapeSweep` 统一接受 Box、Sphere、Capsule、Convex Hull 和 Triangle Mesh 几何；当前轻量后端使用保守包围球 Sweep，保证不会漏碰，可能比精确 TOI 略早命中。
- Character Controller 保留分轴 Slide、抬升/前进/回落 Step、Skin Width、坡度法线和边缘接地探测。
- RigidBody 增加角速度、角阻尼、摩擦、惯量和三轴旋转锁；冲量求解加入切向摩擦。Joint 增加 Hinge/Spring、轴、阻尼、限制和 Break Force。
- `TINYENGINE_ENABLE_JOLT` 与 `TINYENGINE_JOLT_DIR` 提供可选 Jolt 源码构建入口；`PhysicsBackend` 暴露运行时选择和可用性查询。未启用依赖时明确拒绝选择 Jolt，内置后端始终可用。

## Runtime UI 与文字

- `TextLayout` 严格处理 UTF-8，按强字符确定基础方向、重排 RTL Run、生成 fi/fl 连字、按宽度断行，并解析 `[b]`、`[i]`、`[color=#RRGGBB]`、`[size=N]`。
- OpenGL 后端按首次出现的 Codepoint 动态栅格化到 1024² Glyph Atlas。Windows 使用系统字体/GDI；非 Windows 构建强制查找并链接 FreeType，可用 `TINYENGINE_FONT_PATH` 指定字体。
- 富文本颜色拆成独立 UI Batch；字号和布局位置来自 Shaped Glyph。
- UISystem 新增 InputField、List、Dropdown、ProgressBar，支持 UTF-8 输入、按 Codepoint 退格、焦点导航、选择和值变化回调。

说明：当前塑形层采用轻量 HarfBuzz 风格的 Cluster/Run/Advance 数据模型和内置 BiDi/连字规则，没有把 HarfBuzz 作为强制依赖。需要完整 OpenType Script Shaping 的项目可在 `TextLayout` 后替换成 HarfBuzz，而不改变 UI Draw Command。

## 编辑器

- Scene View 左键拖动形成框选；Ctrl 框选追加到现有选择。
- Gizmo 对全部选中实体操作，支持 World/Local 轴和 Center/Active/Individual Pivot。
- Scene View 暴露 Bloom、SSAO、TAA、FXAA、曝光和 Shader Reload。
- Shader Source Editor 提供保存、实时编译验证和错误行定位。
- Material Graph 提供节点和连线可视化编辑。
- Asset Browser 显示缩略图、类型、字节数、依赖数、丢失依赖修复，以及 Move/Rename 后引用更新。
- 通用反射 Inspector 的属性编辑继续进入 Scene Snapshot Undo；拖动型属性只在激活时记录一次，避免每帧制造 Undo 项。

## 验证

- Debug 全目标编译通过：runtime、editor、Assimp importer、asset/package/scene/commandlet 工具和测试。
- CTest：`tinyengine_import_compatibility`、`tinyengine_core` 全部通过。
- 新增测试覆盖 Cubemap Atlas/LUT、材质覆盖与 Shader 反射、DDS BC1、跨块流式读取、Material Graph、Shape Sweep、角速度、Unicode/BiDi/连字和 UI 控件。
- Demo 使用真实 SDL2/OpenGL 后端运行三帧并正常退出；默认场景实例合批后每帧一个主场景 Draw Call。

## 明确边界

- 遮挡剔除是 CPU 保守遮挡体方案，不是 UE Nanite/HZB；TAA 没有 Motion Vector，快速运动时质量低于现代完整 TAA。
- 内置 Triangle Mesh 接触生成仍以三角形候选加保守解穿为主；要求大型复杂刚体堆叠时应启用并完成项目侧 Jolt 适配验证。
- DDS/KTX2 路径消费已有 BC1–BC7 数据；本仓库不内置专利/质量敏感的 BC6H/BC7 编码器，平台 Cook 应调用目标平台的离线编码工具生成 DDS/KTX2。
