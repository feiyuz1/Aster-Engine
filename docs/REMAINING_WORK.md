# TinyEngine 未完成工作记录

记录日期：2026-07-14

本停止点已经完成：线性 Radiance HDR、颜色/数据纹理色彩空间、Mipmap、运行时 Tangent、六类 PBR 贴图、最多四盏方向光、程序化天空环境，以及 Assimp 对应贴图导入。

## P0 未完成

1. 渲染：Cubemap Skybox、漫反射/镜面 IBL、预过滤环境贴图、BRDF LUT、各向异性过滤、级联阴影、Normal Matrix、透明阴影、抗锯齿和 GPU profiler。
2. Shader/材质：include、宏与 variant、编译缓存、完整错误面板、材质 Inspector、材质实例和贴图导入通道规则。
3. 资产：DDS/KTX2/BCn、后台导入 worker、进度/取消、增量 Cook、缩略图、丢失依赖修复和平台资产配置。
4. Runtime UI：字体 Atlas、文本绘制、锚点/布局、裁剪/滚动、图片采样、DPI、键盘和手柄导航。
5. 物理：Jolt 后端、Convex/TriangleMesh、准确窄相位、CCD、Joint、完整 CharacterController、Sweep/Slide 和调试绘制。
6. 编辑器：属性反射、自动 Inspector、场景脏标记/自动恢复、多场景标签、多选/框选、Local Gizmo、Inspector 拖放、Material/Shader 编辑器。

## P1 未完成

- 骨骼、蒙皮、Animation Clip、状态机、混合树、Root Motion。
- 粒子、Bloom、SSAO、TAA/FXAA、LOD、实例化和遮挡裁剪。
- 空间音频、Mixer/Bus、OGG 流式播放。
- 输入映射资产、重绑定和手柄热插拔。
- Lua/C# 或稳定的原生插件 ABI。
- 导航网格、AI 寻路；联网项目还需网络同步、预测和回放。

## 发布工程未完成

- Windows CI、格式检查、编辑器自动化与打包冒烟测试。
- Crash Dump、结构化日志、CPU/内存/GPU Profiler。
- 许可证清单、示例项目、API 文档和数据版本迁移工具。
- 当前工作区另有一组尚未提交的 standalone game/package pipeline 改动，需要独立完成测试和 Git 提交，不能与本次渲染提交混合。
