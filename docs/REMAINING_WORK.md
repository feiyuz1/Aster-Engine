# TinyEngine 未完成工作记录

记录日期：2026-07-15

当前已完成：线性 Radiance HDR、颜色/数据纹理色彩空间、Mipmap、运行时 Tangent、六类 PBR 贴图、最多四盏方向光、程序化天空环境、Assimp 对应贴图导入，以及 standalone game/TPAK 项目打包发布闭环。

## P0 未完成

1. 渲染：Cubemap 离线预过滤、BRDF LUT、各向异性过滤、级联阴影、Normal Matrix、透明阴影、实例化和遮挡裁剪；HDR equirect 漫反射/镜面 IBL、FXAA、LOD 和 GPU profiler 已完成。
2. Shader/材质：材质实例运行时继承和 Inspector 已完成；仍缺实例覆盖开关、贴图导入通道规则、材质图和 Shader 参数反射。include、宏/variant、预处理缓存、依赖热重载和完整错误输出已完成。
3. 资产：DDS/KTX2/BCn 编译器、缩略图和交互式丢失依赖修复；运行时 Mesh/Texture 二进制、AssetHandle、依赖闭包、CPU/GPU 驻留预算、Windows/Linux Cook Profile、后台导入和增量 Cook 已完成。
4. Runtime UI：Windows 中文 TrueType 单字栅格化已完成；仍缺动态 Unicode Atlas、非 Windows FreeType、BiDi/连字/断行。严格 UTF-8、本地化、ASCII Atlas、布局、裁剪、滚动、DPI 和导航已完成。
5. 物理：精确 Convex/TriangleMesh 窄相位、连续 Shape Sweep、斜坡 Slide 和可选 Jolt 后端；基础 Pawn/Character、台阶尝试/接地/碰顶、连续子步、Mesh 包围体、Joint、冲量响应和调试数据已完成。
6. 编辑器：Ctrl 多选和批量删除已完成；仍缺框选、批量 Gizmo、Local Gizmo 和 Shader 文本编辑器。

## P1 未完成

- Assimp 骨骼/权重/Clip 导入、GPU 蒙皮、Blend Tree 与 Animation Graph 编辑器；Skeleton、关键帧采样、蒙皮矩阵、条件状态机、Transform Clip、交叉淡化和 Root Motion 已完成。
- 粒子 CPU 仿真和 OpenGL Billboard Renderer 已完成；仍缺纹理 Flipbook、批量实例化/GPU 仿真与特效编辑器。Bloom、SSAO、TAA、实例化和遮挡裁剪仍未完成；FXAA 与 LOD 已完成。
- OGG 解码与长音频流式播放；空间音频、并发 Voice 和 Mixer/Bus 已完成。
- 平台输入 Glyph 和手柄震动工具；输入映射资产、运行时重绑定和手柄热插拔已完成。
- Lua/C# 或稳定的原生插件 ABI。
- Recast 风格多边形 NavMesh、动态障碍重建、Crowd、EQS 与可视化行为树编辑器；网格 A*、Blackboard 和运行时行为树节点已完成。
- 联网已有快照编解码、抖动缓冲插值和回放文件；仍缺 UDP/可靠通道、RPC、Ownership、客户端预测、服务器纠错和安全边界。
- World Partition 已有距离/迟滞流送协调器，但异步场景 Cell 合并、HLOD 和编辑器分区烘焙仍未完成。
- Sequencer 已有 Transform Track、事件和播放控制，仍缺 ECS 属性绑定、相机切换、曲线切线、音轨和可视化编辑器。
- 高度场已有采样/法线/网格，仍缺 Chunk LOD、裂缝处理、笔刷、植被和虚拟纹理；媒体已有图片序列，仍缺视频解码和音画同步。

## 发布工程未完成

- 格式 CI 检查、渲染截图对比、逐分配内存采样和二进制场景转换；基础无头 Commandlet、Archive/迁移、CVar/Stat、Windows CI、打包冒烟、Crash Dump、结构化日志、CPU/GPU Profiler 和许可证清单已完成。
- 更完整的示例项目、API 文档和发行说明生成。
