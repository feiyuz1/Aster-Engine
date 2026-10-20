# 独立项目工具与引擎仓库目录整理

日期：2026-10-20。状态：用户已确认继续，按“新增工具先提交、再整理文件”的顺序实施。实际完成范围以提交和验收记录为准。

## 1. 用户目标与范围

新增可独立运行的 TinyEngineProjectTool.exe，像 UE 的项目工具一样，支持在 Windows 中右键 .tinyproject 生成 Visual Studio 工程。工具在 Programs 中有独立源码目录、CMake 目标和安装位置。

用户已明确：只整理引擎仓库，不重排新建游戏项目的目录，也不迁移用户游戏项目。现有 C++ SDK、DLL、Play 和游戏项目的 build/ 约定继续使用。

整理包括源码工具、脚本、文档，以及引擎仓库自己的构建与运行生成物。不是把整个引擎复制成 UE 的目录结构；src、include、editor、assets、tests 的职责保留。

## 2. 已核实的现状

- 独立程序源码散在 tools/，包括 AssetTool.cpp、SceneTool.cpp、PackageTool.cpp、Commandlet.cpp；同目录还有 ReleaseSmoke.ps1。
- SolutionGenerator 已能调用 CMake，识别 VS 自带 CMake，并由编辑器触发生成或构建。新 EXE 应复用这份实现。
- SDK 查找目前位于 ProjectScaffolder.cpp，与生成启动场景的 ECS 代码处于同一编译单元。新工具需要提取可复用的纯文件路径查找，避免为生成 SLN 加载完整引擎。
- 主工作区有 build、build-ci、build-full、build-noimport-editor；还有 Logs、Crashes、Saved、Dist、artifacts 和 imgui.ini。
- 多个 CMakeCache 保存绝对路径。SDK 工作树仍引用主工作区 build/_deps 中的 SDL2 和 ImGui；不能先搬走这个目录。
- 文档既有使用指南，也有设计、专题、历史报告和计划；部分主工作区的本地报告及旧测试文件没有被 Git 跟踪。整理时必须保留原跟踪状态，不能把本地文件一股脑提交。
- D:/project/tinyengine 当前 main 仍是之前的计划基线；SDK 实现位于现有 codex/project-sdk-p0 工作树。不能把两个目录的文件直接互相覆盖。

## 3. 用户怎样使用项目工具

1. SDK 内提供 Programs/ProjectTool/TinyEngineProjectTool.exe。普通用户不需要输入命令。
2. 首次双击工具，可注册当前用户的项目右键菜单；同一工具支持移除该菜单。
3. 此后右键 MyGame.tinyproject，选择“生成 Visual Studio 工程”。Windows 11 使用经典菜单入口时，路径为“显示更多选项 → 生成 Visual Studio 工程”。
4. 工具验证项目文件与同目录 CMakeLists.txt，从自身安装位置识别 SDK，调用共享的生成流程。
5. 沿用已有约定，把 SLN 及 VS 中间文件生成到游戏项目 build/。操作只生成工程，不编译、不启动编辑器、不执行项目 DLL。
6. 成功显示 SLN 位置；失败保留原文件并显示可定位错误。完整日志进入项目 Saved/ProjectTool/。

高级与自动化入口保留 --generate-project <文件>、--register、--unregister、--help，并提供非交互模式及明确退出码。

## 4. 工具边界

- Programs/ProjectTool 是独立 CMake 工程，也可被引擎根 CMake 作为子目录构建。
- ProjectTool 只依赖 C++17 标准库和 Windows 系统 API，不链接 tinyengine、SDL、OpenGL 或 ImGui，不要求先编译编辑器。
- 同一份 SolutionGenerator 和 SDK 路径查找实现由编辑器与工具复用，不另写一套 CMake 命令。
- 生成时显式指定所选 SDK 的 TinyEngine_DIR，防止旧 CMakeCache 把工程继续绑定到另一个仍然存在的 SDK。
- SDK 发行包使用 Release 版 ProjectTool；开发者可单独构建 Debug 版工具。工具的编译配置不决定生成出来的游戏工程使用 Debug 还是 Release。
- 其他现有程序移到 Programs 的各自目录，按需要链接引擎；不能把它们宣称成完全没有引擎依赖。
- 保持已有 CMake 目标名、CLI 参数和工具职责，更新源码路径、解决方案分组、安装规则及调用方。

## 5. Windows 右键菜单

采用当前用户的静态 Shell verb，命令完整引用工具 EXE 和项目参数。只管理 TinyEngine 自己命名的菜单键，保留已有的文件默认程序和其他软件菜单。

候选位置：HKCU/Software/Classes/SystemFileAssociations/.tinyproject/shell/TinyEngine.GenerateSolution。

注册后通知 Shell 刷新关联。重复注册可更新到当前 SDK；旧 SDK 的卸载操作必须检查所有者，不能移除新 SDK 已接管的菜单。失败不能留下半个可见菜单；若覆盖自己的旧注册失败，恢复旧命令。

没有管理员提权、PATH 修改、默认应用抢占、系统菜单整体修改或自动注册其他文件类型。普通构建和自动测试不修改真实用户菜单；测试使用独立临时注册表根，并清理自己创建的键。最终实际注册使用安装位置稳定的 EXE，避免指向临时 QA 目录。

选择理由：静态 verb 足以完成本需求。原生 Windows 11 首层菜单的打包/COM 扩展增加部署范围，放到后续。纯脚本方案不满足用户要求的独立 EXE。

参考：[Microsoft 文件类型注册](https://learn.microsoft.com/en-us/windows/win32/shell/fa-file-types)、[稳定的次级 verb 注册](https://learn.microsoft.com/en-us/windows/win32/shell/app-registration#registering-verbs-and-other-file-association-information)、[Microsoft PowerToys 对经典和现代菜单的说明](https://github.com/microsoft/PowerToys/blob/main/.github/skills/ui-tests-local-vm/references/shell-extensions-and-signing.md)。

## 6. 引擎仓库目标目录

```text
TinyEngine/
  README.md
  CMakeLists.txt
  CMakePresets.json
  TinyEngine.tinyproject
  include/                   引擎公开头文件
  src/                       引擎实现
  editor/                    编辑器实现
  assets/                    引擎示例与基础资源
  tests/                     可执行回归与夹具
  cmake/                     构建、SDK 和安装规则
  Programs/
    CMakeLists.txt
    ProjectTool/
    AssetTool/
    SceneTool/
    PackageTool/
    Commandlet/
  Scripts/
    Build/
    Tests/
    Maintenance/
  docs/
    README.md                文档索引
    Guides/
    Reference/
    Architecture/
    Reports/
    superpowers/             保留已审阅设计与实施记录
  Intermediate/              构建缓存、依赖缓存、文档生成物
  Saved/                     本地运行、测试与历史归档
  artifacts/                 可交付 SDK 和压缩包
```

隐藏的 Git、IDE、代理配置保留其职责，不为外观整齐擅自迁移用户配置。

### 受版本控制文件迁移

| 原位置 | 目标位置 |
|---|---|
| tools/AssetTool.cpp | Programs/AssetTool/AssetTool.cpp |
| tools/SceneTool.cpp | Programs/SceneTool/SceneTool.cpp |
| tools/PackageTool.cpp | Programs/PackageTool/PackageTool.cpp |
| tools/Commandlet.cpp | Programs/Commandlet/Commandlet.cpp |
| tools/ReleaseSmoke.ps1 | Scripts/Build/ReleaseSmoke.ps1 |
| 文档中心.md | docs/README.md（原文件在 docs 中） |
| 构建指南、使用指南、项目工作流、项目代码开发、联网指南 | docs/Guides/ |
| 工具参考、原生插件 API、资产与数据格式、第三方依赖与许可 | docs/Reference/ |
| 设计、项目架构、源码阅读与注释规范、材质系统 | docs/Architecture/ |
| 材质系统验收及已经跟踪的阶段验收记录 | docs/Reports/ |

同步修改 CMake、CI、脚本根目录计算、Doxygen、文件头路径说明及当前文档链接。历史设计和计划保留原文件，增加目录变更说明，避免重写历史叙述。

### 本地生成物迁移

1. 建立 CMake presets，把引擎的新构建统一放到 Intermediate/Build/<配置入口>，依赖缓存放到 Intermediate/Dependencies；保留本机依赖路径的用户覆盖入口，不在仓库中固化 D:/OpenSourceProject。
2. 先构建并验证新入口；新构建和 SDK 验证必须不再引用待迁移的旧目录。
3. 维护脚本先输出逐项迁移预览，检查项目根、CMake HOME、依赖引用、目录占用与目标冲突，再允许应用。
4. 无引用的旧 build* 目录归档到 Intermediate/LegacyBuilds/。这是历史缓存归档，不冒充可以直接继续使用的 CMake 构建目录。
5. 引擎本地的旧日志、崩溃、旧 Dist 和 imgui.ini 按类别归入 Saved/Archive/；有运行进程、未知归属或冲突时保留原件并报告。已有 Saved 和 artifacts 不重复搬运。
6. 主工作区中未跟踪的旧报告和脚本按清单迁移并保留未跟踪状态；不自动提交它们的内容，不删除未知文件，不扫描或移动仓库以外的游戏项目。
7. 引擎自身的新维护/工具日志使用 Saved 下的专用目录。游戏项目现有日志、缓存和资源布局不改变。

根目录不会一次性删除任何构建缓存。必要时先完成源码整合与新依赖入口验证，再执行本地归档，避免破坏当前工作树依赖。

## 7. 兼容与验收

- 独立配置/编译 ProjectTool 时关闭网络且没有 SDL/ImGui 路径，仍能得到可运行 EXE。
- 移动后的 SDK 可由安装的 ProjectTool 在含中文和空格的项目路径中生成 SLN，不访问引擎源码。
- 已有 CMakeCache 指向 SDK A 时，选择 SDK B 重新生成，产物实际使用 B；生成后可正常编译项目并在编辑器 Play。
- 生成工程不会加载项目 DLL；无效路径、无 CMakeLists、工具链缺失和 CMake 失败返回非零并留下诊断。
- 临时注册表测试覆盖引号/中文路径、重复注册、部分写入失败、拥有者保护和注销；确认默认打开方式没有改变。
- 在最终安装目录注册后，手工核对 Explorer 菜单并从右键完成一次实际生成；Windows 11 的经典菜单入口明确记录。
- 其他 Programs 目标仍能构建，Commandlet 校验、原有打包目标及 CI 调用路径有效。
- 文档索引和被移动的现行链接可解析；生成目录不进入 Git，必要测试输入可从干净检出取得。
- 新引擎构建、SDK 外部消费、已有项目打开、Play/Stop、材质及编辑器回归通过后，才迁移旧缓存。
- 每个实际本地移动保留清单和可执行回退路径；验证失败先修复或回退已确认的迁移，不清空历史缓存。

## 8. 落地顺序

在现有 SDK 工作树顺序实施：先共享生成服务和独立工具，再完成注册/安装，随后移动 Programs、Scripts 和文档，最后统一构建入口并处理本地生成物。每批先验证，再继续下一批。

主工作区的本地归档必须晚于新源码整合和新构建验证。实施计划需要列出具体提交、验证命令以及主工作区整合步骤；不能只交付另一份工作树而让日常目录继续维持旧结构。
