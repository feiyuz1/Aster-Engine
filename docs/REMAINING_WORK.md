# TinyEngine 后续工作记录

记录日期：2026-07-16
当前版本：0.26.0

0.25 的渲染/资产/物理/UI 与 0.26 的动画/VFX/音频/插件/导航 AI/联网能力均已进入可编译、可测试、可运行的代码路径。完成范围和实现边界见 `ENGINE_COMPLETION_025.md` 与 `ENGINE_COMPLETION_026.md`。

以下内容不是本轮遗漏，而是从“轻量可用引擎”继续向生产级大型引擎提升时的后续项。

## P1 质量与规模升级

- 渲染：Render Graph、GPU HZB、Motion Vector、分离式多级 Bloom、GTAO、可调 TAA Resolve、Clustered/Forward+ Lighting、GPU Driven Indirect Draw。
- Shader：跨平台 HLSL/GLSL 编译链、SPIR-V、PSO Cache、异步编译、Graph Subgraph/Function、完整材质预览球场景。
- 资产：接入目标平台 BC/ASTC 编码器、Basis Universal 超压缩、异步文件 I/O、远程 DDC、导入 Worker 进程隔离。
- 物理：引入 Jolt 源码后的跨平台适配认证、复杂接触流形、车辆、布料、破坏和物理回放调试。
- 文字：可选 HarfBuzz 完整 OpenType Script Shaping、Fallback Font Chain、IME 候选窗、文字选区与剪贴板。
- 编辑器：多 Viewport、正交视图、可停靠布局持久化、Scene Outliner 搜索/过滤、完整资产 Diff/Merge。

## 其他引擎模块

- 动画：IK Rig/Retarget Pose、Full Body IK、Motion Warping、蒙太奇和动画性能分析器。
- VFX：GPU Burst、深度碰撞、透明排序、Ribbon、Sub-emitter 和事件回读。
- 音频：Opus、效果器链、可视化 Mixer、音频资源 Cook 和平台硬件解码。
- 导航/AI：正式 Recast 体素烘焙、Detour Funnel/Tile Cache、复杂 Crowd 避障与异步查询。
- 网络：连接握手、加密认证、NAT 穿透、会话/匹配、分片、大规模 Replication Graph 和反作弊。
- World Partition：异步 Scene Cell 合并、HLOD 和编辑器分区烘焙。
- Sequencer/地形/媒体：属性绑定与音轨、地形 Chunk LOD/植被/虚拟纹理、视频解码与音画同步。

## 发布工程

- 增加格式 CI Gate、渲染截图基线、逐分配内存采样、API 文档和发行说明自动生成。
