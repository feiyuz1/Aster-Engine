# TinyEngine

TinyEngine 是一个参考 Unreal Engine 子系统边界、按轻量目标重新实现的 C++17 3D 引擎核心。它不是 UE 源码的裁剪副本，而是保留 Core、Application、Asset、World、RHI、Physics、Audio、Animation 和 Script 的最小闭环。

## 已实现

- Core：日志、事件、配置、模块生命周期、代际 Handle
- Platform：文件、路径、计时器、文件变化检测
- Math：向量、四元数、矩阵、Transform、投影、Ray、AABB
- Memory：帧线性分配器、线程安全定长对象池
- Task：工作线程、四级优先级、依赖、等待、异常传播、安全关机
- ECS/Scene：代际 Entity、组件 Query、父子层级、递归销毁、版本化场景保存/加载
- Asset：注册表、同步/异步加载、状态等待、错误信息、网格/材质/Shader 资源、TPAK 打包
- Render：SDL2、OpenGL 3.3 Core、VAO/VBO/IBO、GLSL、材质、相机、方向光、深度和背面剔除
- Physics：固定步长、重力、静态/动态/运动学刚体、Box/Sphere Collider、层过滤、Trigger、碰撞事件、Raycast
- Input：键盘、鼠标、语义化 Action/Axis
- Audio：SDL 音频设备、格式转换、音量和异步队列播放
- Animation：版本无关的 Transform 关键帧 Clip、插值、循环和速度控制
- Script：Entity/World 上下文、生命周期、Factory 替换和状态安全的实例热重建
- Tools：资产打包工具、场景查看/修改工具
- Engineering：CTest、安装规则、CPack、Debug/Release 构建选项

## 构建与测试

```powershell
cmake -S . -B build -DTINYENGINE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## 运行

```powershell
.\build\Release\tinyengine_demo.exe
.\build\Release\tinyengine_demo.exe 300
```

控制：`WASD/RF` 移动，方向键和 `Q/E` 旋转，`Z/X` 缩放，`Shift` 加速，`Esc` 退出。

## 工具

```powershell
# 打包和查看资产包
.\build\Release\tinyengine_asset_tool.exe pack assets Saved\Assets.tpak
.\build\Release\tinyengine_asset_tool.exe list Saved\Assets.tpak

# 查看和修改场景
.\build\Release\tinyengine_scene_tool.exe Saved\Demo.scene list
.\build\Release\tinyengine_scene_tool.exe Saved\Demo.scene add NewEntity
.\build\Release\tinyengine_scene_tool.exe Saved\Demo.scene transform 0 0 2 0 0 45 0 1 1 1
```

## 设计边界

当前实现的是轻量引擎核心，不包含 UE 的 Nanite、Lumen、Niagara、World Partition、Blueprint、网络服务、VR/AR、电影系统和完整可视化编辑器。这些属于游戏或产品方向扩展，不进入核心依赖。完整实现约束见 [技术设计文档](设计.md)，精简分层见 [架构说明](docs/ARCHITECTURE.md)。
