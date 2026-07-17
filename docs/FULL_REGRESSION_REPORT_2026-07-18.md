# TinyEngine 全功能回归测试报告

- 测试日期：2026-07-18
- 测试提交：`2192a7a`
- 测试配置：Windows Release，OpenGL 4.3 Compute + Indirect
- 测试工程：`TinyEngine.tinyproject`、`Saved/EditorQA/EditorQA.tinyproject`
- 测试产物：`Saved/Regression_20260718/`、`Saved/EditorQA/`

## 结论

本轮重新构建并测试了仓库当前提供的全部 9 个 Release 可执行程序，覆盖核心自动化、真实 FBX/glTF 导入、Demo、Editor、Game、Commandlet、资产工具、场景工具和项目打包工具。构建成功，CTest 2/2 通过，资产/场景/Commandlet 的全部公开命令均成功执行，打包产物可运行。

编辑器主要交互路径完成截图回归。中英文切换及持久化、Play/Stop、渲染开关、Shader Reload、多选、Inspector、Undo/Redo、四类专项编辑器打开和 Assimp 后台导入均可用。

本轮新增 2 个 P1 故障：源码工程直接运行 Game 时场景 65/65 帧均为 0 Draw Call；设置窗口在打开后被固定布局面板覆盖。历史故障中，TE-QA-002、003、005、007、008、009、010 仍可复现或由当前代码确认；TE-QA-001 的危险保存路径仍存在，但本轮被 TE-QA-002 遮挡而没有再次改写文件；TE-QA-004 本轮因所有二进制已重建而未出现版本错配，但打包器仍没有版本/ABI Gate；TE-QA-006 已修复。

当前开放故障合计 11 项：P0 1 项、P1 5 项、P2 4 项、P3 1 项。详细复现步骤见 [编辑器 UI 故障清单](EDITOR_QA_REPORT_2026-07-17.md)。

“全功能回归”指全部公开可执行入口、已有自动测试覆盖和可重复 UI 主路径；不表示已穷举所有硬件、资产格式变体、网络异常、长时间稳定性及每个参数组合。

## 构建与自动化测试

| 项目 | 命令/范围 | 结果 |
| --- | --- | --- |
| 全目标构建 | `cmake --build build --config Release -j 8` | 通过，9 个 EXE 均存在 |
| CTest | `ctest --test-dir build -C Release --output-on-failure` | 2/2 通过 |
| 核心测试 | `tinyengine_tests.exe` | `All TinyEngine tests passed` |
| 真实导入矩阵 | `tinyengine_import_compatibility_tests.exe` | 4/4 通过 |

真实导入矩阵实际覆盖：FBX 骨骼动画、FBX 复杂多 Mesh、glTF 2.0 Skin、glTF 2.0 复杂 PBR。核心测试覆盖数学/序列化/ECS、场景与 Prefab、资源编译/加载/TPAK、Render Graph、Clustered Lighting、现代渲染帧图、材质与 Shader、物理、动画、VFX、音频、输入、Runtime UI、插件、导航 AI 和网络编解码等已有断言。

## 可执行入口与工具回归

| 程序 | 实测内容 | 结果 |
| --- | --- | --- |
| `tinyengine_demo.exe` | 65 帧 OpenGL 冒烟 | 通过，Draw Call 为 1–4 |
| `tinyengine_editor.exe` | 有界启动及 UI 回归 | 通过，场景正常绘制 |
| `tinyengine_game.exe` | 源码 `.tinyproject` 直接运行 65 帧 | **失败，65/65 帧为 0 Draw Call，TE-QA-011** |
| 打包后的 Game | 打包工程运行 65 帧 | 通过，65/65 帧为 1 Draw Call |
| `tinyengine_commandlet.exe` | `ValidateAssets`、`ValidateScenes`、`MigrateScenes`、`CookProject` | 全部通过 |
| `tinyengine_asset_tool.exe` | `import`、`bake-env`、`scan`、`cook`、`pack`、`list`、`thumbnail`、`chunk`、`missing` | 全部通过 |
| `tinyengine_scene_tool.exe` | `list`、`add`、`transform`、`remove` | 全部通过，使用隔离场景 |
| `tinyengine_package_tool.exe` | 项目、EXE、Manifest、TPAK 分发 | 通过，生成 10 个暂存文件 |

Commandlet 的场景迁移在隔离副本上生成 `.bak` 后再次通过校验；CookProject 生成 Windows Shipping 平台清单、Cooked 资产、Shader Cache 和 `Game.tpak`。Asset Tool 的真实 OBJ 导入生成 1 个 Prefab、1 个 Mesh 和 2 个 Material；环境烘焙生成 Environment、Irradiance、Prefilter 与 BRDF LUT 文件。

## 编辑器交互回归

| 功能 | 结果 | 说明 |
| --- | --- | --- |
| 中文/英文切换 | 通过 | 即时切换，`language=zh-CN/en` 可持久化并在重启恢复 |
| 设置窗口 | **失败** | 下一帧被 Hierarchy/Scene View 等固定面板覆盖，TE-QA-012 |
| Play / Stop | 通过 | 运行态进入、绘制和返回编辑态正常 |
| Scene View 控件 | 通过 | W/E/R、Snap、Local/World、Pivot、FXAA/Bloom/GTAO/TAA、Shader Reload 可操作 |
| Hierarchy / Inspector | 通过 | 实体选择、Active、Ctrl 多选和主对象编辑提示正常 |
| Undo / Redo | 通过 | 通过 Edit 菜单实测 |
| 新建场景 | **失败** | 新标签出现但旧场景仍是活动内容，TE-QA-002 |
| Blend Tree / Animation Graph | 部分通过 | 编辑器可打开；新资产仍带缺失的 `assets/player.skanim`，TE-QA-003 |
| VFX / Behavior Tree | 部分通过 | 编辑器可打开；Registry 仍显示 Binary，TE-QA-009 |
| Assimp 模型导入 | 通过 | 真实 `box.obj` 后台导入完成 |
| 响应式布局 | 部分失败 | 默认窗口工具栏与统计文本仍截断，TE-QA-005/007 |
| 场景标签关闭 | 失败 | 没有关闭入口和未保存确认，TE-QA-010 |

Win32 合成键对 SDL/ImGui 的 Delete、Ctrl 快捷键和 Console Enter 传递不稳定。本轮 Undo/Redo 已通过鼠标菜单验证；Console 命令提交不能据此判为产品故障，仍需物理键盘人工复核。Shader 源码错误定位、Material Graph 每个节点操作、资产 Move/Rename 与依赖交互修复没有稳定的黑盒 UI 自动化入口，本轮由核心测试和代码路径覆盖，仍建议建立专门的可脚本化 UI 用例。

## 新增故障摘要

### TE-QA-011 · P1 · 源码工程直接运行 Game 不渲染场景

- 命令：`build/Release/tinyengine_game.exe TinyEngine.tinyproject --frames=65`。
- 实际：退出码为 0，但日志中 65/65 帧均为 `RenderFrame draw calls: 0`。
- 对照：同一引擎经 Package Tool 生成的 Game 运行 65 帧，65/65 帧均为 1 Draw Call。
- 根因：`src/GameMain.cpp:51` 在初始化前把工作目录切到 EXE 目录；场景中的 `assets/...` 引用随后由 `ResourceManager::Load()` 按工作目录直接加载。虽然 `ResourceManager` 保存了绝对 `m_assetRoot`，加载模板没有使用它解析相对路径，见 `include/MiniEngine/Resource/ResourceManager.hpp:30`。
- 建议：统一使用 ProjectSettings/AssetRegistry 解析资源 URI；缓存键和实际文件路径分离；增加“从任意 CWD 直接运行源码工程”的集成测试。
- 证据：`Saved/Regression_20260718/direct-game-65.log`、`packaged-game-65.log`。

### TE-QA-012 · P1 · 设置窗口被编辑器固定布局覆盖

- 复现：打开 `Edit → Settings`，等待下一帧。
- 实际：设置窗口内容出现在 Hierarchy/Scene View 后方，只剩隐约文字，无法正常阅读和点击。
- 根因：`EditorApp::Draw()` 在固定布局窗口之前调用 `DrawSettings()`，见 `editor/EditorApp.cpp:394`；后绘制且每帧固定位置的面板覆盖设置窗口。
- 建议：把设置窗口放到所有固定面板之后绘制，或实现为 Modal/Dockable Window，并增加打开后等待多帧的截图回归。
- 证据：`Saved/EditorQA/qa-32-language-dialog.png`、`qa-34-language-chinese.png`。

## 建议修复顺序

1. 修复 TE-QA-001/002，先消除场景误保存风险并完成真正的多文档切换。
2. 修复 TE-QA-011，保证源码工程、编辑器 Play 和打包运行使用同一套资源路径语义。
3. 修复 TE-QA-012 与 TE-QA-003/009，恢复设置和资产创建/诊断闭环。
4. 为 Packager 增加版本/ABI Gate，再处理默认窗口布局、统计文本和日志节流。
