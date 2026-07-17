# TinyEngine 编辑器 UI 冒烟测试与故障清单

- 测试日期：2026-07-17
- 测试版本：`build/Release/tinyengine_editor.exe`（2026-07-17 02:09:36）
- 测试环境：Windows，2560×1440 桌面，150% DPI；最大化窗口与默认 1280×720 逻辑窗口
- 隔离工程：`Saved/EditorQA/EditorQA.tinyproject`

> 2026-07-18 复测：使用提交 `2192a7a` 重新构建并回归。新增 TE-QA-011、012；TE-QA-006 已修复；其余状态和全程序/工具结果见 [全功能回归测试报告](FULL_REGRESSION_REPORT_2026-07-18.md)。

## 结论

编辑器的 Play/Stop、基础渲染开关、Shader Reload、多实体选择、四类专项资产编辑器、Assimp 后台模型导入和项目打包主路径均可运行；核心自动测试与真实 FBX/glTF 兼容测试也全部通过。

两轮累计确认 12 项问题，其中 TE-QA-006 已修复；当前开放 11 项：P0 1 项、P1 5 项、P2 4 项、P3 1 项。最先应修复的是“新建场景 → 保存”的潜在启动场景覆盖、源码工程 Game 不渲染和新场景未真正切换。P0/P1 修复前，不建议将当前编辑器用于不可恢复的正式场景制作或对外发布打包。

严重级别约定：P0 表示数据丢失或发布阻断；P1 表示核心工作流错误；P2 表示有绕行方式但明显影响使用或维护；P3 表示低风险易用性问题。

## 已确认故障

### TE-QA-001 · P0 · 保存 Untitled 场景会静默覆盖启动场景

- 复现：启动隔离工程，选择 `File → New Scene`，编辑当前内容，再选择 `File → Save Scene`。
- 实际：没有 Save As 对话框、路径提示或覆盖确认，`assets/Demo.scene` 的 SHA-256 在操作前后发生变化。
- 预期：首次保存无路径场景时必须要求用户选择新路径；不得回退并覆盖 startup scene。
- 根因：`SaveCurrentScene()` 在文档路径为空时依次回退到 `SceneManager::GetCurrentScene()` 和项目 startup scene，见 `editor/EditorApp.cpp:2328`。
- 2026-07-18 状态：危险回退代码仍存在；本轮由于 TE-QA-002 将活动标签切回旧场景，没有再次改写启动场景，不能据此判定已修复。
- 建议：实现 Save As；无路径文档禁止直接 Save；覆盖已有文件前确认；先原子写临时文件再替换。
- 证据：`Saved/EditorQA/qa-09-new-scene-correct.png`、`qa-10-package-correct.png`。

### TE-QA-002 · P1 · New Scene 创建后没有真正切换到新场景

- 复现：打开 `Demo.scene`，选择 `File → New Scene`。
- 实际：出现 `Untitled *` 标签，但原 `Demo.scene` 仍保持选中，Hierarchy 仍显示原场景实体；下一帧又切回原文档。
- 预期：Untitled 标签立即成为活动标签，Hierarchy/Scene View 显示空场景。
- 根因：`NewSceneDocument()` 修改了内部活动索引，但 `DrawSceneTabs()` 没有为新标签设置 `ImGuiTabItemFlags_SetSelected`；ImGui 保持旧标签可见后触发 `SwitchSceneDocument()`，见 `editor/EditorApp.cpp:2282`、`:2218`。
- 建议：增加 pending-selected document，下一帧对目标标签设置 `SetSelected`；切换完成后再清除标记，并增加多文档状态回归测试。
- 2026-07-18 状态：再次复现；`Untitled *` 标签出现时 Hierarchy 和 Scene View 仍显示 `Demo.scene` 内容。
- 证据：`Saved/EditorQA/qa-09-new-scene-correct.png`、`qa-38-new-scene-regression.png`。

### TE-QA-003 · P1 · 新建动画资产自带不存在的硬编码依赖

- 复现：点击 `+ Blend Tree` 或 `+ Animation Graph`。
- 实际：生成文件均引用 `assets/player.skanim`，但当前项目只有 `assets/player.anim`；Registry 同时显示 `Dependencies: 0`，缺失依赖检测没有报警。
- 预期：新资产应为空、要求用户选择 Clip，或引用一个确定存在的项目模板；依赖应被 Registry 记录并校验。
- 根因：模板和新增节点均硬编码 `assets/player.skanim`，见 `editor/EditorApp.cpp:1329`、`:1343`、`:1745`、`:1834`。
- 建议：移除项目相关硬编码；保存 Blend Tree/Animation Graph 时解析并写入 `.meta` 依赖；运行前验证 Clip 存在且类型匹配。
- 证据：`Saved/EditorQA/assets/NewBlendTree.blendtree`、`NewAnimationGraph.animgraph`。

### TE-QA-004 · P1 · 编辑器打包未校验 Game 可执行文件是否与当前引擎匹配

- 复现：在当前 Release 编辑器中选择 `File → Package Project`，然后运行包内程序。
- 实际：打包成功，但直接复制了相邻的 `tinyengine_game.exe`。当前编辑器构建时间为 2026-07-17，包内 Game 为 2026-07-16；编辑器运行在 OpenGL 4.3 Compute + Indirect 路径，包内运行日志仍报告 OpenGL 3.3。
- 预期：打包前验证 Engine ABI、场景格式版本、构建配置和提交/版本标识一致；不一致时阻止发布或清晰告警。
- 根因：菜单逻辑直接把可执行目录下的 `tinyengine_game.exe` 交给 Packager，见 `editor/EditorApp.cpp:456`。
- 当前结果：包内程序仍能启动并完成 3 帧，故障表现是发布一致性风险，而不是本次测试中的启动崩溃。
- 2026-07-18 状态：全 Release 目标已重建，当前包内 EXE 与本轮构建一致并连续 65 帧正常绘制；但打包路径仍直接取相邻 Game EXE，没有版本/ABI 校验，因此风险仍开放。
- 证据：`Saved/EditorQA/Dist/TinyEngineEditorQA/Package.manifest`、`qa-package-run.log`。

### TE-QA-005 · P2 · 默认窗口下 Scene View 工具栏被截断且无法访问后半段

- 复现：以编辑器默认窗口尺寸启动，系统 DPI 设为 150%。
- 实际：工具栏只显示到 FXAA，Bloom、GTAO、TAA、Reload Shaders 等控件超出 Scene View 右边界；没有换行或水平滚动入口。
- 预期：工具栏响应式换行、分组折叠或提供可见的水平滚动。
- 证据：`Saved/EditorQA/qa-21-default-window.png`。

### TE-QA-006 · P2 · Assets 的类型列与路径列发生文字重叠（2026-07-18 已修复）

- 复现：默认窗口启动并创建 Animation Graph。
- 实际：`AnimationGraph` 等长类型名与后面的绝对路径重叠，形成类似 `AnimationGraD:/...` 的不可读文本。
- 预期：使用 ImGui Table 的可调整列，或根据类型文本宽度动态计算路径列起点。
- 原根因：路径固定列起点过窄；当前已调整为 `SameLine(110.0f)`，见 `editor/EditorApp.cpp:1511`。
- 2026-07-18 状态：最大化窗口的英文 `AnimationGraph` 与路径已分离，中英文均未再复现重叠。窄窗口仍应长期改为 ImGui Table，但不再计入开放故障。
- 证据：`Saved/EditorQA/qa-21-default-window.png`。

### TE-QA-007 · P2 · Console / Stats 的 Render Graph 状态被截断

- 复现：最大化或默认窗口观察 Console / Stats 第三行。
- 实际：最大化时 `GPU Driven` 尾部仍被截断，默认窗口从 HZB 附近开始不可见；没有换行、Tooltip 或水平滚动。
- 预期：按字段分行/表格显示，窄窗口下自动换行，并保留完整 Tooltip。
- 位置：`editor/EditorApp.cpp:2174`。
- 2026-07-18 状态：最大化窗口中已能完整显示；默认窗口仍从右侧截断，因此保持开放。
- 证据：`Saved/EditorQA/qa-03-main-controls.png`、`qa-21-default-window.png`。

### TE-QA-008 · P2 · Renderer 每帧写 Debug 日志导致日志洪泛

- 复现：让编辑器空闲运行数秒并统计 stdout。
- 实际：每帧输出一行 `RenderFrame draw calls`。例如一次短测试的 1,955 行日志中有 1,952 行为该消息；多轮测试均有约 99% 的日志被它占据。
- 影响：持续磁盘 I/O、结构化日志膨胀、有效警告被淹没，长时间编辑会显著增加日志体积。
- 根因：`src/Render/Renderer.cpp:493` 无条件逐帧调用 `Debug()`。
- 建议：改成显式 CVar/诊断模式，或按秒节流；正常编辑器只保留统计面板内存数据。
- 证据：`Saved/EditorQA/qa-*.stdout.log`。

### TE-QA-009 · P2 · Registry 类型和依赖元数据不能覆盖新增资产类型及旧 Meta

- 复现：创建 VFX、Behavior Tree，并扫描已有 `player.anim`。
- 实际：`.vfx`、`.btgraph` 显示为 `Binary`；已有 `player.anim.meta` 也继续把动画显示为 `Binary`。专项编辑器能按扩展名打开这些文件，但 Registry 类型、筛选和依赖诊断与实际能力不一致。
- 根因：`AssetType()` 未覆盖 `.vfx`/`.btgraph`；扫描时旧 `.meta` 的 `asset.type` 无条件覆盖新推断类型，见 `src/Resource/AssetRegistry.cpp:26`、`:89`。
- 建议：补齐所有正式扩展名；增加 Meta schema/version 和迁移；对未知/过期类型重新推断；为 VFX、行为树和动画图解析依赖。
- 证据：`Saved/EditorQA/qa-19-vfx-editor.png`、`qa-20-behavior-tree-editor.png` 及对应 `.meta`。

### TE-QA-010 · P3 · 场景文档标签没有关闭入口

- 复现：连续执行 `New Scene` 或打开多个场景。
- 实际：标签只会累积，没有关闭按钮，也没有未保存关闭确认流程。
- 预期：每个标签可关闭；脏文档关闭时提供 Save / Discard / Cancel。
- 根因：`BeginTabItem()` 的 `p_open` 参数传入 `nullptr`，见 `editor/EditorApp.cpp:2230`。

### TE-QA-011 · P1 · 源码工程直接运行 Game 不渲染场景

- 复现：执行 `build/Release/tinyengine_game.exe TinyEngine.tinyproject --frames=65`。
- 实际：退出码为 0，但 65/65 帧均为 0 Draw Call；打包版同样运行 65 帧则全部为 1 Draw Call。
- 根因：`src/GameMain.cpp:51` 把工作目录切到 EXE 目录，而 `ResourceManager::Load()` 没有使用已保存的项目资产根目录解析场景中的 `assets/...` 相对引用。
- 建议：统一资源 URI 解析，禁止把进程 CWD 当作项目资产根；增加任意 CWD 启动集成测试。
- 证据：`Saved/Regression_20260718/direct-game-65.log`、`packaged-game-65.log`。

### TE-QA-012 · P1 · 设置窗口被固定布局面板覆盖

- 复现：打开 `Edit → Settings` 并等待下一帧。
- 实际：窗口被 Hierarchy、Scene View 等后绘制面板盖住，内容只在后方隐约可见，正常鼠标操作受阻。
- 根因：`DrawSettings()` 在全部固定布局窗口之前调用，见 `editor/EditorApp.cpp:388`–`:404`。
- 建议：最后绘制设置窗口，或改成 Modal/Dockable Window；增加多帧截图回归。
- 证据：`Saved/EditorQA/qa-32-language-dialog.png`、`qa-34-language-chinese.png`。

## 已通过项目

- Play / Stop：运行态进入成功，Stop 后编辑场景恢复。
- Scene View：Translate / Rotate / Scale、Snap、Local / World、Pivot 模式和 FXAA / Bloom / GTAO / TAA 开关可点击，Shader Reload 有成功消息且无编译错误。
- Hierarchy / Inspector：实体创建、实体属性展示、Ctrl 多选可用，多选时显示“editing primary”。
- Autosave：Restore Autosave 成功并显示确认消息。
- 资产编辑器：Blend Tree、Animation Graph、VFX、Behavior Tree 创建文件并打开对应编辑器。
- Assimp 导入：真实 `box.obj` 后台导入成功，生成 1 个 Prefab、1 个 Mesh 和 2 个 Material。
- Package Project：生成 Manifest、TPAK、项目文件和 EXE；包内 EXE 可启动并正常退出。
- 自动测试：`tinyengine_tests.exe` 通过；`ctest --test-dir build -C Release --output-on-failure` 为 2/2 通过，包括真实 FBX/glTF 兼容矩阵。
- 语言设置：中文/英文可即时切换并持久化，重启后正确恢复；设置窗口层级问题另见 TE-QA-012。

## 尚需人工复核

Win32 合成按键对 SDL/ImGui 的特殊键传递不稳定，本轮不能把以下现象判定为产品故障：Delete 删除、Ctrl+Z/Ctrl+Y、Console 输入后按 Enter。鼠标菜单路径与物理键盘应再做一轮人工复核。资产 Move/Rename、依赖交互修复、Shader 源码错误定位和各专项编辑器内部每一个增删/保存按钮也需要后续专项用例，不属于本轮高频冒烟覆盖。

## 修复顺序建议

1. 先合并修复 TE-QA-001 与 TE-QA-002，并补“新建—切换—保存—关闭—恢复”的多文档自动测试。
2. 修复 TE-QA-011，使源码工程和打包工程使用一致的资源路径语义。
3. 修复 TE-QA-012、003 与 009，使设置和资产创建、类型识别、依赖诊断形成闭环。
4. 为 Packager 增加版本/ABI/场景格式 Gate，解决 TE-QA-004。
5. 最后处理响应式布局和日志节流，并建立 100%/150%/200% DPI、1280×720 到 4K 的截图基线。

本地截图、日志、隔离资产与打包产物统一保存在 `Saved/EditorQA/`，不进入正式项目资产目录。
