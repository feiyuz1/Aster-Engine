# TinyEngine 简化材质系统 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 TinyEngine 中实现可实际渲染的 PBR 父材质、参数实例、动态覆盖与基础可视化节点编辑。

**Architecture:** `.matgraph` 定义表面计算和参数，`.mat` 保存实例覆盖；MaterialCompiler 生成共享模板可调用的表面函数，MaterialSystem 形成不可变渲染快照。OpenGL 后端缓存各 Pass/几何路径的程序和绑定，图材质共用现有光照。

**Tech Stack:** C++17、Windows、SDL2、OpenGL 4.3、GLSL、ImGui、现有 CMake/CTest 与 ResourceManager。

**Spec:** `../specs/2026-10-11-material-system-design.md`。

状态：用户已授权开始，任务 1–7 已完成，任务 8 正在最终验证。实现与测试结果见 `docs/材质系统验收.md`。

## Global Constraints

- C++17；Windows；现有 SDL2 + OpenGL 4.3 渲染后端。
- 使用现有 ImGui、资源管理器和资产 GUID；不新增第三方依赖。
- UE 5.3.2 仅作为职责划分和数据流的源码参照；实现使用 TinyEngine 类型和 GLSL，不依赖 UE 构建或运行库。
- 旧 `.mat`、`TINY_MATGRAPH 1`、Assimp 导入、Demo.scene 和现有渲染稳定性测试必须继续工作。
- GPU 对象只在持有 OpenGL 上下文的线程创建、替换和销毁。
- 第一版只支持 Surface 域、UV0、Float/Vec2/Vec3/Vec4/Texture2D 参数；最多 64 个数值参数、8 个材质纹理参数。
- 保留本次规划开始前的用户修改；不得为满足本计划覆盖整个 EditorApp.cpp、SdlGlRenderDevice.cpp 或 CMakeLists.txt。
- `.gitignore` 按白名单管理；规划目录、新测试和新素材可能被忽略。执行阶段只对本任务需要交付的目录/文件增加精确白名单，不移除全局规则，不用强制 add 混入其它忽略资产。

## Review Focus

- 旧 override=false、缺失 override 与图 v1 输出迁移：在 Task 1/2 测试中固定旧语义，禁止格式升级改变外观。
- 中文/空格路径、不同工作目录、父链循环：Task 2/7 测试根目录解析、quoted 路径、32 层上限和完整诊断。
- 同一图片的 sRGB/Linear 用途、缺失贴图、16 个纹理单元预算：Task 4 用真实 GPU 验证默认纹理和完整场景绑定。
- 编译失败、参数删除/改类型与父定义更新：Task 7 验证代码/布局/参数成组回退、失效覆盖和子实例失效传播。
- 同一路径动态实例、GPU culling 后绑定、阴影裁切和嵌入模板：Task 5/8 验证批次身份、Pass 一致性和发布环境。

## 执行方式与验证约定

在当前会话使用 executing-plans 逐任务实施；用户已授权“开始”。实现由主代理完成，所有任务验证完成后按技能要求进行一次独立整分支审查。

当前构建目录 `build` 已配置测试和编辑器。命令使用本机 `D:/CMake/bin/cmake.exe` 与同目录 `ctest.exe`；执行时先确认工具位置和 CMakeCache，不重新猜测依赖路径。

新增 CPU 测试目标 `tinyengine_material_tests`，CTest 名 `tinyengine_material_cpu`。新增真实 GPU 目标 `tinyengine_material_render_tests`，CTest 名 `tinyengine_material_render`、`tinyengine_material_builtin`，后者工作目录为 `build/MaterialNoShaders`。GPU 不可用时必须报告无法完成验收，不能把跳过算成通过。

各 Task 的红灯阶段先运行对应测试，确认失败来自新契约尚未实现；随后实现、绿灯、审查，再进入下一 Task。任务完成后的提交只包含该任务文件；仓库已有修改逐块审查后再纳入。以下测试名和断言是待实现验收规范，不是现有测试结果。

## Task 1：建立参数契约与旧格式适配

**Files:**
- Create: `include/MiniEngine/Render/MaterialTypes.hpp`、`tests/MaterialSystemTests.cpp`。
- Modify: `include/MiniEngine/Resource/Resource.hpp`、`src/Resource/Resource.cpp`、`CMakeLists.txt`、`.gitignore`。
- Test: `tests/MaterialSystemTests.cpp`、现有 `tests/EngineTests.cpp` 的 PBR/继承用例。

**Interfaces:**
- Produces: `enum class MaterialParameterType { Float, Vec2, Vec3, Vec4, Texture2D }`；`MaterialParameterValue` 为 `variant<float,Vec2,Vec3,Vec4,string>`。
- Produces: `MaterialParameterDecl { name,type,defaultValue,displayName,group,optional<Vec2> range,TextureSampling sampling }`；`TextureSampling { SRgb, Linear, Normal }`。
- Produces: `MaterialRenderState { shadingModel,blendMode,twoSided,maskCutoff }`，枚举按设计 §8。
- Produces: `MaterialDiagnostic { severity,assetPath,nodeId,port,message }`、`MaterialAssetData { formatVersion,definitionPath,parentPath,overrides,legacyData }`。
- Produces: `bool MaterialResource::Load(const string&)` 事务性解析；`const MaterialAssetData& GetMaterialData() const`；旧 getter 保留。

- [ ] **Step 1: 添加 TestLegacyMaterialCompatibility / TestTypedMaterialRoundTrip / TestInvalidMaterialCandidates。** 断言旧 color/贴图/packedLayout/blend 与原值一致；Vec2/Vec3/Vec4/Texture2D 往返不丢类型；quoted 中文空格路径往返；重复参数、未知版本、definition+parent 并存失败；失败 Load 保留原有效数据。
- [ ] **Step 2: 加入独立 CPU 测试 target 与精确 Git 白名单，运行红灯。** `cmake --build build --config Debug --target tinyengine_material_tests -j 8`，随后 `ctest --test-dir build -C Debug -R '^tinyengine_material_cpu$' --output-on-failure`。新接口不存在的编译失败是本阶段预期红灯。
- [ ] **Step 3: 实现带类型数据、v2 解析/保存与 legacy 适配。** 先解析临时数据再提交；旧无版本格式仍按旧 override 语义计算。明确保留 RMA 标签的实际通道含义。非材质 Resource 不重构。
- [ ] **Step 4: 构建并运行 CPU 与 core 回归。** 新测试全通过，原 PBR/实例加载用例通过。
- [ ] **Step 5: 审查并提交本任务变更。** 建议提交说明 `feat(material): add typed parameters and legacy material adapter`。

## Task 2：父图定义、继承解析与依赖索引

**Files:**
- Create: `include/MiniEngine/Resource/MaterialDefinitionResource.hpp`、`src/Resource/MaterialDefinitionResource.cpp`、`include/MiniEngine/Render/MaterialSystem.hpp`、`src/Render/MaterialSystem.cpp`。
- Modify: `MaterialGraph.hpp/.cpp`、`ResourceManager.hpp/.cpp`、`AssetRegistry.hpp/.cpp`、`src/Resource/AssetMaintenance.cpp`、CMake。
- Test: `tests/MaterialSystemTests.cpp`。

**Interfaces:**
- Consumes: Task 1 的参数、资产与状态类型。
- Produces: `MaterialDefinitionData { graph,parameters,renderState,sourcePath }`；`MaterialDefinitionResource::GetData() const`。
- Produces: `ResolvedMaterial { shared_ptr<const MaterialDefinitionData> definition; vector<MaterialParameterValue> values; vector<MaterialDiagnostic> diagnostics; uint64_t generation; }`。
- Produces: `MaterialSystem(ResourceManager&)`；`shared_ptr<const ResolvedMaterial> Resolve(const string& assetPath)`；`const vector<MaterialDiagnostic>& GetDiagnostics(const string&) const`。
- Produces: `vector<string> AssetRegistry::GetDependents(const string& path) const`；材质/图依赖推断与路径改写。

- [ ] **Step 1: 添加 TestThreeLevelInheritance / TestParentCyclesAndDepth / TestAssetRootPaths / TestMaterialDependencies。** 三层覆盖优先级为子→父→定义；删除覆盖恢复继承；旧 override=false 保留父值；32 层成功、33 层失败；A→B→A 报路径链；变更工作目录后 `assets/...` 正确，相对引用和中文空格路径正确；依赖包含父图、父实例和纹理；移动资产后引用重写。
- [ ] **Step 2: 运行 CPU 测试红灯。** 用 Task 1 命令验证新增断言失败。
- [ ] **Step 3: 实现父图资源与有上限、规范化身份的继承解析。** `.matgraph` v2 中参数声明是唯一来源；实例只存覆盖。内置 PBR 定义由 legacy 参数产生统一解析数据；加载没有图资源的旧材质仍成功。
- [ ] **Step 4: 补依赖扫描与资产移动字段改写，运行 CPU/core/import 回归。** 独立检查父子更新所需反向索引，不将热更新行为提前塞入 ResourceManager 通用 Reload。
- [ ] **Step 5: 审查并提交。** `feat(material): resolve definitions and inherited instances`。

## Task 3：有类型节点图与 CPU 材质编译器

**Files:**
- Create: `include/MiniEngine/Render/MaterialCompiler.hpp`、`src/Render/MaterialCompiler.cpp`。
- Modify: `MaterialTypes.hpp`、`MaterialGraph.hpp/.cpp`、MaterialDefinitionResource、ShaderPipeline.hpp/.cpp、CMake。
- Test: `tests/MaterialSystemTests.cpp`。

**Interfaces:**
- Consumes: MaterialDefinitionData。
- Produces: `MaterialParameterLayout { numericSlots,textureSlots,canonicalSchema }`；槽按参数名排序，64/8 上限。
- Produces: `enum class MaterialPass { Base, Shadow }`；`enum class MaterialGeometry { Static, Skinned, Instanced }`。
- Produces: `CompiledMaterial { surfaceSource,opacitySource,layout,renderState,canonicalProgramContent,dependencies,compilerVersion }`；编译器版本第一版为 1。
- Produces: `MaterialCompileResult { shared_ptr<const CompiledMaterial> material; vector<MaterialDiagnostic> diagnostics; bool Success() const; }`。
- Produces: `MaterialTemplateSources { unordered_map<string,string> files; }`；`GeneratedShaderSources { string vertex,fragment; vector<string> dependencies; }`。本任务定义类型，Task 4 提供模板文件，Task 8 提供外部/嵌入内容加载。
- Produces: `MaterialCompileResult MaterialCompiler::Compile(const MaterialDefinitionData&) const`；`GeneratedShaderSources GeneratePass(const CompiledMaterial&,MaterialPass,MaterialGeometry,const MaterialTemplateSources&) const`。

- [ ] **Step 1: 添加 TestGraphTypes / TestLegacyGraphUpgrade / TestReachableCycles / TestDeterministicCompilation / TestLimits / TestOpacityDependency。** 逐项覆盖设计 §7 的节点和类型广播；Vec4→Vec3 必须 ComponentMask；合法参数名/重复名；v1 端口与 Alpha 映射；可达环失败、孤立环警告；移动节点位置程序内容不变；Scalar 参数默认值变更不改变程序内容，Constant 值变化改变；第 65 个数值/第 9 个纹理失败；Shadow 不生成仅服务 Emissive 的采样。
- [ ] **Step 2: 运行 CPU 测试红灯。** 每个失败必须指向尚未实现的类型或生成规则。
- [ ] **Step 3: 实现图验证、稳定拓扑遍历和中间表达式值。** 共用子表达式生成一次局部变量；参数 Uniform/纹理声明来自布局；生成 EvaluateSurface 和 EvaluateOpacity，不能生成固定灯方向或独立光照 main。
- [ ] **Step 4: 实现 v1→v2 转换和规范化缓存内容。** 类型/采样语义/字面常量进程序描述；参数值、贴图路径、节点位置不进。缓存命中比较完整规范化内容，避免 hash 碰撞复用错误结果。
- [ ] **Step 5: 运行 CPU/core 回归并审查提交。** 原图测试调整为新编译契约并继续覆盖旧图加载；提交 `feat(material): compile typed graphs into surface functions`。

## Task 4：共享 Shader 模板与 GL 程序绑定

**Files:**
- Create: `src/Render/GlMaterialProgramCache.hpp/.cpp`、`assets/shaders/material/Surface.glsl`、`Lighting.glsl`、`VertexTransform.glsl`、`BasePass.vert/.frag`、`ShadowPass.vert/.frag`、`tests/MaterialRenderTests.cpp`。
- Modify: SdlGlRenderDevice.cpp、ShaderPipeline、Renderer.hpp、MaterialTypes.hpp、BuiltinShaders.hpp.in、CMake、精确白名单。
- Test: 新 GPU 测试与 RenderStabilityTests。

**Interfaces:**
- Consumes: CompiledMaterial、GeneratedShaderSources、MaterialParameterLayout、FrameView。
- Produces: `MaterialTextureBinding { string parameterName; uint32_t slot; shared_ptr<TextureResource> resource; TextureSampling sampling; }`；`MaterialRenderProxy { shared_ptr<const CompiledMaterial> compiled; vector<Vec4> numericSlots; vector<MaterialTextureBinding> textures; MaterialRenderState state; uint64_t generation,valueHash; string canonicalValueContent; }`。
- Produces: 后端私有 `GlMaterialProgram` 保存 GLuint 和该 program 的位置/块布局；`GlMaterialProgram* GlMaterialProgramCache::GetOrCreate(const CompiledMaterial&,MaterialPass,MaterialGeometry)`；`bool Bind(const MaterialRenderProxy&,const FrameView&,MaterialPass,MaterialGeometry)`，失败返回 nullptr/false 并保留诊断。
- Produces: 测试条件下的编译次数查询和线性 HDR/附件读取入口；只在 TINYENGINE_TEST_HOOKS 下开放。

- [ ] **Step 1: 添加 TestSharedPrograms / TestParameterUpload / TestTextureSemantics / TestTextureBudget / TestFallbackTexture。** 红蓝实例只编译一次同一变体；改变数值或纹理路径不新增程序；改变图结构新增；按 Float/Vec2/Vec3/Vec4 验证槽值；同一灰图在线性/sRGB用途产生预期差异；8 个材质纹理+3 阴影+3 环境+2 buffer 同时绑定不冲突；缺失贴图按语义回退且不反复刷诊断。
- [ ] **Step 2: 注册真实 GPU 测试 target 并运行红灯。** `cmake --build build --config Debug --target tinyengine_material_render_tests -j 8`；`ctest --test-dir build -C Debug -R '^tinyengine_material_render$' --output-on-failure`。
- [ ] **Step 3: 从当前默认 Shader 提取共同光照与顶点函数，接入生成表面代码。** 保留现有 IBL 基线和 MRT 输出，不同时重写光照数学。新模板用 GLSL 430；帧 uniform、对象 uniform 和材质 UBO 分开绑定。带旧 shader 字段的兼容分支保留原 program/uniform 行为，与新模板使用同源光照函数；反射按版本缓存。
- [ ] **Step 4: 实现 GL 程序缓存与绑定。** UBO binding 2、64×Vec4；材质 0..7、shadow 8..10、environment 11..13、light/cluster 14..15。所有 uniform 位置按实际 program 记录，查询能力并检查链接后的块大小/槽与 sampler 类型；即使材质没提供贴图也明确绑定默认纹理。
- [ ] **Step 5: 运行 GPU 与既有两个 render 回归，审查提交。** 提交 `feat(material): bind compiled materials through shared GL templates`。

## Task 5：渲染快照、动态实例与多 Pass

**Files:**
- Modify: MaterialSystem.hpp/.cpp、MaterialTypes.hpp、Renderer.hpp/.cpp、SdlGlRenderDevice.cpp、GL cache、BasePass/ShadowPass/VertexTransform 模板。
- Test: MaterialSystemTests、MaterialRenderTests、RenderStabilityTests。

**Interfaces:**
- Consumes: Task 2 的解析数据、Task 3 编译结果、Task 4 GPU 绑定。
- Produces: `shared_ptr<const MaterialRenderProxy> MaterialSystem::BuildProxy(const string&,const DynamicMaterialInstance* = nullptr)`，使用缓存，不逐帧重编译。
- Produces: `shared_ptr<DynamicMaterialInstance> MaterialSystem::CreateDynamicInstance(const string& assetPath)`；实例的 private 构造接收已解析参数声明和基础资产路径；公开 `bool SetParameter(const string&,const MaterialParameterValue&)`、`bool ClearOverride(const string&)`、`uint64_t GetRevision() const`，类型错误不修改值或 revision。
- Produces: `MaterialSystem& Renderer::GetMaterialSystem()`；`void Renderer::SetDynamicMaterial(Entity,shared_ptr<DynamicMaterialInstance>)`；RenderItem.materialProxy。
- Produces: RenderDevice 的默认 CPU 验证入口 `bool ValidateMaterial(const CompiledMaterial&,vector<MaterialDiagnostic>&)`；GL 实现验证 Static Base/Shadow 和已使用的几何变体，Null 实现 CPU 成功即通过。

- [ ] **Step 1: 添加 TestDynamicIsolation / TestResolvedBatchIdentity / TestPassClipping / TestTwoSidedState / TestGeometryVariants / TestPbrPathEquivalence。** 两动态实例颜色独立；同路径不同值不合批、同值可合批；类型错误 revision 不变；销毁 Entity 不留下覆盖；Masked 主画面和阴影有同一孔洞；交替 TwoSided 不串状态；Static/Skinned/Instanced 用正确顶点变换；图与固定 PBR 的 HDR 相对误差 <=1%。
- [ ] **Step 2: CPU 与 GPU 用例运行红灯。** 固定相机/灯光，关闭 TAA/Bloom/GTAO 后读线性 HDR 做一致性比较；阴影测试使用明确 UV 图案，不只检查 GLSL 字符串。
- [ ] **Step 3: Renderer 生成不可变快照，并按完整有效材质身份构建批次。** 参数与纹理内容参与批次相等性；透明和蒙皮沿用现有不能合批规则；GPU culling 返回绘制后重绑全部场景/材质资源。
- [ ] **Step 4: 接通各 Pass 的状态与几何路径。** Opaque 不 discard；Masked 共用 cutoff；Alpha 沿用现有混合/排序与随机 alpha 阴影近似。颜色保留三个输出，Unlit ambient=0；当前姿态蒙皮在颜色/阴影一致，不扩展前帧骨骼速度。
- [ ] **Step 5: 跑 CPU/GPU/core/render 回归，审查提交。** 提交 `feat(material): integrate instances and consistent render passes`。

## Task 6：可操作的材质和节点编辑器

**Files:**
- Create: `editor/MaterialAssetEditor.hpp/.cpp`、`editor/MaterialGraphEditor.hpp/.cpp`。
- Modify: EditorApp.hpp/.cpp、CMake、资产创建入口。
- Test: `tests/EditorInteractionTests.cpp`、MaterialSystemTests。

**Interfaces:**
- Consumes: 参数声明、MaterialAssetData、MaterialDefinitionData、MaterialSystem 诊断与候选验证。
- Produces: `MaterialAssetEditor::Open(path,data)`、`Draw()`、`GetDraft() const`、`IsDirty() const`；图编辑器同样提供图草稿、脏状态和保存请求，服务由 EditorApp 注入。
- Produces: 可测试命令 `SetOverride(name,value)`、`ResetOverride(name)`、`ConnectPorts(from,outputPort,to,inputPort)`、`DeleteNode(id)`，UI 与测试使用同一命令。

- [ ] **Step 1: 添加 TestMaterialOverrideControls / TestGraphPortEditing / TestCreateInstance / TestEditorCompileFailure。** 修改/恢复继承改变真实草稿；创建实例写 v2 正确父引用；连线端口按类型校验；删节点同时删相关连线；用注入的保存请求回调返回编译诊断，确认保留脏稿并显示错误；成功保存后关闭/重开无丢失。窗口无 GPU 时先测草稿命令，真实失败后的旧场景保留由 Task 7 覆盖。
- [ ] **Step 2: 构建并运行编辑器用例红灯。** `cmake --build build --config Debug --target tinyengine_editor_interaction_tests -j 8`；`ctest --test-dir build -C Debug -R '^tinyengine_editor_interaction$' --output-on-failure`。
- [ ] **Step 3: 抽取当前两个材质编辑函数的必要职责。** 参数面板从声明生成，不再把 Vec4 任意解释为所有参数；保留旧模式编辑适配。节点画布提供真实端口、参数声明面板、错误定位及全部首版节点创建。
- [ ] **Step 4: 接入保存/应用和现有场景预览。** 不增加独立预览窗口；保存流程由 Task 7 候选提交完成。窗口关闭/切换资产的脏稿处理遵循现有编辑器文档模式。
- [ ] **Step 5: 跑编辑器/CPU 回归并审查提交。** 提交 `feat(editor): edit material definitions and parameter instances`。

## Task 7：候选发布、热重载和参数失效处理

**Files:**
- Modify: MaterialSystem、MaterialDefinitionResource、MaterialResource 的候选提交路径、ResourceManager 材质衔接、AssetRegistry、GL cache、两个材质编辑器。
- Test: MaterialSystemTests、MaterialRenderTests、EditorInteractionTests。

**Interfaces:**
- Consumes: 已有解析/编译/绑定；Task 5 的 RenderDevice::ValidateMaterial。
- Produces: `MaterialReloadResult MaterialSystem::Reload(const string&,MaterialValidator)`，validator 为 `function<bool(const CompiledMaterial&,vector<MaterialDiagnostic>&)>`。
- Produces: `MaterialReloadResult MaterialSystem::ApplyDefinitionDraft(const string&,const MaterialDefinitionData&,MaterialValidator)` 和 `ApplyInstanceDraft(const string&,const MaterialAssetData&,MaterialValidator)`；结果包含 success、effectiveGeneration、diagnostics。
- Produces: 候选在本次调用内完成验证；调用来自 GL 主线程或 Null 工具环境。成功后原子写入/发布；失败不更换有效 generation。

- [ ] **Step 1: 添加 TestReloadRollback / TestInheritedInvalidation / TestParameterSchemaChange / TestNoRecompileForDefaults / TestBrokenExternalEdit。** GLSL 编译失败后 surface/layout/values/textures 与原 generation 全一致；修复后可切换；父默认值更新只影响未覆盖子；改参数类型/删除声明使旧有效覆盖进入失效列表，新默认值生效；首次加载未知覆盖拒绝；仅默认值更新程序编译次数不变；磁盘文件损坏保留上次有效画面。
- [ ] **Step 2: 运行 CPU/GPU/编辑器新增用例红灯。** 通过 validator 注入失败测试发布原子性，再用真实 GL 错误测试程序回退。
- [ ] **Step 3: 实现候选 generation 和依赖闭包验证。** 在发布前验证必需/已使用变体，所有相关子实例与参数布局一同更新；更新从主线程完成，Draw 使用 immutable snapshot。不得让通用 Reload 的 Unload 清空当前有效材质。
- [ ] **Step 4: 接通编辑器原子保存与诊断显示。** 图保存先 CPU/GL 验证再写文件；实例参数只做类型/值及依赖验证并复用程序。明确写入失败也不能发布 generation；外部草稿坏了可以继续修复重载。
- [ ] **Step 5: 跑上述回归并审查提交。** 提交 `feat(material): publish valid generations and preserve reload fallback`。

## Task 8：嵌入模板、打包、示例与整体验收

**Files:**
- Create: `cmake/EmbedMaterialShaders.cmake`；`assets/Materials/M_Paint.matgraph`、`MI_PaintRed.mat`、`MI_PaintBlue.mat`、`M_Masked.matgraph`、`MI_Masked.mat`。
- Modify: BuiltinShaders.hpp.in、CMake、ShaderPipeline 模板读取、AssetCompiler、必要打包验证、`.gitignore`、使用指南/资产格式文档。
- Test: MaterialRenderTests、MaterialSystemTests、ImportCompatibilityTests、EditorInteractionTests 与既有 render/core。

**Interfaces:**
- Consumes: 所有已完成材质 API。
- Produces: `MaterialTemplateSources`，外部模板与同源嵌入模板按规范化文件键查询；嵌入生成包含所有设计 §11 列出的 shader 文件及其 include。
- Produces: AssetCompiler 对图/实例做 CPU 验证，保留源格式发布；Saved/MaterialCache 保存生成源码、布局与依赖摘要，不发布驱动 program binary。

- [ ] **Step 1: 添加 TestEmbeddedGraphMaterials / TestPackagedWorkingDirectory / TestImportedLegacyMaterials / TestMaterialCacheInvalidation。** 无外部 shader 目录时车漆图与 Masked 正常；包在不同 CWD 加载图/父实例/默认贴图；现有 Assimp 输出不用手工改格式；模板 include 修改使程序内容缓存失效；内容相同仅时间戳变化不改变程序键。
- [ ] **Step 2: 配置独立嵌入模板 CTest 工作目录，运行红灯。** 该目录只放用例生成的材质/纹理，不放外部模板；测试不能偷偷回读项目绝对 shader 路径。
- [ ] **Step 3: 实现同源模板嵌入与离线 CPU 验证。** CMake 的 configure depends 包括共享 include；更新现有包/资源路径流程，运行时 shader 编译使用同一套模板。离线工具无 GPU 时不宣称验证 GL 链接成功。
- [ ] **Step 4: 增加可复现示例与使用文档。** 说明父图、实例、覆盖/恢复、动态实例 C++ API、首版节点和透明近似限制；示例不覆盖现有 Demo.scene 的用户改动。
- [ ] **Step 5: 执行最终验收。**

```powershell
& 'D:/CMake/bin/cmake.exe' --build build --config Debug --target tinyengine_material_tests tinyengine_material_render_tests tinyengine_tests tinyengine_render_stability_tests tinyengine_editor_interaction_tests tinyengine_import_compatibility_tests -j 8
& 'D:/CMake/bin/ctest.exe' --test-dir build -C Debug -R '^(tinyengine_material_cpu|tinyengine_material_render|tinyengine_material_builtin|tinyengine_core|tinyengine_render_stability|tinyengine_render_builtin_shaders|tinyengine_editor_interaction|tinyengine_import_compatibility)$' --output-on-failure
```

同时完成真实编辑器操作：建父图→创建红蓝实例→赋给两个模型→改变父默认值→单独动态改色→破坏并修复连线→验证 Masked 与阴影→打包异目录启动。记录 GPU、图像比较、程序编译次数和任何失败；不把 CPU 编译通过替代为整体验收通过。

- [ ] **Step 6: 审查第一版完成条件并提交。** 对照设计 §12 逐项记录结果；提交 `feat(material): ship examples and packaged material validation`。有未通过项目则第一版未完成。

## 实施阶段交付顺序

| 阶段 | 完成任务 | 可审查结果 |
|---|---|---|
| A | 1、2 | 旧材质兼容；新父图与实例可加载、继承与诊断明确 |
| B | 3、4 | 图能进入真实 GPU 绘制；程序与参数绑定分离 |
| C | 5 | 动态实例、Masked、双面、阴影与批次行为正确 |
| D | 6、7 | 编辑器能完成创建、修改、保存与失败恢复 |
| E | 8 | 示例、打包、嵌入模板与整套回归通过 |

不在未检查机器/图规模和上述阶段完成前给出确定工期。后续扩展依次考虑静态开关/函数复用、顶点位移与精确蒙皮速度、清漆/皮肤/折射；独立的 GI、Lightmap、反射探针不属于材质系统本期任务。
