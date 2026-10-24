# Aster-Engine 使用指南

本文对应 Aster-Engine 0.27.0，面向使用编辑器制作内容、运行项目和生成发布包的开发者。编译器与第三方依赖配置见[构建指南](build_guide.md)，命令行参数见[工具参考](../reference/tool_reference.md)，资产内部格式见[资产与数据格式](../reference/asset_data_formats.md)。

## 开始前必须知道

- 当前版本应使用支持 OpenGL 4.3 Core、Compute Shader 和 SSBO 的显卡驱动。
- 项目、场景和资产之间统一使用项目相对路径；不要把本机绝对路径写入 `.tinyproject`、场景、材质或动画资产。
- 新建未命名场景第一次保存时会进入 Save As；覆盖已有场景前会二次确认并保留 `.bak`。正式项目仍应使用 Git 管理场景。
- `tinyengine_game.exe` 会按项目资产根解析逻辑资源路径，不依赖进程工作目录；从其他目录启动时仍建议给出绝对项目路径，避免项目文件本身找错。
- Game 构建会生成 `.buildinfo`；Editor/Package Tool 打包前会校验引擎版本、ABI、场景格式、构建 ID、配置和编译器，不匹配时直接终止。

最短可用流程是：完成 Release 构建 → 打开项目 → 在已有场景副本中编辑 → Play 验证 → 运行校验和测试 → 生成发布包 → 运行包内程序。

## 1. 打开示例项目

完成构建后，在仓库根目录运行：

```powershell
.\build\Release\tinyengine_editor.exe Aster-Engine.tinyproject
```

Debug 版本便于调试，Release 版本更适合编辑器交互、性能观察和打包验收。命令应从仓库根目录执行，或者给编辑器传入 `.tinyproject` 的绝对路径。

项目文件中的路径相对于 `.tinyproject` 所在目录解析。示例项目使用：

```ini
project.name=Aster-Engine
project.assets=assets
project.startupScene=assets/Demo.scene
project.cooked=saved/Cooked
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

### 1.1 创建自己的项目

不带项目参数启动 `tinyengine_editor.exe` 会显示项目启动页。在启动页输入项目名，通过“保存到目录”右侧的“浏览...”选择已有父目录，也可以手动填写路径，然后点击“创建并打开”。Windows 目录选择窗口从当前填写的目录打开，取消选择会保留原路径；项目文件夹会自动建在所选目录中。也可以选择已有 `.tinyproject` 打开。新项目自动带有场景、基础素材、C++ 游戏代码和 `CMakeLists.txt`。在编辑器的“文件 → 生成 Visual Studio 解决方案”创建 `.sln`，然后在 VS 2022 编译项目游戏目标。完整步骤和编辑器 Play 的限制见[项目工作流](project_workflow.md)。

Windows 编辑器在创建窗口前固定 DPI 感知模式，目录窗口中的右键菜单和“新建文件夹”不会改变整个界面的缩放。界面尺寸以逻辑坐标计算，显示器 DPI 只影响渲染清晰度；需要调整字号与控件大小时使用“编辑 → 设置 → 界面缩放”。

建议的项目目录：

```text
MyTinyGame/
├─ MyTinyGame.tinyproject
├─ CMakeLists.txt
├─ src/                     # Game.cpp、Game.hpp 和 main.cpp
├─ assets/                  # Startup.scene 和基础素材
├─ build/                   # 生成的 Visual Studio 解决方案
└─ saved/                   # 缓存、日志和恢复文件
```

### 1.2 安全创建和保存场景

TE-QA-001/002 已于 2026-08-10 修复。现在可以使用 `文件 > 新建场景`，新标签会立即成为活动标签；未命名场景第一次按 `Ctrl+S` 会打开 Save As，而不会回退覆盖启动场景。

1. 选择 `文件 > 新建场景` 并确认“未命名”标签处于选中状态。
2. 按 `Ctrl+S`，输入资产目录内的项目相对路径，例如 `assets/Scenes/Level01.scene`。省略扩展名时会自动补 `.scene`。
3. 已保存场景按 `Ctrl+S` 更新原文件；按 `Ctrl+Shift+S` 或选择“场景另存为...”可更换路径。
4. Save As 指向已有文件时必须再次点击“确认覆盖”；旧内容保存在同目录 `.bak` 中。
5. 取消、路径校验失败或磁盘写入失败时，文档保持未保存状态，恢复文件不会被删除；从打包入口触发时也不会继续打包。
6. 保存后仍建议使用 `git diff -- assets/Scenes/Level01.scene` 检查修改范围。

场景与 `.tinyproject` 应纳入 Git；`saved/`、`Dist/`、构建目录和临时导入产物是否提交，以仓库 `.gitignore` 为准。
完整根因、实现和验收步骤见TE-QA-001～016 修复复盘（本地历史文档）。

## 2. 编辑器布局与操作

Aster-Engine 编辑器支持简体中文和 English，首次启动默认使用简体中文。通过 `编辑 > 设置... > 界面语言`（英文界面为 `Edit > Settings... > Interface Language`）可以即时切换；选择会保存到当前项目的 `saved/EditorSettings.ini`，下次打开该项目时自动恢复。

Windows 优先加载微软雅黑，并依次回退到黑体和宋体；Linux 优先使用 Noto Sans CJK 或文泉驿微米黑，因此运行中切换为中文不需要重启。系统缺少中文字体时会回退到 ImGui 默认字体，并在控制台输出警告。

默认工作区将层级、场景和属性放在上方，资产、控制台、性能合并为底部标签。拖动面板标签可重新停靠、合并或浮动，拖动分隔线调整大小；关闭的面板通过“视图”菜单重新打开。布局自动按项目保存；“视图 > 保存当前布局 / 载入已保存布局 / 恢复默认布局”管理布局快照。`F11` 放大场景并保留原停靠关系，再按一次还原。设置中的“界面缩放”可在 85%～150% 间调整字号和间距。

| 区域 | 位置 | 用途 |
|---|---|---|
| 场景层级 | 左上 | 查看实体父子层级，选择实体，创建和批量删除实体 |
| 场景视图 | 中上 | 编辑场景、控制编辑器相机、框选实体并操作 Gizmo |
| 属性检查器 | 右上 | 编辑所选实体的组件，或编辑资产面板中选中的材质 |
| 资产 | 底部标签 | 浏览、导入、重导入、移动、预览和修复资产 |
| 控制台、性能 | 底部标签 | 控制台执行命令和查看日志，性能标签显示 CPU/GPU/Render Graph 统计 |

资产面板选中一个或多个文件后，按 **Delete** 或右键 **删除...** 打开确认窗口。被磁盘资产、当前及其他打开的场景、撤销/重做历史、未保存的材质或着色器、项目设置引用的资产会列出引用来源，并阻止删除。确认后文件与 `.meta` 一起移入项目 `saved/DeletedAssets/`；在资产面板按 **Ctrl+Z** 或右键空白处 **恢复删除的资产** 可恢复，重新打开项目后也能找回回收站记录。同名文件已存在时恢复会停止并保留回收站内容。

**新建资产 → 材质** 创建可直接使用的节点材质，双击后在中文材质编辑器中编辑。选择已有材质后右键 **创建材质实例** 创建继承它的参数实例。两者均可拖到场景网格、属性检查器的材质字段，或右键 **应用到选中网格**。详细操作见[材质系统](../Architecture/材质系统.md)。

### 2.1 顶部菜单和运行状态

- `文件 > 保存场景`：保存当前场景，等同于 `Ctrl+S`。
- `文件 > 打开场景...`：`Ctrl+O` 打开场景列表，也可输入路径；打开后保留其他标签的未保存修改。
- `文件 > 新建场景`：创建新的场景标签。
- `文件 > 恢复自动保存`：恢复 `saved/EditorAutosave.scene`；没有恢复文件或处于运行状态时不可用。
- `文件 > 打包项目`：保存场景后在后台打包到 `Dist/<项目名>`，显示处理中状态和完成结果；打包期间可查看场景，编辑和保存暂时锁定，完成后自动恢复。完成前不能重复打包或退出；有资产导入任务时需先等待导入结束。
- `文件 > 生成 Visual Studio 解决方案`：异步调用 CMake，在项目 `build/` 目录生成 VS 2022 `.sln`，并显示完成路径或错误。
- `编辑 > 撤销/重做`：撤销或重做编辑操作。
- `编辑 > 设置...`：打开编辑器设置，在简体中文和 English 之间切换界面语言；更改立即生效并按项目保存。
- `运行`：进入运行状态，按钮会变成 `停止`，右侧状态由 `编辑中` 变为 `运行中`。
- `停止`：退出运行状态并恢复进入运行前的编辑场景快照。

编辑器进入 Play 前保存内存快照，退出 Play 时恢复，因此运行时产生的 Transform、物理或 Gameplay 修改不会污染编辑场景。未保存场景每 30 秒自动写入恢复文件。

### 2.2 场景层级

- 点击 `+ 实体` 创建空实体。
- 单击实体进行单选；按住 `Ctrl` 单击可以追加选择或从选择集中移除。
- 双击实体让编辑器相机靠近并对准该物体；有模型或碰撞体时按物体尺寸留出视距，空实体按世界位置定位。相机朝向保持不变，运行状态下不移动编辑器相机。
- 有子实体的节点可以展开或折叠，父子关系来自 `RelationshipComponent`。
- 选中实体后按 `Delete` 批量删除当前选择集。
- 主选中实体是最后选中的实体；多选时属性检查器编辑选择集的共有组件。

### 2.3 场景视图

场景视图顶部工具条从左到右包括：

- `光照 / 无光照 / 线框`：切换视口显示模式。光照显示材质与灯光效果，无光照显示基础颜色，线框查看模型三角形边线；运行场景仍使用正常光照。
- `显示`：开关地面网格与选中包围盒。
- `吸附`：启用步长吸附；`吸附设置` 分别设置平移、旋转和缩放步长。
- `世界/局部`：切换移动、旋转使用的坐标轴；缩放始终显示物体局部轴，避免世界轴与局部 Scale 分量不一致。
- `中心/活动项/独立`：设置多选 Gizmo 中心。`中心` 使用选择集中心，`活动项` 使用主实体，`独立` 分别修改各实体。
- `速度`：调整相机移动、平移和滚轮推进倍率。

地面网格固定在世界坐标的 `Y=0` 平面，红线为 X 轴、蓝线为 Z 轴。移动或转动相机时，格线位置和世界尺寸保持不变；细网格按屏幕上的间距平滑淡出，远处衔接较粗网格，不会因跨过某个相机高度突然改变整块网格的大小。

曝光、FXAA、泛光、GTAO、TAA 和着色器重载位于 `编辑 > 设置 > 渲染设置`；曝光与后处理作用于光照视图，切换显示模式不会改写这些设置。重载失败时继续使用上一份有效程序，并把错误写入控制台。

场景视图鼠标与键盘操作：

- 点击场景视图获得焦点后，`W/A/S/D` 前后左右移动，`Q/E` 下降/上升，`Shift` 加速；无需持续按住右键。右键拖动控制朝向，`Alt+左键` 围绕选中对象环绕，鼠标中键平移，滚轮推进。右上导航立方体可拖拽环绕，点击面、边或角切换观察方向，房屋按钮返回默认视角；环绕保持选中物体为中心。文本输入、弹窗和打包期间不会移动相机。
- 左键单选；拖动空白区域框选；`Ctrl` 追加或移除选择。
- 选中物体后同时显示组合 Gizmo：外侧箭头平移、圆环旋转、内侧方块缩放。拖拽时锁定当前手柄，一次拖拽只产生一条撤销记录。
- 拖动红、绿、蓝 Gizmo 轴修改 X、Y、Z；启用 Snap 后按设定步长变化。
- 从资产面板拖动 `.prefab` 到场景视图，或使用右键菜单中的“放入场景”，可以实例化完整模型层级。双击 Prefab/Mesh 只打开预览，不会修改场景。
- 拖动 `.mesh` 或 `.prefab` 时，青色包围盒和落点标记预览指针射线命中的对象包围盒表面；没有对象时使用地面。松开才创建实体，位置和创建共用一次撤销；预制体多根节点保持相对偏移。此预览使用包围盒，不是模型着色预览。
- 拖动 `.mat` 会高亮指针下的 Mesh 并显示名称，松开后应用材质；没有 Mesh 时不修改引用。
- 场景或层级面板拥有焦点时，按 `F` 聚焦主选中对象；文本输入、弹窗和拖动期间不会抢走按键。

Scene View 常显显示模式、显示菜单、吸附、坐标空间、轴心和相机速度，宽度不足时换行；后处理集中在 `编辑 > 设置`。顶部提供打开、保存按钮；层级面板可搜索对象。网格和选中包围盒仅作为编辑辅助，不写入场景。

资产面板通过“新建资产”或空白处右键创建文件夹、材质父图、材质实例、动画图、混合树、特效和行为树；资产保存在当前目录，文件夹右键则创建在该文件夹中。“筛选”直接展开可多选的类型列表，已选类型显示为可移除标签；“显示”独立控制网格、列表和缩略图大小。右键资产打开“详情 / 预览”或“依赖图”。高度不足以显示完整缩略图时自动使用紧凑列表，避免只能看见图块却看不见文件名。

编辑视图使用无逐帧采样抖动的稳定投影；运行视图仅在设备启用 TAA 时加入采样偏移，关闭 TAA 后不再继续抖动。

### 2.4 属性检查器

- 选中实体后，名称、启用、标签和层合并在“对象”区块中；其余组件可单击标题折叠，字段采用左侧标签、右侧数值布局。
- `添加组件...` 支持搜索；组件标题右侧的 `...` 或右键菜单提供“重置为默认值”和“移除组件”。这些操作均可撤销。
- XYZ 轴分别以红、绿、蓝标识，点击轴按钮可单独重置；旋转使用角度标识，窄面板会将三个轴纵向排列。
- 资产字段支持“选择...”筛选匹配类型、拖入资产、输入路径后按 Enter 或离开字段提交，以及“定位”和“清空”。“定位”会展开资产所在目录并高亮资源，同时保留当前对象属性。不存在或类型不匹配的资产不会覆盖原引用，错误显示在字段旁并弹出通知。
- 多选时只显示共有组件；“多个值”或 `--` 表示字段值不同。修改一个轴会批量应用该轴并保留其余轴的差异；每次连续编辑、重置和移除作为一次撤销操作。
- 资产面板单击资源只选中，属性检查器显示资产信息；双击或“打开 / 预览”打开对应编辑器或预览窗口。新版材质在独立编辑器中显式保存草稿；旧版材质保留原有属性编辑入口。
- 操作成功和失败会显示可关闭的短通知，同时保留控制台日志；移动资产失败还会在移动对话框中显示原因。

常用组件及组合方式：

| 组件 | 用途 | 使用要点 |
|---|---|---|
| Name / Active / Tag / Layer | 标识、启用状态和逻辑分组 | 新实体自动具有基础组件；Layer 也用于用户逻辑筛选 |
| Transform | 位置、欧拉旋转和缩放 | 父子实体按层级合成世界变换；非等比缩放由法线矩阵处理 |
| Mesh Renderer | 网格和材质 | Mesh 字段使用 `.mesh`，Material 字段使用 `.mat` |
| Level Of Detail | 两级附加网格及距离 | 距离应满足 Level 1 小于 Level 2，并为各级准备兼容材质布局的网格 |
| Camera | FOV、裁剪面和主相机 | 正常运行至少需要一个启用的 Primary Camera |
| Light | Directional、Point 或 Spot | Directional 用于太阳/CSM；Point/Spot 受 Range 和 Clustered Lighting 管理 |
| Script | 原生脚本类名 | 当前内置示例类为 `OrbitScript`；新增脚本需在 C++ 的 `ScriptEngine` 注册工厂 |
| Pawn / Character Controller | 玩家控制、行走、冲刺和跳跃 | 与输入映射配合；控制器状态在 Play 中更新 |
| Rigid Body / Collider | 动态、静态、运动学物体与碰撞形状 | 可选 Box、Sphere、Capsule、Convex Hull、Triangle Mesh；静态环境优先使用静态刚体 |
| Joint | Fixed、Distance、Hinge、Spring 约束 | Connected Entity 必须指向有效实体；需要同时设置轴、限制或弹性参数 |
| Animation | Clip、Skeleton、Graph、混合和 Root Motion | 蒙皮模型需要匹配的 `.skeleton` 与 `.skanim`；Graph 路径使用 `.animgraph` |
| Audio Source | 一次性/循环、空间音频和 Bus | 3D 声音设置 Spatial，并保证 Maximum Distance 大于 Minimum Distance |

建议先添加 Collider，再添加 Rigid Body；三角网格碰撞主要用于静态场景，动态角色优先使用 Capsule。TE-QA-014 的高速薄墙穿透特定缺陷已修复并有回归；`Continuous` 不代表全部高速、复杂形状组合都已验证，实际玩法仍需专项测试。

### 2.5 Assets 资产面板

面板顶部可以直接创建：

- `+ Blend Tree`
- `+ Animation Graph`
- `+ VFX`
- `+ Behavior Tree`

常用资产操作：

- `导入模型...`：Windows 会打开系统文件选择器，并按 Assimp 实际支持格式提供“所有支持模型”、FBX、glTF/GLB、OBJ 和“所有文件”过滤项；选定文件后再设置导入参数。
- `Reimport`：使用 `.meta` 中保存的源文件和导入设置重新生成当前资产。
- 使用目录树、搜索框、类型过滤和面包屑切换目录；窄面板通过“文件夹”打开目录树。点“筛选”直接勾选类型，多种类型之间取并集，并与搜索文字共同匹配；标签上的 x 单独移除一种类型，搜索旁的 X 一次清除搜索与全部类型筛选。“显示”菜单切换网格、列表及缩略图大小，显示设置保存到 `saved/EditorSettings.ini`。
- 单击选择资产；按住 `Ctrl` 可以多选。Mesh、Prefab、Material 和 Texture 会异步生成带磁盘缓存的缩略图，其他类型显示可辨识的类型图标。
- 资产面板拥有焦点时按 `F2`，或点击“重命名 (F2)” / 右键 `Move / Rename...`：移动资产并保留 GUID；同步更新 `.meta`、依赖、场景中的绝对/相对引用及打开场景的撤销记录。材质草稿需先保存或放弃再重命名。
- 右键 `Regenerate Thumbnail`：重新生成缩略图；多选时会批量重建所选资产。
- Prefab 或 Mesh 右键 `放入场景`，也可以使用预览区的同名按钮。
- 缺失依赖区域会列出候选资产；点击 `Repair -> <路径>` 交互式修复引用。
- 右键资产选择“依赖图”：左侧是引用它的资产，中间是当前资产，右侧是它依赖的资产，箭头从引用者指向依赖项；红色节点表示缺失引用。滚轮缩放、中键拖动画布、单击节点定位到资产浏览器、双击节点以它为中心继续查看。可调整展开层数、刷新或适应画布；最多显示 128 个节点，超出时提示截断，循环引用不会无限展开。
- 依赖图读取现有资产注册表和 `.meta` 中已记录的依赖，统一解析相对路径及 GUID；没有记录的依赖不会自动补入图中。它参考 UE Reference Viewer 的浏览方式，未增加各资源格式的依赖提取规则或 UE 的完整引用分类。
- 列表和缩略图模式均使用单击选中、双击打开；文件夹双击进入，Prefab/Mesh 双击预览，Scene 双击打开场景标签。拖动 Prefab/Mesh 到场景视图或选择“放入场景”才会创建实体。当前 Scene 拖放不会打开或合并场景。

选择不同扩展名时会打开相应工具窗口：

- `.skanim`、`.blendtree`、`.animgraph`：Animation Graph / Events。
- `.vfx`：打开特效编辑器。
- `.btgraph`：打开行为树编辑器。
- `.vert`、`.frag`：打开着色器源码编辑器。
- `.mat`：材质节点图或材质实例（统一格式）。

### 2.6 Console / Stats

输入命令后按 Enter 执行。常用示例：

```text
r.Bloom false
r.GTAO true
r.TAA true
r.FXAA false
r.GpuBudgetMB 512
r.TrimGpu
```

独立的“性能”标签显示：

- Draw Calls、CPU 剔除数、LOD 切换数和 GPU 帧耗时。
- CPU Queue、Shadow、Scene/UI 和整帧耗时。
- Render Graph Pass 数与物理资源数。
- Cluster Light Reference 数量和 HZB 层数。
- GPU Driven 当前使用 `Compute + Indirect`、`CPU cull + Indirect` 或直接绘制回退。

### 2.7 常用快捷键

- `Ctrl+Z` / `Ctrl+Y`：Undo / Redo。
- `Ctrl+S`：保存当前场景。
- `Ctrl+C` / `Ctrl+V`：通过临时 Prefab 复制和粘贴实体。
- `Delete`：删除选择集。
- `F2`：在资产面板中重命名所选资源；`F11` 放大或还原场景视图。
- `W/A/S/D/Q/E`：移动有焦点的场景视图相机；选中物体直接使用组合 Gizmo，无需切换模式。

## 3. 导入模型

编辑器 Assets 面板点击“导入模型...”后，先在 Windows 系统文件选择器中选择源模型。选择器会记住最近目录，取消不会修改当前导入参数或输出错误。随后在参数弹窗中确认：

| 选项 | 含义 | 建议 |
|---|---|---|
| 源文件 | 系统选择器返回的 FBX、glTF、GLB、OBJ 等 Assimp 支持源模型 | 界面会即时校验文件存在、可读且扩展名受支持；该路径只写入 `.meta` 的重导入信息，不应被 Runtime 引用 |
| 缩放 | 导入时统一缩放 | 先确认 DCC 单位；不要用实体 Transform 长期补偿错误导入单位 |
| 翻转 UV | 翻转纹理 V 方向 | 只在贴图上下颠倒时启用 |
| 压缩动画 | 导入后压缩位置、旋转和缩放曲线 | 正式资产建议开启，并通过误差参数控制质量 |
| 位置/旋转/缩放误差 | 曲线关键帧移除容差 | 默认 `0.001`、`0.1°`、`0.001`；高精度机械动画应降低容差 |

点击导入后任务在后台执行，Assets 面板显示 Queued/Importing 进度并允许取消。同一时刻编辑器只提交一个活动导入任务。输出固定进入 `assets/Imported/<源文件名>/`，完成后 Registry 自动重新扫描、切换到输出目录并选中根 Prefab。

把导入模型放入场景：

1. 导入完成后，Assets 面板会进入 `assets/Imported/<源文件名>/` 并选中类型为 Prefab、扩展名为 `.prefab` 的根资产。完整模型应优先使用 Prefab，它会保留节点层级、各子 Mesh 和材质绑定。
2. 点击预览区的 `放入场景`，或右键 Prefab 选择 `放入场景`；也可以把它拖到场景视图。双击缩略图仅打开预览。成功后场景层级会增加实体，通知和 Console 显示“已放入场景”。
3. 拖放实例出现在指针预览落点；右键“放入场景”使用资产原点。之后可通过 Gizmo 或 Transform 继续调整位置。
4. 只需放置某个子网格时，可以对对应 `.mesh` 使用相同操作；编辑器会创建新的 Mesh 实体，不会覆盖当前选中实体。
5. 材质 `.mat` 必须拖到已经具有 Mesh Renderer 的选中实体上。

拖动资产时会显示“拖到场景视图中”提示；导入完成通知、自动选中 Prefab、右键/预览区“放入场景”入口已经补齐。Windows 原生文件选择和资产网格见编辑器需求清单（本地历史文档）。

也可以使用命令行：

```powershell
.\build\Debug\tinyengine_asset_tool.exe import `
  D:\Models\Character.fbx assets/Character assets/Character
```

Assimp 导入器会按源内容生成 `.mesh`、`.mat`、`.prefab`、`.skeleton` 和 `.skanim`，并生成稳定 `.meta`。蒙皮顶点保留权重最高的四根骨骼并归一化；同一模型的多个 Mesh 使用统一骨骼编号。

导入后按以下顺序验收：

1. 检查 Console 中的导入 warning；需要核对骨骼、顶点/三角形数量和动画压缩率时，运行真实导入兼容测试并查看 `saved/ImportCompatibility/REPORT.md`。
2. 在 Assets 面板选择生成的 Prefab，确认完整层级/材质缩略图、依赖和源文件记录；缩略图缓存位于 `saved/Thumbnails/`，源资产或依赖更新后会自动失效。
3. 把 Prefab 拖入测试场景，检查材质、法线、UV、比例和骨骼姿势。
4. 对动画资产打开 Animation Graph / Events 窗口，在 Debug Time 上采样关键姿势。
5. 源模型变化后选择生成的 Prefab，再点击 `重新导入`；不要删除 `.meta`，否则 GUID 和重导入设置会丢失。

贴图不会在导入时自动离线编码为 BC1–BC7。DDS/KTX2 可由 Runtime 直接上传，但平台压缩文件应由外部纹理工具生成；颜色贴图使用 sRGB，Normal、Metallic、Roughness、AO、ORM/RMA 保持线性。

导入是 Authoring 流程。独立游戏不读取 FBX/glTF，而只读取转换后的轻量资产。

## 4. 创建和编辑专用资产

Assets 面板可直接创建：

- `.blendtree`：1D 动画混合树。
- `.animgraph`：参数、状态、转换和交叉淡化。
- `.vfx`：发射、生命周期、纹理 Flipbook 与 CPU/GPU 模式。
- `.btgraph`：行为树节点图。

资产面板的新建菜单中选择“材质”可生成带 PBR 节点连线的 `.mat`；选择“材质实例”则引用当前选中的材质。双击普通材质打开节点编辑器，双击实例打开参数覆盖编辑器，并可点击“打开父材质”查看父图。

着色器源码编辑器编辑 `.vert/.frag` 时只修改内存缓冲区，显式保存后才调用真实 OpenGL 编译器。编译失败时保留上一份有效 Program，并显示驱动错误与行号；源码保存成功和编译成功是两个独立结果。

### 4.1 动画 Clip、Blend Tree 和 Animation Graph

模型导入后会生成一个 `.skeleton` 和一个或多个 `.skanim`。导入器会把片段到骨架和蒙皮网格的关联写入 `.meta` 的 GUID 依赖，动作不需要与骨架同名。源动作名称相同或文件名规范化后重名时，输出文件会自动加后缀，避免互相覆盖。

双击 `.skanim` 打开独立的“动画片段”窗口：

1. 预览区同时显示蒙皮模型轮廓与骨架，拖动旋转、滚轮缩放；可分别隐藏模型或骨架，点击“适配视图”重新取景。预览使用简化的无材质显示，最终材质效果请在场景中检查。
2. 使用播放、暂停、复位、速率和循环预览检查动作，拖动时间条定位到任意时间。预览循环与资产本身的“片段循环”分别控制。
3. 骨骼层级支持选择并检查当前局部平移、角度和缩放；缺少关联时，在“预览资源”选择已登记的匹配骨架及网格。旧资产仍支持片段旁的同名骨架作为回退。
4. 可以编辑片段名称、循环、事件时间、名称和参数，并在当前播放时间添加事件。事件时间必须在片段时长内；压缩曲线操作也会进入文档撤销历史。
5. `Ctrl+S` 保存动画，`Ctrl+Z` / `Ctrl+Shift+Z` 撤销和重做。未保存修改会显示星号，打开另一片段或重命名受影响的资源前需保存或恢复。关闭片段及退出编辑器时提供保存、放弃、取消。
6. 在实体的 Animation 组件中绑定 Clip 和匹配 Skeleton；需要 Root Motion 时再开启对应选项。

导入时会合并被省略辅助节点的静态及动画变换，并将逆绑定矩阵换算到导入单位。当前 GPU 骨架上限为 64 根骨骼；同一模型的蒙皮网格使用共享绑定空间。多个网格对同名骨骼提供不兼容的逆绑定矩阵时，导入会明确失败，应先在 DCC 中统一绑定空间后重新导入。

1D Blend Tree 用参数阈值在相邻 Clip 之间插值。阈值必须唯一并按数值排序，Clip 路径必须有效。Animation Graph 的使用顺序是：添加参数 → 添加状态并绑定 Motion → 设置 Entry State → 添加 Transition、比较运算、阈值和交叉淡化时间 → 保存 → 把 `.animgraph` 填入 Animation 组件。

新建 Blend Tree/Animation Graph 时会优先使用项目内已有 `.skanim`；没有可用 Clip 时会生成 `NewAnimationClip.skanim` starter clip。新加的 Sample/State 默认不伪造路径，保存前应明确绑定所需 Motion；保存时 Registry 会记录结构化依赖。

### 4.2 VFX

创建 `.vfx` 后可以设置最大粒子数、发射率、寿命、初速度、扩散、重力、颜色、尺寸、Flipbook 行列/FPS/循环和 CPU/GPU 模式。CPU 模式便于调试和需要 CPU 粒子状态的效果；GPU 模式适合大量连续弹道 Billboard，但当前不提供 Burst、碰撞、Ribbon 或 GPU 事件回读。

使用 Flipbook 时，纹理帧按规则网格排列。行列必须大于零，FPS 应与素材帧率一致；透明特效材质需要确认混合模式和排序结果。

### 4.3 Behavior Tree

`.btgraph` 保存节点、连线和编辑器位置。运行时支持 Sequence、Selector、Condition 和 Action；编辑器的新建模板提供 Sequence 根与 Action，并允许编辑节点 Type/Label、添加 Action、调整位置和保存。运行时任务依赖 Blackboard Key 时，必须确保 Key 名称和类型与行为代码一致。可视化编辑器负责图资产，不等同于完整的调试器；复杂 AI 仍应结合日志、Blackboard 快照和 EQS 结果检查。

### 4.4 材质、材质实例和贴图通道

材质统一保存为 `.mat`。普通材质的 `TINY_MAT 3 graph` 内容包括参数、节点连线和渲染设置；实例的 `TINY_MAT 3 instance` 内容通过 `parent` 引用父材质，只保存参数覆盖。双击普通材质直接查看和编辑节点连线；实例可通过“打开父材质”查看共享逻辑。取消“覆盖”即可恢复继承值。

新建材质包含 Base Color、Metallic、Roughness、Emissive、Opacity、法线和贴图通道组成的完整 PBR 图。颜色纹理使用 sRGB，Normal、Metallic、Roughness、AO 和 Packed 数据保持线性。修改父图会影响引用它的实例；需要独立逻辑时创建单独的普通材质。

图必须存在有效的表面输出。编译器会检测环路、列出相关节点 ID 并拒绝保存无效候选；看到环路错误时应删除回边后重新保存。

旧的 `.matgraph`、无版本 `.mat` 和版本 2 材质不再加载。已有项目可先运行 `python scripts/migrate_materials.py <项目目录>` 检查，再增加 `--apply` 完成离线迁移；原文件备份到 `saved/MaterialMigration/`。迁移后重新烘焙和打包。

### 4.5 Shader 源码

选择 `.vert` 或 `.frag` 打开源码编辑器。输入不会立即覆盖文件；标题的未保存标记表示内存文本与最近打开/保存内容不同。点击其他类型资产不会关闭当前 Shader 文档。

编辑后点击“保存并重载”，或在 Shader 窗口中按 `Ctrl+S`（包括文本输入期间）：

- 先原子保存文件并生成 `.bak`，再编译和重载；写入失败保留文本，不继续关闭/切换/退出。
- 关闭窗口、切换另一个 Shader、退出编辑器时，未保存修改提供“保存 / 放弃修改 / 取消”。放弃不写盘；取消保留原文档；关闭后重新选择该 Shader 会读取磁盘最新内容。
- 外部程序修改、删除或移动文件后，旧缓冲区不会直接覆盖它。请先保留所需文本，或放弃修改并重新打开文件。
- 有语法错误的源码仍可显式保存；“已保存”不代表编译通过，查看下方编译诊断。

- 编译成功时新 Program 生效并清除旧错误。
- 编译失败时显示驱动返回的行号和信息，同时继续使用上一份有效 Program。
- `#include` 路径应保持在项目 Shader 目录内；递归 include 会被拒绝。
- `#pragma variant` 会展开宏组合，组合数量应受控，避免 Cook 和启动时间膨胀。

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
.\build\Debug\tinyengine_asset_tool.exe thumbnail assets/player.mesh saved/player.ppm
```

大型文件分块：

```powershell
.\build\Debug\tinyengine_asset_tool.exe chunk Movie.bin saved/Movie.chunkmanifest 4194304
```

移动或重命名资产应从编辑器资产操作入口完成，使 `.meta`、依赖和文本引用同步更新。不要只在文件管理器中移动单个资产。

`.meta` 保存稳定 GUID、Registry 类型、源文件、导入设置和依赖。正常重导入和移动时应与主资产一起保留。Registry 使用 `asset.schemaVersion=2`；扫描时会用正式扩展名修正旧 Meta 中错误的 Binary 类型，并解析 Blend Tree/Animation Graph 的路径或 GUID 依赖。迁移后仍应执行 `missing`/`ValidateAssets` 验证引用。

缺失依赖的处理流程：

1. 运行 `tinyengine_asset_tool missing assets` 获取所有缺失引用。
2. 在编辑器选择问题资产，查看候选项及相似路径。
3. 使用 `Repair -> <路径>` 更新引用，不要直接批量字符串替换二进制资产。
4. 再次执行 `missing` 和 `ValidateAssets`，并检查 `git diff`。

大文件 `chunk` 只生成分块清单和块文件，不会自动改变场景引用。使用方必须通过流式加载接口读取 Manifest；移动 Manifest 时也要移动同目录块文件。

## 6. 环境光照

```powershell
.\build\Debug\tinyengine_asset_tool.exe bake-env `
  assets/Environment.hdr assets/EnvironmentBaked
```

产物包含原始全景图引用、六面 Irradiance Atlas、六级 GGX Prefilter Atlas、BRDF LUT 和 `.env` 清单。把 `.env` 路径写入项目的 `render.environment`；运行时会使用原始全景图绘制天空背景，并使用其烘焙结果提供 IBL。请保留 `.env` 所引用的源 HDR 文件，并让它位于项目资产目录内以便 Cook/打包。

## 7. 运行

编辑器内最常用的是顶部 `运行/停止`。Play 会使用内存场景快照，Stop 后恢复编辑态；需要保留的改动必须在进入 Play 前保存。

开发 Demo：

```powershell
.\build\Debug\tinyengine_demo.exe
```

参数 `3` 表示只运行三帧，适合自动化冒烟：

```powershell
.\build\Debug\tinyengine_demo.exe 3
```

编辑器和 Game 都支持有限帧冒烟参数：

```powershell
.\build\Release\tinyengine_editor.exe Aster-Engine.tinyproject --frames=3
.\build\Release\tinyengine_game.exe Aster-Engine.tinyproject --frames=3
```

第二条命令可直接用于源码项目冒烟；资源会按项目资产根解析。若从其他工作目录启动，应传入 `.tinyproject` 的绝对路径。正式发布仍需按下一节生成包并运行包内程序。

程序日志写入标准输出和项目根目录的 `Logs/TinyEngine.jsonl`；未捕获异常报告默认写入进程工作目录下的 `Crashes/`。自动化收集日志时应同时重定向 stdout 和 stderr。Renderer 的 Draw Call 调试日志只在首帧及每 300 帧输出，持续统计请使用 Console / Stats 面板。

## 8. 校验、Cook 和打包

提交或打包前建议依次执行：

```powershell
.\build\Debug\tinyengine_commandlet.exe ValidateAssets assets
.\build\Debug\tinyengine_commandlet.exe ValidateScenes assets
.\build\Debug\tinyengine_commandlet.exe CookProject `
  Aster-Engine.tinyproject saved/PlatformCook Windows Shipping
```

`ValidateAssets` 检查 Meta 和依赖；`ValidateScenes` 无图形加载全部场景。`MigrateScenes` 会创建 `.bak` 后重写当前格式，只应在已提交 Git 的场景副本上执行。`CookProject` 生成目标平台清单、转换后的资产、Shader Cache 和 `Game.tpak`，但不生成最终发布目录。

一键生成示例游戏包：

```powershell
cmake --build build --config Release --target package_game
```

或显式调用打包器：

```powershell
.\build\Release\tinyengine_package_tool.exe `
  Aster-Engine.tinyproject `
  saved/Package/Aster-Engine `
  .\build\Release\tinyengine_game.exe
```

发布目录包含重命名后的游戏可执行文件、配套 `.buildinfo`、Shipping `.tinyproject`、`Content/Game.tpak`、`Package.manifest` 和运行提示。打包器拒绝绝对资产路径、`..` 路径和危险输出目录，验证启动场景确实进入 TPAK，并把通过校验的构建兼容字段写入 Manifest。

建议的可重复发布流程：

```powershell
cmake --build build --config Release --target `
  tinyengine_game tinyengine_package_tool tinyengine_editor -j 8
ctest --test-dir build -C Release --output-on-failure
.\build\Release\tinyengine_commandlet.exe ValidateAssets assets
.\build\Release\tinyengine_commandlet.exe ValidateScenes assets
.\build\Release\tinyengine_package_tool.exe `
  Aster-Engine.tinyproject `
  saved\Package\Aster-Engine `
  .\build\Release\tinyengine_game.exe
.\saved\Package\Aster-Engine\Aster-Engine.exe `
  .\saved\Package\Aster-Engine\Aster-Engine.tinyproject --frames=65
```

验收包时检查：程序退出码为 0；日志出现非零 Draw Call；`Package.manifest` 的项目名、启动场景、TPAK 路径和构建兼容字段正确；发布目录不包含 FBX/glTF/OBJ、`.meta` 或编辑器缓存。Shader 源文件仍是当前 Runtime 资产的一部分，不应手工删除。缺少 `.buildinfo` 或版本、ABI、场景格式、构建 ID、配置、编译器任一不匹配时，Packager 会拒绝生成包；此时应重建同一配置的 Game 和打包工具。

## 9. 输入、音频和游戏逻辑

### 9.1 输入映射

项目通过 `input.bindings` 加载 `.input`。逻辑 Axis 可以绑定多个来源并设置比例，Action 表示按下状态和单帧触发。示例：

```text
TINYINPUT 1
axis "MoveX" "MoveX" 1
axis "LookX" "MouseX" 1
action "Jump" "Jump"
action "Sprint" "Sprint"
```

运行时代码通过 `InputSystem::GetAxis()`、`IsDown()` 和 `WasPressed()` 查询逻辑名称，而不是直接依赖某个键位。`BindAxis()`/`BindAction()` 可执行运行时重绑定，`SaveBindings()` 保存用户配置。`GetGlyph()` 可根据 Keyboard/Mouse、Xbox、PlayStation 或 Nintendo 样式显示提示图标；`Rumble()` 需要真实手柄和平台后端支持。

`Pawn` 的 Controller Index 指定玩家槽位，`Character Controller` 消费移动、冲刺和跳跃参数。新项目应先复制并修改 `assets/default.input`，再在项目文件中设置路径。

### 9.2 音频

把 WAV/OGG 路径填入 Audio Source：

- 短音效使用非循环 Voice，通常放在 `SFX` 或 `UI` Bus。
- 音乐和长 OGG 使用流式播放，通常放在 `Music` Bus。
- 3D 声音开启 Spatial，并设置 Minimum/Maximum Distance。
- `Master`、`SFX`、`Music`、`UI` Bus 可独立设置音量。

程序接口提供 `PlayOneShot()`、`PlayStream()`、`PlaySpatial()`、`SetBusVolume()` 和 `StopAll()`。仓库已有 `tests/fixtures/editor_ui/assets/audio/long_stream.ogg`，扩展回归覆盖有效 OGG 长流解码；发布前仍应验证目标平台上的实际播放。

### 9.3 原生 Gameplay 和脚本

Aster-Engine 目前没有 Lua/C#。Gameplay 通过 `GameInstance`、注册到 `ScriptEngine` 的 C++ `ScriptInstance`，或稳定 C 插件 ABI 扩展。`GameInstance` 生命周期是 `onInitialize` → `onSceneLoaded`/`onUpdate` → `onShutdown`；实体 Script 组件中的 Class 必须匹配已注册工厂名。

最小注册方式：

```cpp
class SpinScript final : public tinyengine::ScriptInstance
{
  public:
    void onUpdate(float deltaTime) override
    {
        auto *world = getWorld();
        if (!world)
            return;
        if (auto *transform = world->GetComponent<tinyengine::TransformComponent>(getEntity()))
            transform->rotation.y += 45.0f * deltaTime;
    }
};

void registerGameScripts(const tinyengine::EngineContext &context)
{
    context.scriptEngine->registerFactory(
        "SpinScript", [] { return std::make_unique<SpinScript>(); });
}
```

可在自定义 `GameInstance::onInitialize()` 中取得预先保存的 Engine 引用，再把 `Engine::GetContext()` 传给该函数。注册必须在进入 Play、创建对应实体脚本实例之前完成。需要跨 DLL 的扩展不要直接传递 C++ STL/异常，应使用[原生插件 API](../reference/native_plugin_api.md)定义的 C ABI。

### 9.4 Runtime UI 与文字

Runtime UI 与编辑器 ImGui 是两套系统。Gameplay 通过 `EngineContext::uiSystem` 创建 Panel、Label、Button、Image、InputField、List、Dropdown 和 ProgressBar，再设置父子关系、Horizontal/Vertical 布局、Anchor、滚动、DPI 和回调。每个 Widget ID 在当前 UI 树中应唯一。

```cpp
auto &button = context.uiSystem->AddButton(
    "start", "开始游戏", {40, 40, 180, 44}, [] { /* 切换状态 */ });
context.uiSystem->setAnchor(button.id, tinyengine::UIAnchor::Center);
```

每帧把窗口逻辑尺寸传给 `SetViewportSize()`，DPI 变化时调用 `SetDpiScale()`；鼠标/触摸状态传给 `Update()`，键盘或手柄导航使用 `FocusNext()` 与 `ActivateFocused()`。UTF-8 文本会经过动态 Glyph Atlas、BiDi、连字、断行和富文本管线，但当前不是完整 HarfBuzz/OpenType 实现，复杂文字脚本必须用目标语言样本验收。

### 9.5 导航、AI 与联网

简单关卡可使用 `NavigationGrid` 配置原点、网格尺寸、可行走单元和代价，再调用 `FindPath()`。任意三角地形可用 `PolygonNavMesh::Build()` 生成多边形邻接，动态障碍修改后调用 `RebuildDynamicObstacles()`；Crowd Agent 在设置目标后每帧 `Update()`。这些接口是 Recast 风格轻量实现，不包含商业级 Recast/Detour 烘焙工具链。

行为树通过 Blackboard 交换状态，EQS 使用采样点和加权测试返回候选位置。网络功能包括 UDP、可靠通道、RPC、Ownership、预测/纠错和 Replay，但会话发现、NAT 服务、加密、账号和反作弊属于产品层；接入前先阅读[联网指南](networking_guide.md)的包长、白名单和安全边界。

## 10. 物理内容制作检查

推荐组合：

- 地面和建筑：Static Rigid Body + Box/Convex Hull/Triangle Mesh Collider。
- 可推动物体：Dynamic Rigid Body + Box/Sphere/Capsule/Convex Hull。
- 角色：Capsule Collider + Character Controller；不要用动态 Triangle Mesh 作为角色。
- 移动平台：Kinematic Rigid Body + 简单 Collider。
- 高速小物体：开启 Continuous，但仍需用真实速度和墙厚做专项测试。

Collider Layer 是自身所属位，Mask 是允许碰撞的位集合。没有接触时先检查双方 `A.mask & B.layer` 和 `B.mask & A.layer` 是否均非零。质量只对 Dynamic 有意义；Static 物体不应依赖速度；Kinematic 由程序设置运动。

TE-QA-014/015 的薄墙穿透和三角网格斜坡特定缺陷已修复，自动回归覆盖高速刚体不穿薄墙及低摩擦球体沿斜坡滑动。复杂接触、高速弹丸、台阶或其他形状组合仍需实际玩法测试，不能把两个用例通过等同于全面物理认证。

## 11. 测试和发布前检查

日常提交前：

```powershell
cmake --build build --config Release -j 8
ctest --test-dir build -C Release --output-on-failure
.\build\Release\tinyengine_commandlet.exe ValidateAssets assets
.\build\Release\tinyengine_commandlet.exe ValidateScenes assets
git diff --check
git status --short
```

CTest 当前包含核心、真实 FBX/glTF 兼容和扩展专项三组。扩展报告中的含义：

- PASS：断言实际通过。
- FAIL：非预期失败，必须处理后再发布。
- XFAIL：稳定复现已编号故障，不等同于功能通过。
- XPASS：已知故障基线意外通过，应确认是否已经修复并更新测试。
- BLOCKED：缺少资源、硬件或实验环境，不能计为通过。

发布候选还应运行包内 Game 至少 65 帧，检查启动场景、Draw Call、输入、声音、Shader 编译、窗口关闭和退出码。需要测试编辑器鼠标流程时使用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tests/editor_ui_advanced_regression.ps1 -Scenario GizmoMarquee
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tests/editor_ui_advanced_regression.ps1 -Scenario ShaderMaterial
```

该旧鼠标脚本仍有固定坐标和 DPI 假设问题（8.25 报告 BUG-2），运行前必须重新标定，不能直接把结果当作当前产品验收。它会占用前台窗口和鼠标；Win32 合成按键还需物理键盘复核。新增 `tinyengine_editor_interaction` 直接注入 ImGui 输入，可验证交互逻辑，但不替代真实 DPI/鼠标矩阵。

`tinyengine_script_lifecycle` 验证脚本回调期间的自毁、增删和重载；`tinyengine_scene_save` 包含 Velocity 场景、快照及预制体往返；`tinyengine_platform_mount` 验证发布模式下的文本、二进制、空文件及资源管理器读取隔离。这些项目随 CTest 执行。

发布程序还应从包目录和含同名场景的外部目录分别启动。以下集成检查会生成隔离测试包并实际启动 Game，需要 SDL/OpenGL 环境，因此不加入默认无窗口 CTest：

```powershell
cmake "-DGAME=$PWD/build/Release/tinyengine_game.exe" `
  "-DPACKAGE_TOOL=$PWD/build/Release/tinyengine_package_tool.exe" `
  "-DTEST_ROOT=$PWD/saved/PackageCwdRegression" `
  -P tests/package_working_directory_tests.cmake
```

## 12. 输出目录、缓存与 Git

| 路径 | 内容 | 建议 |
|---|---|---|
| `build/`、`build-runtime/` | CMake/MSBuild 中间文件和 EXE | 不提交，可随时重新生成 |
| `assets/` | 正式 Authoring/Runtime 资产和 `.meta` | 提交；移动资产时同时提交引用更新 |
| `saved/Cooked/`、`saved/PlatformCook/` | Cooked 资产、Shader Cache、Manifest、TPAK | 通常不提交；发布系统归档 |
| `saved/EditorAutosave.scene` | 编辑器自动恢复场景 | 不作为正式场景提交 |
| `saved/EditorSettings.ini` | 当前项目的语言、界面缩放、可见面板和视口偏好 | 通常不提交 |
| `saved/EditorLayout.ini`、`EditorLayout.saved.*` | 自动布局和手动保存的布局快照 | 通常不提交 |
| `saved/ExtendedRegression/`、`saved/ImportCompatibility/` | 自动测试生成报告 | 按项目质量流程选择归档，不与手写正式报告混淆 |
| `saved/Package/`、`Dist/` | 发布目录 | 不提交二进制；由 CI 或 Release Artifact 保存 |
| `Logs/TinyEngine.jsonl`、`Crashes/` | 结构化日志、MiniDump 和文本崩溃信息 | 不提交敏感日志；定位后清理 |

推荐一次提交只包含一个明确内容变化及其 `.meta`、引用和文档。提交前确认没有把 `saved/` 中的隔离测试工程、绝对源资产路径或大型发布包意外加入 Git。

## 13. 常见问题

| 现象 | 优先检查 | 处理方式 |
|---|---|---|
| 编辑器无法启动或 OpenGL 初始化失败 | 驱动是否支持 OpenGL 4.3 Core | 更新驱动；远程桌面/虚拟机可能只暴露软件 OpenGL |
| 中文显示方框 | 系统 CJK 字体和启动日志 | Windows 安装微软雅黑/黑体/宋体；Linux 安装 Noto Sans CJK 或文泉驿微米黑 |
| Assets 没有导入按钮 | 是否以 `TINYENGINE_BUILD_IMPORTERS=ON` 配置并提供 Assimp | 重新配置并构建 Editor/Importer |
| 模型材质或 UV 错误 | 导入比例、Flip UV、贴图色彩空间 | 用隔离目录重新导入并与 DCC 对照，不要覆盖唯一资产后再排查 |
| 新动画图运行时报缺 Clip | Sample/State 是否已绑定有效 `.skanim` | 绑定项目内 Clip；无现成 Clip 时使用自动生成的 starter clip，并执行 `ValidateAssets` |
| Assets 将 VFX/行为树/动画显示为 Binary | 是否完成 schema v2 扫描迁移 | 重新扫描 Assets；Registry 会按正式扩展名修正旧 Binary，再校验依赖 |
| 找不到曝光或后处理选项 | 是否打开编辑器设置 | 点击 `编辑 > 设置 > 渲染设置`；视口显示模式与辅助显示分别在 `光照`、`显示` 菜单 |
| 直接运行源码 Game 有窗口但没有物体 | 项目参数是否指向正确 `.tinyproject` | 从其他工作目录启动时传绝对项目路径，再执行 `ValidateAssets`/`ValidateScenes` |
| 打包器拒绝 Game 或提示旧版本 | EXE 旁的 `.buildinfo` 与当前工具是否匹配 | 重建同一配置的 Game 和 Package Tool，不要复制或伪造 sidecar |
| 恢复自动保存后内容不对 | `saved/EditorAutosave.scene` 时间和当前项目 | 先备份当前场景；恢复后另存为副本并比较，不要直接覆盖正式场景 |
| 日志快速变大 | 是否还有其他模块逐帧输出 | Draw Call 已节流为首帧及每 300 帧；按日志分类继续定位高频来源 |
| 测试显示 BLOCKED | 报告中的平台/硬件条件 | 补 Linux/macOS、物理手柄、多厂商 GPU、干净 VM 或外部网络实验环境后再测；OGG 长流已通过 |

当前故障、证据和安全边界见编辑器质量状态与故障清单（本地历史文档）；历史回归结果见综合回归测试报告（本地历史文档）；尚未实现的新能力见编辑器需求清单（本地历史文档）。

## 14. 当前适用范围

Aster-Engine 0.27 适合小型 3D 游戏、工具可视化、教学和引擎实验。它已经形成可运行闭环，并具备轻量 Render Graph、Forward+ 与 GPU Driven 渲染，但没有多 RHI、商业级 Recast 烘焙、加密联网服务、Full Body IK 或完整 OpenType/HarfBuzz 塑形。具体边界见后续工作（本地历史文档）。

对于正式项目，当前最重要的使用纪律是：所有内容进入 Git；通过 Save As 为 Untitled 场景命名；为动画状态明确绑定 Clip；保留 Game 的 `.buildinfo` 并让 Packager 执行兼容 Gate；源码 Game 与包内 Game 都做冒烟；把 FAIL、XFAIL 和受外部环境限制的 BLOCKED 分开记录。TE-QA-001～016 的完整修复思路见全量修复复盘（本地历史文档）。
# 材质父图与实例

父材质节点、参数覆盖、动态实例、Masked 示例和保存/打包规则见 [材质系统](../Architecture/材质系统.md)。示例位于 `assets/Materials`。
