# TinyEngine 使用指南

## 1. 打开示例项目

完成 Debug 构建后运行：

```powershell
.\build\Debug\tinyengine_editor.exe TinyEngine.tinyproject
```

项目文件中的路径相对于 `.tinyproject` 所在目录解析。示例项目使用：

```ini
project.name=TinyEngineDemo
project.assets=assets
project.startupScene=assets/Demo.scene
project.cooked=Saved/Cooked
input.bindings=assets/default.input
physics.fixedStep=0.016666667
```

主要字段：

- `project.name`：编辑器标题和打包后的可执行文件基础名。
- `project.assets`：资产根目录。
- `project.startupScene`：启动场景。
- `project.cooked`：Cook 输出目录。
- `project.package`：发布项目使用的 TPAK；通常由打包器写入。
- `input.bindings`：`.input` 映射资产。
- `render.environment`：可选 `.env` 环境资产。
- `physics.fixedStep`：固定物理步长。

## 2. 编辑器布局与操作

- Hierarchy：实体层级、选择和批量删除。
- Inspector：通过反射编辑组件，支持组件增删、资产类型校验和 Undo。
- Scene View：编辑器相机、拾取、框选和 Gizmo。
- Assets：扫描资产、模型导入、缩略图、依赖修复和专用资产编辑器。
- Console：查看日志、执行命令和修改 CVar。

Scene View 快捷操作：

- 按住鼠标右键并使用 `W/A/S/D/Q/E` 飞行，`Shift` 加速。
- 左键单选；拖动空白区域框选；`Ctrl` 追加或移除选择。
- `W/E/R` 切换平移、旋转、缩放 Gizmo。
- Gizmo 支持 Local/World 和 Center/Active/Individual Pivot。
- `Ctrl+Z` / `Ctrl+Y`：Undo / Redo。
- `Ctrl+S`：保存当前场景。
- `Ctrl+C` / `Ctrl+V`：通过临时 Prefab 复制和粘贴实体。

编辑器进入 Play 前保存场景内存快照，退出 Play 时恢复，因此运行时修改不会污染编辑场景。未保存场景会定期写入 `Saved/EditorAutosave.scene`。

## 3. 导入模型

编辑器 Assets 面板选择“Import Model”，或使用：

```powershell
.\build\Debug\tinyengine_asset_tool.exe import `
  D:\Models\Character.fbx assets/Character assets/Character
```

Assimp 导入器会按源内容生成 `.mesh`、`.mat`、`.prefab`、`.skeleton` 和 `.skanim`，并生成稳定 `.meta`。蒙皮顶点保留权重最高的四根骨骼并归一化；同一模型的多个 Mesh 使用统一骨骼编号。

导入是 Authoring 流程。独立游戏不读取 FBX/glTF，而只读取转换后的轻量资产。

## 4. 创建和编辑专用资产

Assets 面板可创建：

- `.blendtree`：1D 动画混合树。
- `.animgraph`：参数、状态、转换和交叉淡化。
- `.vfx`：发射、生命周期、纹理 Flipbook 与 CPU/GPU 模式。
- `.btgraph`：行为树节点图。
- `.matgraph`：可视化材质节点图。

Shader Editor 编辑 `.vert/.frag` 时会调用真实 OpenGL 编译器。失败时保留上一份有效 Program，并显示驱动错误与行号。

## 5. 资产维护

扫描资产：

```powershell
.\build\Debug\tinyengine_asset_tool.exe scan assets
```

查找缺失依赖：

```powershell
.\build\Debug\tinyengine_asset_tool.exe missing assets
```

生成缩略图：

```powershell
.\build\Debug\tinyengine_asset_tool.exe thumbnail assets/player.mesh Saved/player.ppm
```

大型文件分块：

```powershell
.\build\Debug\tinyengine_asset_tool.exe chunk Movie.bin Saved/Movie.chunkmanifest 4194304
```

移动或重命名资产应从编辑器资产操作入口完成，使 `.meta`、依赖和文本引用同步更新。不要只在文件管理器中移动单个资产。

## 6. 环境光照

```powershell
.\build\Debug\tinyengine_asset_tool.exe bake-env `
  assets/Environment.hdr assets/EnvironmentBaked
```

产物包含六面 Irradiance Atlas、六级 GGX Prefilter Atlas、BRDF LUT 和 `.env` 清单。把 `.env` 路径写入项目的 `render.environment`。

## 7. 运行

开发 Demo：

```powershell
.\build\Debug\tinyengine_demo.exe
```

参数 `3` 表示只运行三帧，适合自动化冒烟：

```powershell
.\build\Debug\tinyengine_demo.exe 3
```

独立 Runtime：

```powershell
.\build\Debug\tinyengine_game.exe TinyEngine.tinyproject --frames=3
```

## 8. 校验、Cook 和打包

提交或打包前建议依次执行：

```powershell
.\build\Debug\tinyengine_commandlet.exe ValidateAssets assets
.\build\Debug\tinyengine_commandlet.exe ValidateScenes Saved
.\build\Debug\tinyengine_commandlet.exe CookProject `
  TinyEngine.tinyproject Saved/PlatformCook Windows Shipping
```

一键生成示例游戏包：

```powershell
cmake --build build --config Release --target package_game
```

或显式调用打包器：

```powershell
.\build\Release\tinyengine_package_tool.exe `
  TinyEngine.tinyproject `
  Saved/Package/TinyEngineDemo `
  .\build\Release\tinyengine_game.exe
```

发布目录包含重命名后的游戏可执行文件、Shipping `.tinyproject`、`Content/Game.tpak`、`Package.manifest` 和运行提示。打包器拒绝绝对资产路径、`..` 路径和危险输出目录，并验证启动场景确实进入 TPAK。

## 9. 当前适用范围

TinyEngine 0.26 适合小型 3D 游戏、工具可视化、教学和引擎实验。它已经形成可运行闭环，但没有完整 RHI、商业级 Recast 烘焙、加密联网服务、Full Body IK、现代 GPU Driven 渲染或完整 OpenType 塑形。具体边界见[后续工作](REMAINING_WORK.md)。
