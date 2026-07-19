# TinyEngine 0.26 动画、VFX、音频、插件、导航 AI 与联网完成说明

记录日期：2026-07-16

本轮在 0.25 的渲染、资产、物理和编辑器闭环上补充角色表现、扩展机制、AI 导航与多人游戏基础。实现继续遵守轻量原则：接口和数据边界参考 UE 的职责划分，但不复制 UObject、Gameplay Ability System、Recast/Detour 或 Online Subsystem。

## 动画

- `SkeletonRetargeter` 支持按同名骨骼自动建表，也允许显式指定源/目标骨骼和位移比例。
- `SolveTwoBoneIk` 提供带 Pole Vector 和权重的解析式双骨骼 IK。
- `TINY_SKANIM 3` 保存 Root Motion 位移曲线；运行时支持普通、循环和跨循环区间的增量提取。
- `BlendTree2D` 使用距离权重混合任意二维样本；`BlendAnimationLayers` 支持逐骨骼遮罩、Override 和 Additive 层。
- `BuildAnimationDebugSnapshot` 输出关节世界位置和父子连线；Animation Editor 增加时间采样、Root Motion 和同名 Skeleton 调试信息。

当前重定向是名称/显式映射，不包含 UE IK Rig 的链定义和 Retarget Pose；IK 是双骨骼求解器，不是 Full Body IK。

## VFX

- `.vfx` 资产保存容量、发射、重力、生命周期颜色/尺寸、纹理、Flipbook 行列/FPS/循环和 CPU/GPU 模式。
- CPU 粒子计算 Flipbook 帧，Renderer 按纹理分组，把位置、尺寸、颜色和 UV Rect 作为实例数据一次提交。
- GPU 模式不生成 CPU 粒子。OpenGL 3.3 Vertex Shader 根据发射时间、实例编号、速度、扩散、重力和生命周期直接计算粒子状态并实例化绘制，因此没有逐帧 CPU 粒子积分或 GPU 回读。
- CPU/GPU 路径均可采样 Flipbook 纹理；没有纹理时保留原有软边圆形 Billboard。
- VFX Editor 可创建和编辑 `.vfx`，包括模拟后端和 Flipbook 参数。

GPU 模式适用于连续弹道型发射器；GPU Burst、碰撞、排序、Ribbon 和事件回读仍属于后续高级特效能力。

## 音频与输入

- `stb_vorbis` 从 `TINYENGINE_STB_DIR` 接入；本机缺少源码时 CMake 自动获取 stb。
- `.ogg` Voice 保留压缩数据，仅按小块解码 PCM；Mixer 按源采样率步进到 44.1 kHz，支持单次和循环长音频，不把完整 PCM 展开到内存。
- `InputSystem::GetGlyph` 提供 Keyboard/Mouse、Xbox、PlayStation 和 Nintendo 显示名回退。
- `InputSystem::Rumble` 通过后端回调调用 `SDL_GameControllerRumble`，频率强度先被限制到有效范围，并可查询手柄连接状态。

## 稳定原生插件 ABI

- `PluginAPI.h` 只暴露固定宽度整数、指针、长度和 C 调用约定，不让 STL、RTTI、异常或引擎对象跨动态库边界。
- Host/Plugin 结构均携带 ABI 版本和结构大小；插件通过唯一入口 `TinyPlugin_GetAPI` 完成协商。
- `PluginManager` 支持动态库和静态插件、重名拒绝、Startup 回滚、逆序 Shutdown、Tick、宿主日志及跨边界对齐内存分配。
- `EngineContext` 暴露 PluginManager；Engine 主循环驱动插件 Tick，并在其他核心服务销毁前统一卸载插件。

插件 ABI 当前只提供生命周期、日志和内存基础。组件注册、反射和渲染扩展应通过后续版本追加结构尾字段，不能破坏 ABI v1。

## 导航与 AI

- `PolygonNavMesh` 从三角网格构建可行走多边形，按共享边建立邻接，并在多边形走廊上执行 A*。
- 动态障碍具有稳定 ID，可新增、移动、删除并重建阻挡标记。
- `NavCrowd` 提供路径跟随、速度限制和局部分离避让。
- `EnvironmentQuery` 可生成圆形网格或路径点，并用多个加权测试筛选、排序结果。
- `BehaviorTreeGraph` 保存节点、子边、编辑器坐标和 Root，加载时拒绝重复 ID、缺失引用和循环；编辑器提供节点预览、拖动、增删及保存。

这是 Recast 风格的轻量多边形运行时，不包含 Recast 的体素化、区域腐蚀、轮廓简化和 Detour Funnel；复杂开放世界烘焙仍建议接入正式 Recast/Detour 后端。

## 联网

- `UdpSocket` 提供 IPv4 UDP Bind/Send/Receive、临时端口、非阻塞模式、地址校验和 1200 字节 Datagram 上限。
- `ReliableChannel` 使用 Sequence、Ack、32 位 Ack Window、重复包抑制、重发间隔、队列上限和尝试次数上限。
- `RpcRegistry` 使用显式白名单 ID、1024 字节 Payload 上限、严格包长检查和逐客户端 Token Bucket。
- `NetworkOwnership` 验证实体所有者，拒绝其他客户端释放或覆盖。
- `ClientPrediction` 保存有界输入历史，应用本地预测，并在服务器确认后重放未确认输入、平滑小误差或吸附大误差。

当前层提供可组合的传输和同步原语，不包含大厅/匹配、NAT 穿透、加密认证、连接迁移、分片大包或反作弊服务。

## 验证

- Debug 全目标编译：Runtime、Editor、Game、Assimp importer、工具和测试。
- `tinyengine_core` 覆盖本轮动画、VFX 资产/GPU 命令、Glyph/震动、插件 ABI、NavMesh/Crowd/EQS、行为树图、可靠通道、RPC、Ownership、预测和 UDP 边界。
- `tinyengine_import_compatibility` 继续覆盖真实 FBX/glTF 导入矩阵。
- SDL2/OpenGL Demo 运行三帧，所有辅助 Shader（包括 GPU 粒子）成功编译链接并正常退出。
