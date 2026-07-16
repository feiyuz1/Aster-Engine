# TinyEngine 0.27 现代渲染架构完成说明

本文记录 Render Graph、GPU HZB、Motion Vector TAA、独立多级 Bloom/GTAO、Forward+/Clustered Lighting 和 GPU Driven Indirect Draw 的实际实现、验证方式与质量边界。

## 1. 帧图与资源模型

`RenderGraph` 使用逻辑 Texture/Buffer Handle 描述资源，Pass 显式声明 Read/Write。编译阶段建立 RAW、WAR、WAW 依赖并执行稳定拓扑排序，同时拒绝无效 Handle、未写先读和依赖环。瞬态资源记录 First/Last Pass，并在尺寸、格式和类型兼容且生命周期不重叠时复用 Alias Slot。

当前主帧包含：

1. Shadow Cascade 0
2. Shadow Cascade 1
3. Shadow Cascade 2
4. Forward+ Scene
5. GPU HZB
6. GTAO
7. Bloom Pyramid
8. Motion Vector TAA
9. Tone Map

OpenGL 资源仍由 `SdlGlRenderDevice` 持有；Render Graph 当前负责逻辑依赖、顺序、校验、生命周期和统计，不是完整跨 API 资源分配器。

## 2. Forward+ / Clustered Lighting

Light 组件支持 Directional、Point、Spot，包含颜色、强度、范围、方向和 Spot 内外锥角，场景格式升级为 `TINYSCENE 11` 并兼容读取 1–10。

每帧最多收集 64 盏灯。`ClusteredLightBuilder` 使用 16×9 屏幕 Tile、24 个对数深度 Slice 和每 Cluster 64 灯上限生成 Offset、Count、Light Index。方向光进入全部 Cluster；点光与聚光灯按 View Space 包围范围投影到 Tile/Slice 区间。Light 数据和 Cluster 表通过 OpenGL Texture Buffer 上传，PBR Fragment Shader 根据像素坐标和 View Depth 只遍历当前 Cluster。

## 3. Motion Vector TAA

场景 FBO 使用两个颜色附件：RGBA16F HDR 与 RG16F Motion Vector。Renderer 保存上一帧 ViewProjection 和逐 Entity Model；Vertex Shader 同时输出当前与上一帧 Clip Position，Fragment Shader 写出 UV 空间速度。

TAA 使用两张 RGBA16F History Ping-Pong：

- 用 Motion Vector 计算上一帧采样 UV。
- 计算当前帧 3×3 邻域最小/最大颜色并 Clamp History，降低鬼影。
- 根据速度在 0.92 到 0.72 之间调整历史权重。
- 首帧、Resize 或关闭 TAA 时使 History 失效。

## 4. 独立 HZB、GTAO 与 Bloom

- HZB：从 Scene Depth 开始，以保守最大深度执行 2×2 降采样，逐级生成到 1×1。
- GTAO：半分辨率、八方向、每方向四步地平线近似；关闭时写入白色遮蔽图。
- Bloom：五级独立 RGBA16F 金字塔，第一级亮部软阈值提取，后续逐级降采样和模糊；TAA Composite 按权重合并五级。
- Tone Map：在 Temporal Resolve 后执行曝光、ACES、Gamma，并保留可选 FXAA。

各阶段拥有独立 Shader、Framebuffer Attachment 和 Render Graph Pass，不再塞进单个 Tone Shader。

## 5. GPU HZB 剔除与 Indirect Draw

不透明、无蒙皮且 Mesh/Material 相同的对象先形成实例批次。OpenGL 4.3 Compute Shader 读取当前实例 Model 与 World Bounds，使用上一帧 ViewProjection 投影包围球，并采样上一帧 HZB：

- 屏幕外实例直接剔除。
- 根据投影直径选择 HZB Mip。
- 通过原子加法把可见 Model Matrix 紧凑写入实例 Buffer。
- 同一个 Compute Dispatch 直接更新 `DrawElementsIndirectCommand.instanceCount`。
- Memory Barrier 后由 `glDrawElementsIndirect` 提交。

透明、蒙皮和单实例对象保持独立绘制。Compute/Indirect 加载失败时仍可使用 CPU 已收集实例和直接绘制回退；0.27 默认创建 OpenGL 4.3 Core Context。

## 6. 编辑器、控制台与统计

Runtime CVar：

- `r.Bloom`
- `r.GTAO`
- `r.TAA`
- `r.FXAA`

编辑器 Scene View 暴露 Bloom、GTAO、TAA、FXAA 和曝光。Console / Stats 显示 CPU/GPU 帧耗时、Draw/Cull/LOD、Render Graph Pass 数与物理资源数、Cluster Light Reference 数、HZB 层数，以及 Compute + Indirect、CPU Cull + Indirect 或 Fallback 状态。Diagnostics 同步记录 `render.graph_passes`、`render.graph_physical_resources` 和 `render.cluster_light_references`。

## 7. 验证

本轮验证命令：

```powershell
cmake --build build --config Debug --target tinyengine_tests tinyengine_demo tinyengine_editor -j 4
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\tinyengine_demo.exe 5
.\build\Debug\tinyengine_editor.exe TinyEngine.tinyproject --frames=5
```

核心测试覆盖 Render Graph 的编译/执行/错误检测/资源别名、Clustered Light 分配和 Renderer 九 Pass 帧图；真实 FBX/glTF 兼容测试与核心测试均通过。

## 8. 当前边界

- Render Graph 尚未抽象真实 GPU Barrier、Queue、Descriptor 或跨 API Physical Resource Pool。
- Cluster 表目前由 CPU 构建；还不是 GPU Cluster Build。
- GPU Driven 路径按实例批次分别 Dispatch/Draw，尚未合并为全场景 Multi Draw Indirect。
- HZB 使用上一帧结果，快速遮挡变化通过保守 Bounds 和深度偏移降低误剔除，但未加入两阶段 Early/Late Occlusion。
- GTAO 是轻量地平线近似，未包含完整法线重建、时域降噪和空间双边滤波。
- TAA 已有 Motion Vector 与邻域约束，但没有 Reactive Mask、透明速度、曝光补偿或现代 TAAU。
- 当前只有 OpenGL 4.3 后端；Vulkan/D3D12/Metal、异步 Compute、Bindless、Mesh Shader 和 Virtual Geometry 属于后续生产级升级。
