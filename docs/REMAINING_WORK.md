# TinyEngine 未完成工作记录

记录日期：2026-07-14

当前已完成：线性 Radiance HDR、颜色/数据纹理色彩空间、Mipmap、运行时 Tangent、六类 PBR 贴图、最多四盏方向光、程序化天空环境、Assimp 对应贴图导入，以及 standalone game/TPAK 项目打包发布闭环。

## P0 未完成

1. 渲染：Cubemap 离线预过滤、BRDF LUT、各向异性过滤、级联阴影、Normal Matrix、透明阴影、实例化和遮挡裁剪；HDR equirect 漫反射/镜面 IBL、FXAA、LOD 和 GPU profiler 已完成。
2. Shader/材质：材质 Inspector、材质实例和贴图导入通道规则；include、宏/variant、预处理缓存、依赖热重载和完整错误输出已完成。
3. 资产：DDS/KTX2/BCn、缩略图、丢失依赖修复和平台资产配置；后台导入 worker、进度/取消与清单增量 Cook 已完成。
4. Runtime UI：Unicode/TTF 字体资产和多语言复杂排版；ASCII Atlas、文本/图片、锚点、父子布局、祖先裁剪、滚动、DPI、键盘和手柄导航已完成。
5. 物理：精确 Convex/TriangleMesh 窄相位、完整 CharacterController Sweep/Slide 与可选 Jolt 后端；连续子步、Mesh 包围体、Joint、冲量响应和调试数据已完成。
6. 编辑器：多选/框选、Local Gizmo 和 Shader 文本编辑器；多场景标签、Material 编辑器、属性反射、自动 Inspector、场景脏标记/自动恢复和 Inspector 类型校验拖放已完成。

## P1 未完成

- 骨骼、蒙皮、Animation 状态机和混合树；Transform Animation Clip、交叉淡化和 Root Motion 已完成。
- 粒子、Bloom、SSAO、TAA、实例化和遮挡裁剪；FXAA 与 LOD 已完成。
- OGG 解码与长音频流式播放；空间音频、并发 Voice 和 Mixer/Bus 已完成。
- 平台输入 Glyph 和手柄震动工具；输入映射资产、运行时重绑定和手柄热插拔已完成。
- Lua/C# 或稳定的原生插件 ABI。
- 导航网格、AI 寻路；联网项目还需网络同步、预测和回放。

## 发布工程未完成

- 格式检查、无界面编辑器自动化、内存采样和数据版本迁移工具；Windows CI、打包冒烟、Crash Dump、结构化日志、CPU/GPU Profiler 和许可证清单已完成。
- 更完整的示例项目、API 文档和发行说明生成。
