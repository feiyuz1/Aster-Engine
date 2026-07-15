# TinyEngine 后续工作记录

记录日期：2026-07-16
当前版本：0.25.0

用户在 0.25 批次指定的渲染、Shader/材质、资产管线、物理、Runtime UI/文字和编辑器核心功能均已进入可编译、可测试、可运行的代码路径。完成范围和实现边界见 `ENGINE_COMPLETION_025.md`。

以下内容不是本轮遗漏，而是从“轻量可用引擎”继续向生产级大型引擎提升时的后续项。

## P1 质量与规模升级

- 渲染：Render Graph、GPU HZB、Motion Vector、分离式多级 Bloom、GTAO、可调 TAA Resolve、Clustered/Forward+ Lighting、GPU Driven Indirect Draw。
- Shader：跨平台 HLSL/GLSL 编译链、SPIR-V、PSO Cache、异步编译、Graph Subgraph/Function、完整材质预览球场景。
- 资产：接入目标平台 BC/ASTC 编码器、Basis Universal 超压缩、异步文件 I/O、远程 DDC、导入 Worker 进程隔离。
- 物理：引入 Jolt 源码后的跨平台适配认证、复杂接触流形、车辆、布料、破坏和物理回放调试。
- 文字：可选 HarfBuzz 完整 OpenType Script Shaping、Fallback Font Chain、IME 候选窗、文字选区与剪贴板。
- 编辑器：多 Viewport、正交视图、可停靠布局持久化、Scene Outliner 搜索/过滤、完整资产 Diff/Merge。

## 其他引擎模块

- 动画：骨骼重定向、IK、2D Blend Space、分层混合和动画调试视图。
- 音频：OGG/Opus 长音频流式解码、效果器和可视化 Mixer。
- 导航/AI：Recast NavMesh、动态障碍、Crowd、EQS 和行为树编辑器。
- 网络：UDP/可靠通道、RPC、Ownership、客户端预测、服务器纠错与安全边界。
- World Partition：异步 Scene Cell 合并、HLOD 和编辑器分区烘焙。
- Sequencer/地形/媒体：属性绑定与音轨、地形 Chunk LOD/植被/虚拟纹理、视频解码与音画同步。

## 发布工程

- 增加格式 CI Gate、渲染截图基线、逐分配内存采样、API 文档和发行说明自动生成。
