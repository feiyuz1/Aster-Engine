# Project SDK P0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 独立项目仅使用已安装 TinyEngine SDK 编译 DLL，编辑器 PIE 和独立游戏启动器都实际调用其中的项目逻辑。

**Architecture:** 保留静态引擎和现有 PIE，以独立 ProjectModuleHost 管理项目 DLL 和每次 Play 的会话。项目仅使用版本化 C 契约及 C++ 包装；宿主通过清单、二进制元数据和 SDK 标识验证真实加载内容。P0 先提供实体查询、Transform、输入和日志，组件注册/序列化的完整功能由 P1/P2 实现。

**Tech Stack:** C++17、Windows x64、Visual Studio 2022、CMake、CTest、现有 SDL2/OpenGL/ImGui；不增加第三方依赖。

**Spec:** [项目 C++ 与编辑器一体化设计](../specs/2026-10-19-project-code-integration-design.md)，本计划实施第 16 节 P0，并提前接入供 P0 验收所需的基础项目会话调用。

## Global Constraints

- Windows x64、Visual Studio 2022 工具链、C++17；其他平台单独扩展。
- 项目消费安装后的 SDK；正常项目构建不读取或编译引擎源码。
- 项目 GameRuntime DLL；编辑器和独立游戏启动器共用项目模块宿主。
- 范围受控的版本化 C 接口，外层提供方便项目使用的 C++ SDK。
- 项目显式登记；首版手写，不依赖 C++ 扫描或代码生成工具。
- 扩展现有 Play/Stop；保留单活动 World 和编辑态快照恢复。
- 重新编译后重启编辑器；第一版不在活跃进程内替换项目 DLL。
- 第一版严格绑定 SDK 契约版本和构建标识，不承诺任意编译器或 SDK 版本间兼容。
- P0 不修改 TINYSCENE 11 或冒充已实现自定义组件持久化；后续 P1–P4 仍是第一版必需阶段。
- 保留已有无代码项目、legacy GameInstance、通用插件和材质/资产工作流；SDK 模块不链接完整 tinyengine，不暴露 STL/World/Engine/ImGui。
- 本计划执行前由用户审阅；推荐当前会话顺序执行。执行时通过 using-git-worktrees 检查隔离环境，使用测试驱动和 verification-before-completion；不因本计划的模板头部自行启动子代理。

## Review Focus

1. 中文/空格路径和无关工作目录：清单、DLL 及 PDB 均按项目根定位，不能误加载同名旧库（Task 2/3/7）。
2. 清单与实际二进制不一致：SDK、项目/模块 ID 或 BuildId 不匹配时拒绝启动，不进入模块 load 或会话回调（Task 2/3）。
3. BeginPlay/Update 失败和重复 Play：会话成对清理、场景恢复，不重复调用析构或继续更新坏会话（Task 4/5）。
4. 场景替换后的旧句柄与越权访问：旧 Entity 被拒绝，注册/编辑阶段不能修改 World，错误输出不保留旧值（Task 4/5）。
5. SDK 迁移目录及引擎源码不参与消费：安装包移动后可配置/链接，纯 C 头可编译，外部项目依赖中没有 SDL/ImGui/引擎源码（Task 1/6/7）。

## 范围、文件和执行约定

P0 交付“外部 C++ 能在现有场景中运行”，示例通过项目会话查询 Player 并旋转它；它还不是可以在 Inspector 中挂载的项目组件。组件操作接口待 P1 扩展，不提供返回假成功的占位实现。P3 继续完成暂停/单步、完整回调调度、编辑器构建界面和重启恢复。

主要文件职责：

| 文件 | 职责 |
|---|---|
| include/MiniEngine/ProjectSDK/ProjectAPI.h、SDKInfo.hpp | 独立可编译的 C ABI、状态码、值/句柄和函数表，以及包装库身份查询 |
| include/MiniEngine/ProjectSDK/Project.hpp、src/ProjectSDK/Project.cpp | C++ Context/Session 包装及异常到状态转换 |
| cmake/ProjectSDKBuildInfo.h.in、cmake/ProjectSDKBuildInfo.cmake.in、cmake/TinyEngineConfig.cmake.in | 每配置 SDK 身份及可移动安装包配置 |
| cmake/TinyEngineProject.cmake、cmake/WriteProjectModuleManifest.cmake | 外部项目目标、编译元数据、配置清单和成功产物发布 |
| include/MiniEngine/Project/ProjectModuleManifest.hpp、src/Project/ProjectModuleManifest.cpp | 项目模块配置、清单与二进制元数据验证 |
| include/MiniEngine/Platform/DynamicLibrary.hpp、src/Platform/DynamicLibrary.cpp | 动态库句柄 RAII、符号、加载依赖目录和错误 |
| include/MiniEngine/Project/ProjectModuleHost.hpp、src/Project/ProjectModuleHost.cpp | 模块与会话的装载/执行/关闭状态机 |
| include/MiniEngine/Project/ProjectHostBridge.hpp、src/Project/ProjectHostBridge.cpp | 引擎服务到 C 契约、世界代次和访问权限 |
| include/MiniEngine/Runtime/Engine.hpp、src/Runtime/Engine.cpp | 共享宿主接入已有引擎生命周期和 PIE |
| tests/ProjectSDKAbiC.c、tests/ProjectSDKAbiTests.cpp | 头文件的 C/C++ 与 ABI 行为 |
| tests/ProjectModuleTests.cpp、tests/TestProjectModule.cpp、tests/TestProjectMissingEntry.cpp | 无图形的清单/动态库/服务/会话验证 |
| tests/ProjectRuntimeTests.cpp | 真实 Engine 的 PIE/游戏运行接线 |
| tests/ProjectSDKConsumer/*、tests/ProjectSDKConsumerTests.cmake | 安装 SDK 的外部消费和旋转逻辑端到端验证 |

新测试和模板被仓库白名单覆盖时，仅为本计划列明的文件/目录增加 .gitignore 例外。不要广泛取消忽略规则。

CMake 配置固定：Debug 映射模块目标 Development，Release 映射 Shipping。SDK ID 同时包含引擎版本/修订、接口版本 1、Windows-x64、编译器标识及实际 CMake 配置；项目必须使用匹配配置。其他配置在 P0 清晰拒绝，现有非 SDK 引擎构建不受此限制。

同一安装前缀允许共存 Debug/Release：分别安装 BuildInfoDebug.h、BuildInfoRelease.h 和 TinyEngineSDKInfo-Debug.cmake、TinyEngineSDKInfo-Release.cmake。公开 BuildInfo.h 根据导入目标为当前配置提供的定义选择对应头；CMake 身份文件为构建辅助提供同源的 SDK ID。不得用后安装配置覆盖公共身份常量，导致 Debug 库配上 Release 头。消费测试同时构建两配置验证这一点。

执行时以隔离工作区为当前目录，使用独立 build-project-sdk，不复用不同生成器或架构的缓存。准备配置命令：

```powershell
cmake -S . -B build-project-sdk -G "Visual Studio 17 2022" -A x64 -DTINYENGINE_BUILD_TESTS=ON -DTINYENGINE_BUILD_EDITOR=ON -DTINYENGINE_BUILD_IMPORTERS=OFF
```

这是未来实施命令，本次规划没有执行构建。已有本机依赖路径通过缓存参数传入，不修改外部依赖源码。测试断言使用现有 CHECK/独立失败退出码风格，新增可执行程序失败时必须返回非零。

ProjectModuleTests.cpp 接受 --group manifest、loader、bridge、session；各 CTest 名称仅运行相应组。ProjectRuntimeTests.cpp 接受 --group runtime。各任务先增加对应测试源/CTest 注册再补实现，编译失败型 RED 必须来自新接口尚不存在，而不是测试目标未创建。

## 固定接口约定

下列类型和方法是任务间契约；不得在后续任务中改用另一套拼写。

- C 前缀 TinyProject，入口 TinyProject_GetAPI，调用约定 TINY_PROJECT_CALL（Windows __cdecl），ABI 版本 TINY_PROJECT_ABI_VERSION=1。
- TinyProjectResult 为 int32_t；OK=0、INVALID_ARGUMENT=1、NOT_FOUND=2、NOT_READY=3、INCOMPATIBLE=4、FAILED=5、UNSUPPORTED=6、WRONG_THREAD=7、READ_ONLY=8。状态码常量统一加 TINY_PROJECT_ 前缀，例如 TINY_PROJECT_OK。全部跨界入口不抛 C++ 异常。
- TinyProjectString 为 const char* data + uint64_t size，UTF-8、不要求 NUL；长度零允许 data 为 null。错误时所有输出参数置空/零。
- TinyProjectWorld 为 uint64_t id；TinyProjectEntity 为 uint64_t world_id + uint64_t value（value 为引擎现有代际实体值）；0 无效。TinyProjectVec3 为三个 float；TinyProjectTransform 为 position/rotation/scale 三个 Vec3，旋转单位沿用引擎 Euler 度。
- TinyProjectModuleInfo 为 struct_size、module_id、project_id、sdk_build_id、project_build_id、source_fingerprint、profile（均为 TinyProjectString，标识由宿主复制）。
- TinyProjectHostAPI 前缀为 abi_version、struct_size、host_context；其函数指针统一以 void* host_context 为首参。
- Host 函数：log(uint32_t level, TinyProjectString message)->void；allocate(uint64_t size,uint32_t alignment,void** out)->TinyProjectResult；deallocate(void* memory,uint32_t alignment)->void；find_entity(TinyProjectWorld world,TinyProjectString name,TinyProjectEntity* out)->TinyProjectResult；is_alive(TinyProjectEntity entity,uint32_t* out)->TinyProjectResult；get_transform(TinyProjectEntity entity,TinyProjectTransform* out)->TinyProjectResult；set_transform(TinyProjectEntity entity,const TinyProjectTransform* value)->TinyProjectResult；get_axis(TinyProjectString name,float* out)->TinyProjectResult；is_down(TinyProjectString name,uint32_t* out)->TinyProjectResult；query_interface(TinyProjectString id,uint32_t version,const void** out)->TinyProjectResult。未知扩展返回 UNSUPPORTED，后续类型/资源子接口经此扩展。
- TinyProjectAPI 前缀为 abi_version、struct_size、info；必需函数为 load(const TinyProjectHostAPI*,void** module)->TinyProjectResult、unload(void* module)->void、create_session(void* module,TinyProjectWorld,void** session)->TinyProjectResult、destroy_session(void* session)->void、begin_play(void* session)->TinyProjectResult、update(void* session,float dt)->TinyProjectResult、end_play(void* session)->void。尾部 fixed_update/late_update 与 update 同签名，可缺省；逐字段检查大小后访问。
- TinyProject_GetAPI(uint32_t requested_abi)->const TinyProjectAPI*；未知版本返回 null。函数表/标识由模块持有到 unload 前；HostAPI 由宿主持有到模块关闭后，二者均不指向调用栈临时对象。
- C++ 包装命名空间 MiniEngine::ProjectSDK。Context::FindEntity(std::string_view)->TinyProjectEntity；IsAlive(TinyProjectEntity)->bool；GetTransform(TinyProjectEntity,TinyProjectTransform&)->bool；SetTransform(TinyProjectEntity,const TinyProjectTransform&)->bool；GetAxis(std::string_view)->float；IsDown(std::string_view)->bool；Log(std::string_view)->void。查找不到返回无效句柄，其他失败可查询 Context::GetLastResult()->TinyProjectResult。
- SDK Session 基类提供 OnBeginPlay(Context&)、OnUpdate(Context&,float)、OnFixedUpdate(Context&,float)、OnLateUpdate(Context&,float)、OnEndPlay(Context&) 虚方法，仅在模块内部使用。ExportProject<TSession>(uint32_t requestedAbi,const TinyProjectModuleInfo& info) noexcept->const TinyProjectAPI* 生成 C 调用适配；异常转换为 FAILED 并记录日志，析构只由模块适配执行。

### Task 1: 可安装、可独立编译的基础 SDK 契约

**Files:**
- Create: `include/MiniEngine/ProjectSDK/ProjectAPI.h`, `include/MiniEngine/ProjectSDK/SDKInfo.hpp`, `include/MiniEngine/ProjectSDK/BuildInfo.h`, `src/ProjectSDK/SDKInfo.cpp`.
- Create: `cmake/ProjectSDKBuildInfo.h.in`, `cmake/ProjectSDKBuildInfo.cmake.in`, `cmake/TinyEngineConfig.cmake.in`.
- Test: `tests/ProjectSDKAbiC.c`, `tests/ProjectSDKAbiTests.cpp`.
- Modify: `CMakeLists.txt`, `.gitignore`.

**Interfaces:**
- Consumes: 现有 CMake 引擎版本/修订/编译器变量及上述固定 C 契约。
- Produces: tinyengine_project_sdk（安装名 TinyEngine::ProjectSDK）；ProjectSDK/BuildInfo.h 中的 TINYENGINE_PROJECT_SDK_BUILD_ID；MiniEngine::ProjectSDK::GetSDKBuildId() noexcept->const char*；安装包的 include、lib 和 lib/cmake/TinyEngine。

- [x] **Step 1: 写失败测试。** 新增 C 与 C++ ABI 目标，测试 `c_header_is_self_contained`、`cpp17_header_is_self_contained`、`entity_handle_layout` 和 `sdk_identity_is_nonempty`。核心断言：`sizeof(TinyProjectEntity)==16`、`sizeof(TinyProjectVec3)==12`、`offsetof(TinyProjectHostAPI,host_context)==8`、`TINY_PROJECT_ABI_VERSION==1`；C 和 C++ 得到相同布局。编译命令只包含 SDK 公开头目录。

  ```cpp
  CHECK(sizeof(TinyProjectEntity) == 16);
  CHECK(offsetof(TinyProjectHostAPI, host_context) == 8);
  CHECK(MiniEngine::ProjectSDK::GetSDKBuildId()[0] != '\0');
  ```

- [x] **Step 2: 验证 RED。** 配置后执行 `cmake --build build-project-sdk --config Debug --target tinyengine_project_sdk_abi_tests`，预期缺少新头/SDK 符号，不能把原工程配置错误当作 RED。
- [x] **Step 3: 实现基础 SDK 和安装配置。** 声明固定契约，生成按配置的 BuildInfo.h，静态包装库只包含 SDK 身份实现；使用 CMakePackageConfigHelpers 和安装导出集，不调用 find_dependency(SDL2/OpenGL/ImGui)。标注 C++17 和 Windows-x64 支持条件，公开接口不 include 内部引擎头。
- [x] **Step 4: 验证 GREEN。** 构建相同目标；执行 `ctest --test-dir build-project-sdk -C Debug -R '^tinyengine_project_sdk_abi$' --output-on-failure`，预期全部断言通过。配置一次 Release，确认其 ID 与 Debug 不同且头/库一致。
- [x] **Step 5: 提交本任务涉及文件。** `git diff --check` 后提交 `feat(sdk): add installable project ABI foundation`，不包含工作区其他文件。

### Task 2: 项目代码配置、模块清单和身份校验

**Files:**
- Create: `include/MiniEngine/Project/ProjectModuleManifest.hpp`, `src/Project/ProjectModuleManifest.cpp`.
- Modify: `include/MiniEngine/Project/Project.hpp`, `src/Project/Project.cpp`, `CMakeLists.txt`, `.gitignore`.
- Test: `tests/ProjectModuleTests.cpp`.

**Interfaces:**
- Consumes: SDK 构建 ID、TinyProjectModuleInfo、现有 Config/PlatformFile。
- Produces: ProjectSettings::projectId（project.id）、codeModule（code.module）、codeRequired（code.required）、codeManifest（code.manifest）；enum class ProjectCodeProfile { Development, Shipping }；ProjectModuleSelection，拥有 projectRoot、moduleId、projectId、sdkBuildId、projectBuildId、sourceFingerprint、profile、libraryPath、pdbPath、dependencies；`bool LoadProjectModuleSelection(const ProjectSettings&,ProjectCodeProfile,ProjectModuleSelection&,std::string* error=nullptr)`；`bool ValidateProjectModuleInfo(const TinyProjectModuleInfo&,const ProjectModuleSelection&,std::string* error=nullptr)`。

- [x] **Step 1: 写失败测试。** `legacy_project_has_no_code`、`code_required_defaults_true`、`project_load_does_not_retain_previous_code_fields`、`manifest_resolves_from_project_root`、`manifest_rejects_incomplete_or_mismatched_identity`、`manifest_rejects_path_escape`。断言旧项目的 codeModule 为空；配置 code.module 后 required 默认 true；二次 Load 不残留上次字段；改变 cwd 不影响中文空格路径；任何身份不匹配返回 false 并含字段名。

  ```cpp
  CHECK(legacy.codeModule.empty());
  CHECK(codeProject.codeRequired);
  CHECK(!ValidateProjectModuleInfo(wrongSdkInfo, selection, &error));
  CHECK(error.find("sdk.buildId") != std::string::npos);
  ```

- [x] **Step 2: 验证 RED。** 构建 `tinyengine_project_module_tests`，预期缺少新配置/清单接口；随后执行 `ctest --test-dir build-project-sdk -C Debug -R '^tinyengine_project_manifest$' --output-on-failure` 观察相应断言失败。
- [x] **Step 3: 实现配置与清单。** ProjectSettings 保存/读取上述四字段，codeModule 非空时要求 project.id 和 code.manifest 有值。使用 u8path，按项目根规范化所有产物路径；拒绝绝对路径、.. 逃逸及符号链接解析后越界，明确哪些依赖属于系统无需列入。清单键固定为 module.id、project.id、sdk.buildId、module.abi、module.platform，以及 Development/Shipping 的 library、pdb、buildId、sourceFingerprint、schemaHash、dependencies.count、dependencies.N。schemaHash 在 P0 固定为 none，不能宣称包含已登记组件。
- [x] **Step 4: 验证 GREEN。** 运行 tinyengine_project_manifest；另回归已有 `tinyengine_core` 中项目设置读写，确认无代码项目不新增必需字段。配置错误保留错误原文和已解析路径。
- [x] **Step 5: 提交。** `feat(project): validate project module manifests`；检查 .tinyproject 和测试临时资产未被意外修改。

### Task 3: 动态库所有权与项目模块装载

**Files:**
- Create: `include/MiniEngine/Platform/DynamicLibrary.hpp`, `src/Platform/DynamicLibrary.cpp`, `include/MiniEngine/Project/ProjectModuleHost.hpp`, `src/Project/ProjectModuleHost.cpp`.
- Modify: `include/MiniEngine/Plugin/PluginManager.hpp`, `src/Plugin/PluginManager.cpp`, `CMakeLists.txt`.
- Test: `tests/TestProjectModule.cpp`, `tests/TestProjectMissingEntry.cpp`, `tests/TestDynamicPlugin.cpp`, `tests/ProjectModuleTests.cpp`.

**Interfaces:**
- Consumes: Task 1/2 的 C ABI 和 ProjectModuleSelection。
- Produces: move-only `DynamicLibrary`，Open(const std::filesystem::path&,const std::vector<std::filesystem::path>&,std::string* error=nullptr)->bool、FindSymbol(const char*) const->void*、Close() noexcept；ProjectModuleHost::Load(const ProjectModuleSelection&,const TinyProjectHostAPI&)->bool、Close() noexcept、GetLastError() const->const std::string&、GetState() const->ProjectModuleState、GetLoadedBuildId() const->const std::string&、GetLoadedLibraryPath() const->const std::filesystem::path&、GetLoadedPdbPath() const->const std::filesystem::path&；ProjectModuleState={Unconfigured,Missing,Incompatible,Registered,Playing,Faulted,Closed}。

- [x] **Step 1: 写失败测试。** `real_module_loads`、`missing_entry_rejected`、`abi_and_short_table_rejected`、`binary_identity_must_match_manifest`、`only_valid_modules_reach_load_callback`、`unicode_shadow_copy_loads`、`close_is_idempotent`。事件日志验证 load/unload 各一次；不兼容库不调用 load；复制后的真实加载路径位于 Library/ProjectModules/<BuildId>。追加通用插件测试：Startup 结束后，Tick 仍可安全通过保留的 HostAPI 记录日志。

  ```cpp
  CHECK(host.Load(validSelection, hostApi));
  CHECK(host.GetLoadedBuildId() == validSelection.projectBuildId);
  CHECK(host.GetLoadedLibraryPath().parent_path() == shadowDirectory);
  ```

  项目测试夹具通过 Host 日志记录固定事件字符串 load/create/begin/update/end/destroy/unload；失败夹具用于断言回调未发生，不通过假模块返回值替代动态装载。

- [x] **Step 2: 验证 RED。** 构建有效/缺入口夹具及 module_tests，运行 `ctest --test-dir build-project-sdk -C Debug -R '^tinyengine_project_loader$' --output-on-failure`；观察缺装载能力和 HostAPI 生命周期断言失败。
- [x] **Step 3: 实现 RAII 与模块 Load/Close。** Windows 使用明确 DLL 依赖搜索目录，失败带 Windows 错误码/路径；不改变进程 cwd 或全局 PATH。先校验清单/文件，再影子复制库、匹配 PDB（存在时）及依赖，然后查入口、前缀、逐字段大小、必需回调及实际身份，最后调用 load。失败释放本次全部句柄/上下文并清空输出；模块请求未知 ABI 返回 null。P0 不实现活跃进程内重新 Load，重复请求清晰拒绝。DynamicLibrary 保留已有非 Windows 通用插件的 dlopen 分支；项目 SDK 的首版支持范围仍为 Windows。
- [x] **Step 4: 回归与 GREEN。** PluginManager 复用 DynamicLibrary，保留原公开 API/诊断语义，为各插件提供地址稳定的 HostAPI 所有者；运行 loader 测试和 `tinyengine_core|tinyengine_extended` 中通用插件验证，确认 Startup 后 HostAPI 仍有效。新增函数表尾部字段的测试验证未知更大结构可接受、缺少可选尾字段不会越界读取。
- [x] **Step 5: 提交。** `feat(project): load validated project modules with owned libraries`。

### Task 4: World/输入桥接与 C++ 会话包装

**Files:**
- Create: `include/MiniEngine/Project/ProjectHostBridge.hpp`, `src/Project/ProjectHostBridge.cpp`, `include/MiniEngine/ProjectSDK/Project.hpp`, `src/ProjectSDK/Project.cpp`.
- Modify: `CMakeLists.txt`.
- Test: `tests/ProjectModuleTests.cpp`, `tests/TestProjectModule.cpp`.

**Interfaces:**
- Consumes: Task 1 的 HostAPI，现有 World/TransformComponent/InputSystem/Logger。
- Produces: `ProjectHostBridge(World&,InputSystem&,Logger&)`；GetAPI() const noexcept->const TinyProjectHostAPI&、GetWorld() const noexcept->TinyProjectWorld、SetWorldAccess(bool ready,bool writable) noexcept、InvalidateWorld() noexcept；SDK Context/Session/ExportProject 签名见固定接口约定。

- [x] **Step 1: 写失败测试。** `find_and_rotate_existing_entity`、`input_axis_and_action_bridge`、`stale_world_entity_rejected`、`registration_and_edit_world_read_only`、`invalid_out_parameters_are_cleared`、`wrong_thread_rejected`、`sdk_exception_does_not_cross_c_callback`。创建 Player，写 rotation.y=90 后断言引擎值为 90；直接清理测试 World 后调用 InvalidateWorld，再复用槽位，旧句柄仍失败（真实 Engine 自动通知由 Task 5 验证）；只读期 set_transform 返回 READ_ONLY 且值不变；未知接口返回 UNSUPPORTED 并置空；非主线程 World 访问返回 WRONG_THREAD；抛异常的 SDK 回调返回 FAILED。

  ```cpp
  CHECK(api.set_transform(api.host_context, player, &rotated) == TINY_PROJECT_READ_ONLY);
  CHECK(world.GetComponent<TransformComponent>(enginePlayer)->rotation.y == 0.0f);
  CHECK(api.query_interface(api.host_context, unknownId, 1, &extension) == TINY_PROJECT_UNSUPPORTED);
  CHECK(extension == nullptr);
  ```

- [x] **Step 2: 验证 RED。** 构建后运行 `ctest --test-dir build-project-sdk -C Debug -R '^tinyengine_project_bridge$' --output-on-failure`，失败应来自未接通的真实服务。
- [x] **Step 3: 实现桥接和包装。** HostAPI 为 Bridge 成员且 Bridge 不可移动；每个有效世界代次使用进程内唯一非零 ID，Clear/恢复时换 ID；find_entity 按实体句柄稳定顺序返回首个同名实体。读写复制 POD，不返回引擎对象地址；Transform 校验有限数；内存对齐参数须为有效二次幂。SDK 适配在模块内创建/销毁对象，Session 调用参数由模块内 Context 持有；C++ Session 虚接口不跨界，异常日志和状态码在适配器边界生成。
- [x] **Step 4: 验证 GREEN。** 运行 bridge 和 ABI 测试；夹具 DLL 仅链接 ProjectSDK。卸载前让 SDK 对象析构，确保没有宿主 delete 项目对象、没有仍使用已失效 HostAPI 的包装。
- [x] **Step 5: 提交。** `feat(sdk): bridge entity transforms input and project sessions`。

### Task 5: 模块会话状态机与实际 PIE/游戏接线

**Files:**
- Modify: `include/MiniEngine/Project/ProjectModuleHost.hpp`, `src/Project/ProjectModuleHost.cpp`, `include/MiniEngine/Runtime/Engine.hpp`, `src/Runtime/Engine.cpp`, `editor/EditorApp.cpp`, `CMakeLists.txt`.
- Test: `tests/ProjectRuntimeTests.cpp`, `tests/ProjectModuleTests.cpp`.

**Interfaces:**
- Consumes: 已加载模块、ProjectHostBridge、现有场景快照和 Engine 生命周期。
- Produces: ProjectModuleHost::BeginPlay(TinyProjectWorld)->bool、Update(float)->bool、FixedUpdate(float)->bool、LateUpdate(float)->bool、EndPlay() noexcept；Engine::GetProjectModuleHost()->ProjectModuleHost&、GetProjectCodeError() const->const std::string&。既有 Engine::Initialize/EnterPlayMode/ExitPlayMode 签名不变。

- [x] **Step 1: 写失败测试。** module_tests 的 `session_begin_end_twice`、`create_or_begin_failure_cleans_once`、`update_failure_faults_session`；runtime_tests 的 `editor_play_calls_project_code`、`standalone_start_calls_same_project_code`、`stop_restores_snapshot_and_invalidates_handles`、`required_missing_code_blocks_play_but_keeps_editor_scene`、`legacy_game_instance_is_not_double_updated`、`pause_skips_project_updates`。断言两次 Play 得到两组 create/begin/end/destroy，Stop 后 Transform 回原值；编辑态不调用 Update；模块失败的游戏启动返回 false，编辑器可以看场景且 Play 返回 false。

  ```cpp
  CHECK(std::count(events.begin(), events.end(), "begin") == 2);
  CHECK(std::count(events.begin(), events.end(), "destroy") == 2);
  CHECK(SceneSerializer::Serialize(*engine.GetContext().world) == editSnapshot);
  CHECK(!missingCodeEngine.EnterPlayMode());
  ```

- [x] **Step 2: 验证 RED。** `ctest --test-dir build-project-sdk -C Debug -R '^tinyengine_project_(session|runtime)$' --output-on-failure`；runtime 使用真实 SDL/OpenGL，不把无图形环境失败误当实现 RED。
- [x] **Step 3: 实现会话状态机。** BeginPlay 创建新 session，begin 成功后进入 Playing；create/begin 失败销毁已创建 session，begin 未成功不调用 end。Update/Fixed/Late 返回错误后禁止继续普通回调，保留诊断，由 Engine 结束/恢复。EndPlay 和 Close 可重复调用，最终顺序 end -> destroy_session -> unload -> 释放库。尾部固定/后置回调不存在时跳过。
- [x] **Step 4: 接入 Engine 和 UI。** 基础服务建立后、场景/资产消费前加载模块，注册期 Bridge 返回 NOT_READY/READ_ONLY；场景成功后开放查询，Play/End 清理期间按契约开放写入。新增代码失败保留编辑器场景并显示诊断；standalone 立即失败。显式项目文件/启动场景错误不回退 Demo；无参数演示路径保留。Play 先快照、再脚本/Gameplay 和项目会话，任一步失败反序清理/恢复；Tick 按规格插入 Update/Fixed/Late，项目会话启用时不再更新 legacy GameInstance。OnWorldCleared 调用 InvalidateWorld。Shutdown 在引擎服务仍有效时清理会话，待模块使用者清理后卸载；编辑器仅在 EnterPlayMode/ExitPlayMode 成功时显示成功提示。
- [x] **Step 5: 验证 GREEN。** 运行 session/runtime 和已有 scene_save、script_lifecycle、editor_interaction；核对 begin 失败、Update 失败以及直接关闭运行中的 Engine 均清理一次并保留有用错误。暂停只验证现有底层暂停能力，P0 不新增 Pause/单步界面。
- [x] **Step 6: 提交。** `feat(runtime): execute project modules in PIE and standalone sessions`。

### Task 6: SDK 项目构建辅助、可移动安装包和成功清单

**Files:**
- Create: `cmake/TinyEngineProject.cmake`, `cmake/WriteProjectModuleManifest.cmake`.
- Modify: `cmake/TinyEngineConfig.cmake.in`, `CMakeLists.txt`.
- Test: `tests/ProjectSDKConsumer/CMakeLists.txt`, `tests/ProjectSDKConsumer/Source/Game.cpp`, `tests/ProjectSDKConsumerTests.cmake`.

**Interfaces:**
- Consumes: ProjectSDK 导入目标、SDK ID、Task 2 清单格式、SDK ExportProject<TSession>。
- Produces: `tinyengine_add_project_module(TARGET <name> PROJECT_ID <stable-id> MODULE_ID <stable-id> SOURCES <list> [RUNTIME_DEPENDENCIES <list>])`；生成 ProjectModuleBuildInfo.h，供项目入口传入完整 TinyProjectModuleInfo；Binaries/Windows-x64/Project.modules 和每配置独立产物目录；CTest tinyengine_project_sdk_consumer。

- [x] **Step 1: 写失败消费夹具。** find_package(TinyEngine CONFIG REQUIRED)，仅用 TinyEngine::ProjectSDK 和上述辅助声明项目模块。OnUpdate 查询 Player 并增加 rotation.y；OnBegin/End 通过 Host 日志输出 `P0_PROJECT_BEGIN`/`P0_PROJECT_END`，更新至少一次后输出 `P0_PROJECT_ROTATED`。CTest 脚本先安装 SDK，再复制/移动到中文空格 QA 路径，在引擎源码目录之外配置和构建消费项目。断言导入依赖没有 SDL/ImGui/tinyengine，也不引用引擎源码路径。

  ```cmake
  if(imported_links MATCHES "SDL|imgui|(^|;)tinyengine($|;)")
    message(FATAL_ERROR "ProjectSDK must not depend on the full engine")
  endif()
  ```

- [x] **Step 2: 验证 RED。** 构建已存在 SDK 后运行 `ctest --test-dir build-project-sdk -C Debug -R '^tinyengine_project_sdk_consumer$' --output-on-failure`，预期缺少构建辅助/元数据能力；不能以安装前目标未构建作为 RED。
- [x] **Step 3: 实现辅助与清单发布。** 只支持预定 Debug/Release 映射，检查 project/module ID 和 SDK 配置匹配。sourceFingerprint 为按相对路径排序的源码/构建配置/显式依赖内容哈希的 SHA256；projectBuildId 为 SDK ID、profile 和 sourceFingerprint 的 SHA256，元数据编进 DLL。相关源文件加入 CMAKE_CONFIGURE_DEPENDS，内容变化也会重新计算，不只追踪文件列表变化。成功链接后先向新 BuildId 目录发布 DLL/匹配链接 PDB及已复制的显式依赖，再以临时文件和原子替换更新当前配置清单。依赖清单仅记录发布到项目目录的路径。失败保留之前清单和可用 DLL，不能直接覆盖清单仍引用的上一版本文件；另一配置记录保持不变。
- [x] **Step 4: 验证 GREEN。** 消费夹具测试通过后，故意制造语法错误并重建，验证命令失败、旧清单及旧产物哈希不变；恢复代码再构建，验证新 BuildId 和二进制元数据一致。Debug 与 Release 同时安装到指定包布局，各自导入正确库。测试使用独立 QA 目录，不重命名、删除或改 ACL 来“隐藏”用户的引擎源码目录。
- [x] **Step 5: 提交。** `feat(build): build external project modules from installed SDK`。

### Task 7: 外部项目真实运行与 P0 交付验证

**Files:**
- Create: `docs/项目代码开发.md`.
- Modify: `docs/项目工作流.md`, `docs/原生插件API.md`, `docs/文档中心.md`, `CMakeLists.txt`, `.gitignore`.
- Test: `tests/ProjectSDKConsumerTests.cmake`, `tests/ProjectRuntimeTests.cpp`.

**Interfaces:**
- Consumes: 所有前置接口、安装 SDK 的 editor/game 路径及外部消费 DLL/清单。
- Produces: CTest tinyengine_project_sdk_runtime_consumer；可按文档复现的项目会话示例和准确阶段说明。

- [x] **Step 1: 写失败端到端检查。** 从外部 QA 项目生成包含 Player 的 TINYSCENE 11 和项目配置，使用安装后 DLL 清单；在无关 cwd 启动 SDK 游戏 `tinyengine_game.exe <absolute-project-file> --frames=3`，必须退出 0 并出现 BEGIN/ROTATED/END。runtime_tests 使用同一 DLL 通过 Engine::Initialize(project,false) 和 EnterPlayMode 触发实际项目更新、Stop 恢复、再次 Play，核对对应日志和 Transform。PDB 路径须匹配影子 DLL；SDK 工作流文档提供 VS 启动宿主与附加的步骤。

  ```cmake
  if(NOT run_exit EQUAL 0 OR NOT run_output MATCHES "P0_PROJECT_ROTATED")
    message(FATAL_ERROR "Installed runtime did not execute project code: ${run_output}")
  endif()
  ```

  run_output 收集进程 stdout/stderr 和项目 Logs/TinyEngine.jsonl；不得仅检查 EXE 文件存在或进程退出码。

- [x] **Step 2: 验证 RED。** `ctest --test-dir build-project-sdk -C Debug -R '^tinyengine_project_sdk_runtime_consumer$' --output-on-failure`；如果单元通过但外部产物失败，保留实际 stdout/stderr 以定位接线缺口。
- [x] **Step 3: 补齐安装/运行接线。** 确保安装 SDK 包含宿主 EXE/基础运行依赖、模板素材、正确配置标识与构建辅助；仅修正端到端失败暴露的 P0 缺口，不绕过清单校验或链接引擎源码。文档明确这是项目会话能力，项目新建向导/SDK 模板迁移、Inspector 组件与发布由后续阶段接入，不宣称第一版已完成。
- [x] **Step 4: 验证 GREEN 与回归。** 构建本计划全部新增目标、tinyengine_game/editor 及所需既有测试目标；运行 `ctest --test-dir build-project-sdk -C Debug -R '^tinyengine_project_' --output-on-failure`，全部通过后运行一次完整 Debug CTest。Release 构建 SDK/模块与限帧游戏也通过；在 VS 手工命中外部项目 Update 断点并核对实际 DLL/PDB。环境缺少独立干净 Windows VM 时记录真实限制，不把本机运行表述为干净机器验收。
- [x] **Step 5: 自查、记录和提交。** 审查各 C 边界、HostAPI 生命周期、错误清理和依赖闭包，运行 git diff --check。记录实际命令、结果、失败修复和未完成的 P1–P4，提交 `docs(project): verify and document external SDK code execution`。P0 结束再进入下一阶段的计划，不通过改代码绕过尚未实施的数据层。

## 需求归属与自查

| 设计要求 | 本计划任务/后续归属 |
|---|---|
| SDK 独立消费、C 边界、版本标识 | Task 1/2/4/6 |
| 项目装载、影子副本、错误和通用插件兼容 | Task 3 |
| 外部代码执行、会话骨架、保留现有 PIE | Task 4/5/7；P3 扩展完整编辑调度体验 |
| 类型和属性、组件存储、TINYSCENE 12、迁移、Missing 数据 | P1，不在 P0 提供假实现 |
| Inspector、撤销、复制、Prefab、数据资源与 GUID | P2 |
| 项目新建向导切换 SDK、旧项目迁移、VS 自动配置、暂停/单步 UI、构建/重启恢复 | P3 |
| 项目目标构建、Cook、资产索引、依赖部署与原子发布 | P4；P0 只证明 DLL 可被独立启动器使用 |

自查项目：接口拼写和返回类型一致；每个 Review Focus 均有归属测试；清单和实际 DLL 双重身份检查；不会从磁盘缺失回退为成功；七项任务的实现及自动验证已完成；Step 7.4 的 VS 断点手工验收因桌面工具无法定位控件而待完成，未宣称通过，详见执行账本和项目代码开发文档。用户已用“开始做吧”确认顺序实施。
