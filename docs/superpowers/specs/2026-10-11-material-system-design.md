# TinyEngine 简化材质系统设计

日期：2026-10-11。状态：规划稿，尚未实施。

## 1. 目标与已确定范围

用户要求参考本机 `D:/UnrealEngine/UnrealEngine-5.3.2-release/Engine`，规划在 TinyEngine 中实现简化材质系统；已选择“PBR 材质 + 父子实例 + 基础节点编辑”。

完成后的工作流：编辑一份父材质节点图，暴露颜色、粗糙度和纹理参数；创建多个 `.mat` 实例，分别覆盖参数；赋给场景模型后，正确使用现有直接光、环境 IBL、阴影和后处理。运行时可以创建独立动态实例并修改参数。编译失败时，场景保留上一份可用材质。

项目约束：

- C++17；Windows；现有 SDL2 + OpenGL 4.3 渲染后端。
- 使用现有 ImGui、资源管理器和资产 GUID；不新增第三方依赖。
- UE 5.3.2 仅作为职责划分和数据流的源码参照；实现使用 TinyEngine 类型和 GLSL，不依赖 UE 构建或运行库。
- 旧 `.mat`、`TINY_MATGRAPH 1`、Assimp 导入、Demo.scene 和现有渲染稳定性测试必须继续工作。
- GPU 对象只在持有 OpenGL 上下文的线程创建、替换和销毁。
- 第一版只支持 Surface 域、UV0、Float/Vec2/Vec3/Vec4/Texture2D 参数；最多 64 个数值参数、8 个材质纹理参数。
- 本次交付设计与实施计划；产品代码、构建文件和已有素材尚未修改。

## 2. 本机 UE 5.3.2 的参考位置

所有 UE 路径均相对于 `D:/UnrealEngine/UnrealEngine-5.3.2-release/Engine/`。行号为本次检查时的位置。

| 源码 | 观察到的职责 | TinyEngine 借鉴内容 |
|---|---|---|
| `Source/Runtime/Engine/Classes/Materials/MaterialInterface.h:254`；`GetRenderProxy:454` | 材质与实例向渲染器提供统一入口 | 用统一的解析结果与渲染快照衔接资产和绘制，无需移植 UObject 继承体系 |
| `Source/Runtime/Engine/Classes/Materials/Material.h:415` | 材质保存计算规则和渲染设置；BlendMode:453、ShadingModel:475、TwoSided:525 | 父材质定义保存节点图、渲染设置和参数默认值 |
| `Source/Runtime/Engine/Classes/Materials/MaterialInstance.h:525` | Parent:546；标量、向量、纹理覆盖分别在 617/621/629 | 实例保存父引用与带类型的覆盖值 |
| `Source/Runtime/Engine/Classes/Materials/MaterialInstanceDynamic.h:12` | 提供 SetScalar/SetVector/SetTexture 和 Create | 独立的运行时参数覆盖对象，不修改共享资产 |
| `Source/Runtime/Engine/Classes/Materials/MaterialExpression.h:312` | 表达式通过编译器生成代码；319 起也提供新 HLSL 生成接口 | 小型有类型表达式图；按输出依赖生成表面函数 |
| `Source/Runtime/Engine/Public/MaterialCompiler.h:84`；`Private/Materials/HLSLMaterialTranslator.h:235` | 编译接口与代码生成分工 | CPU 材质编译器产生 GLSL 和参数布局，不创建 GL 对象 |
| `Shaders/Private/MaterialTemplate.ush:304`；`BasePassPixelShader.usf:807`；`ShadowDepthPixelShader.usf:70` | 表面输入接入通用模板；颜色与阴影阶段都调用材质求值 | 颜色、阴影共享材质表面/透明度规则 |
| `Source/Runtime/Engine/Public/MaterialShared.h:887`、`:1190` | ShaderMap 标识、依赖与程序集合 | 结构、模板依赖、Pass、几何路径参与缓存键 |
| `Source/Runtime/Engine/Public/Materials/MaterialRenderProxy.h:93`、`:125` | 渲染代理缓存 uniform 表达式和 GPU 数据 | 不可变渲染快照；参数更新与程序编译分离 |

UE 同时包含传统表达式编译和新的 HLSL 生成路径。第一版借鉴它们共同的边界，不复制两套编译器、复杂 ShaderMap 或线程系统。

## 3. 当前 TinyEngine 的基础与缺口

- `include/MiniEngine/Resource/Resource.hpp:205`：MaterialResource 已保存固定 PBR 字段、parent、override 和自定义 Vec4 参数。
- `src/Resource/Resource.cpp:830`：加载时递归继承；未写 override 的子属性可以直接覆盖父属性，显式 `override property 0` 禁止该覆盖。迁移时要保留这一旧格式语义。
- `src/Render/MaterialGraph.cpp:87`：现有图支持 Constant/Texture/Add/Multiply/PbrOutput，生成完整的简化片元光照代码。
- `editor/EditorApp.cpp:3945`：保存图后导出 `.frag`，没有把编译结果安装为完整运行时材质。
- `src/Render/SdlGlRenderDevice.cpp:1460`：shader 路径用于参数反射；主绘制仍使用固定 program，缺少按材质选择程序与绑定布局。
- `src/Render/Renderer.cpp:165`：合批按 mesh 路径和 material 路径，无法区分同一路径上的动态参数覆盖。
- `src/Resource/ResourceManager.cpp:73`：通用 Reload 先 Unload，再 Load；材质热更新需要独立的候选与生效流程。
- `src/Resource/AssetRegistry.cpp:77`：结构化依赖推断目前主要覆盖动画资产，需要补材质父引用、图和纹理依赖。
- 默认 Shader 输出 HDR 颜色、运动矢量和间接光照附件；图材质必须保留这套契约。
- 当前阴影片元使用随机阈值处理 alpha；不能将它误认为可配置的 Masked 裁切。

当前 IBL 公式和环境 atlas 采样有此前审计发现的问题。本项目先保持现有光照基线，让固定材质和图材质共享它；IBL 物理正确性修复单列后续工作，不把两种材质图像一致误认为光照公式已经正确。

## 4. 方案选择

| 方案 | 优点 | 代价 | 决策 |
|---|---|---|---|
| 固定 PBR 模板 + 参数实例 | 改动小，容易验证 | 节点图只能做有限参数配置 | 作为第一阶段的可运行基础 |
| 每份材质独立完整 Shader | 手写代码自由度高 | 光照、阴影、MRT 和绑定规则容易重复或分歧 | 本期不采用 |
| 节点图生成表面函数 + 引擎模板 | 图与固定 PBR 共用光照、Pass 与绑定；便于扩展 | 需要有类型编译器与程序缓存 | 推荐的最终架构 |

## 5. 模块边界

| 模块 | 输入、输出与职责 | 生命周期 |
|---|---|---|
| MaterialTypes | 参数类型、渲染状态、诊断、布局与解析快照等数据契约 | 无服务、无 GPU 依赖 |
| MaterialDefinitionResource | 从 `.matgraph` 加载节点、参数声明/默认值和渲染设置；内置 PBR 也形成同类定义数据 | 共享 CPU 资产 |
| MaterialResource | 保留 `.mat` 类型和旧 Get 接口；新增定义引用、父引用及 typed overrides | 共享 CPU 资产 |
| MaterialCompiler | 验证图并生成 EvaluateSurface/EvaluateOpacity、参数布局和各 Pass 源码；产出 CompiledMaterial | CPU，无 GL 调用 |
| MaterialSystem | 解析继承、管理已生效的不可变定义/实例快照、构建参数、处理依赖和候选更新 | Renderer 拥有；使用 ResourceManager |
| DynamicMaterialInstance | 引用资产路径，加一层独立 typed overrides；成功修改增加 revision | 每个需要独立参数的对象持有 |
| MaterialRenderProxy | 已解析的编译结果、数值数据、纹理资源、渲染状态和批次身份 | 按 generation/revision 缓存的不可变快照 |
| GlMaterialProgramCache | 按 Pass/几何路径创建并缓存 GL program；缓存该 program 的 uniform 位置与 UBO；安装、失败回退、释放 | SdlGlRenderDevice 拥有；仅 GL 线程 |

Renderer 通过 `SetResourceManager` 初始化自己的 MaterialSystem，并公开 `GetMaterialSystem()`。编辑器经现有 EngineContext.renderer 使用它，不增加 EngineContext 服务字段。动态实例由 `MaterialSystem::CreateDynamicInstance(assetPath)` 创建，保存已解析的参数声明用于类型校验；构造函数不直接向调用方暴露。渲染 API新增 `SetDynamicMaterial(Entity, shared_ptr<DynamicMaterialInstance>)`；映射用带代际的 Entity，实体销毁或 Renderer::Shutdown 时清理。动态覆盖不序列化进 `.scene`，基础资产路径仍在 MeshComponent.materialPath。

RenderItem 保留旧字段作为迁移适配，同时增加 `shared_ptr<const MaterialRenderProxy> materialProxy`。NullRenderDevice 只读取快照，不创建 GPU 资源。GPU program ID 不出现在通用资源类型中。

## 6. 资产格式与实例语义

### 6.1 父材质与图

`.matgraph` 是父材质定义资产。新格式 `TINY_MATGRAPH 2` 保存渲染设置、带类型参数声明、节点和连线；仍登记为 MaterialGraph，避免打断现有资产浏览器。

参数声明是类型、默认值、范围、显示名/分组和纹理采样语义的唯一来源；参数节点只引用声明。参数内部名大小写敏感，必须匹配 `[A-Za-z_][A-Za-z0-9_]*`，且禁止引擎保留前缀 `te_`；显示名和路径允许中文及空格。参数声明按内部名排序生成稳定布局。

v1 图按旧枚举值读取，不改变 0..4 的含义。旧输出 0 的 RGB 迁为 BaseColor、A 迁为 Opacity；旧输出 1/2 的 X 迁为 Metallic/Roughness，输出 3 的 RGB 迁为 Emissive；其余输出使用默认值。下一次显式保存时写 v2；读取不会自动覆盖磁盘。

v2 的规范记录格式如下，枚举写稳定字符串，字符串统一 quoted：

```text
settings <Lit|Unlit> <Opaque|Masked|Alpha> <twoSided:0|1> <maskCutoff>
parameter "name" <Float|Vec2|Vec3|Vec4|Texture2D> <SRgb|Linear|Normal> <typed-default>
parameter_meta "name" "displayName" "group" <hasRange:0|1> <minimum> <maximum>
node <id> <nodeType> "displayName" <x> <y> <constantType> <vx> <vy> <vz> <vw> "parameterName" "channelMask"
link <fromNode> <fromOutputPort> <toNode> <toInputPort>
```

typed-default 根据类型写 1/2/3/4 个数值或一个 quoted 路径；非纹理参数的 sampling 写 Linear。node 的 constantType 为 Float/Vec2/Vec3/Vec4，仅 Constant 使用；其它节点写 Float 和四个零。parameterName 仅参数节点使用，channelMask 仅 ComponentMask 使用，其它字段写空字符串。channelMask 使用 xyzw，长度 1..4，取值不能超过输入维度。settings 恰好一条，parameter_meta 每声明最多一条，缺失使用内部名显示、空组、无范围。普通节点只有一个输出端口 0；SurfaceOutput 无输出端口，输入端口按 BaseColor/Metallic/Roughness/NormalTS/AO/Emissive/Opacity 顺序为 0..6。TextureSample 输入端口为纹理 0、UV 1；二元节点 A/B 为 0/1；Lerp A/B/T 为 0/1/2；Clamp 值/Min/Max 为 0/1/2。

### 6.2 实例

`.mat` 新格式为 `TINY_MAT 2`，保存 `definition` 或 `parent` 之一，以及覆盖值。只有有覆盖记录的参数才覆盖；删除覆盖即恢复继承。不允许同时指定 definition 和 parent。新实例必须有其中一个；内置定义用 `builtin:PBR`。

```text
TINY_MAT 2
definition "assets/Materials/M_Paint.matgraph"
parameter "PaintColor" Vec4 1 0.05 0.02 1
parameter "PaintRoughness" Float 0.2
texture "PaintBaseMap" "assets/Textures/Paint Base.png"
```

```text
TINY_MAT 2
parent "assets/Materials/MI_PaintRed.mat"
parameter "PaintColor" Vec4 0.02 0.1 1 1
```

所有路径字段使用 quoted 读写。逻辑 `assets/...` 使用 ResourceManager 的资产根/挂载路径；显式相对父文件路径在该文件目录解析；规范化路径用于循环检查。父链最多 32 个实例节点，定义不计入；超过或循环均报告完整引用链。未知参数、类型不匹配、重复参数、非法版本直接拒绝候选。定义更新删除或改变参数类型时，旧覆盖进入“失效覆盖”列表：运行时忽略并使用新默认值，编辑器展示诊断供修复；首次加载无效覆盖仍作为错误拒绝。

无版本头旧 `.mat` 继续使用旧语义，适配到内置 PBR；color、metallic、roughness、emissive、各贴图、packedLayout 与 blend 均保留。带旧 shader 字段的材质走默认程序的兼容分支，保留原有 uniform 参数行为；反射结果改为按源码版本缓存。其它旧材质走统一 PBR 适配。两条路径的光照辅助代码同源；本期不把任意旧 `.frag` 当作新图材质定义。新格式实例由编译器布局驱动，不使用逐 draw 的文本参数反射。

旧 ORM/RMA 按现有 Shader 通道解释保留；目前 RMA 标签对应 R=roughness、G=AO、B=metallic。编辑器展示实际通道，避免默默将它改为另一种 RMA 约定。

## 7. 表面与节点契约

SurfaceData：BaseColor(Vec3，默认 0.8)、Metallic(Float，0)、Roughness(Float，0.7)、NormalTS(Vec3，0/0/1)、AO(Float，1)、Emissive(Vec3，0)、Opacity(Float，1)。所有数值是线性空间；颜色纹理用 sRGB 采样，自发光颜色纹理同样如此，Normal/Metallic/Roughness/AO/打包纹理用线性采样。

最终 Roughness 限制到 [0.04,1]，Metallic/AO/Opacity 到 [0,1]。法线转换由模板统一处理；零长度法线回退 0/0/1。Unlit 的颜色输出为 BaseColor + Emissive，间接光附件为零；Lit 接入当前 GGX、分簇灯光、阴影和 IBL。材质 AO 只影响间接光。

首版节点：Constant（Float/Vec2/Vec3/Vec4）、ScalarParameter、VectorParameter、TextureParameter、TextureSample、TexCoord(UV0)、Add、Multiply、Lerp、OneMinus、Clamp、ComponentMask、NormalDecode、SurfaceOutput。TextureParameter 输出纹理句柄，只能连入 TextureSample；采样结果为 Vec4；NormalDecode 将采样 RGB 解码为切线空间 Vec3。PbrOutput 是读取 v1 时的 SurfaceOutput 别名。

输入类型规则：数值类型只能相同维度或 Float 广播；向量截取必须用 ComponentMask，组向量本期不增加节点。Lerp 的 A/B 按上述规则统一，T 必须 Float；Clamp 的 Min/Max 必须 Float；TextureSample 的 UV 必须 Vec2；SurfaceOutput 各输入按声明类型连接。旧图转换器显式补通道选择。

未连线默认：SurfaceOutput 使用上述 SurfaceData 默认值；TextureSample 的 UV 为 UV0、纹理输入缺失报错；Add/Multiply 的 A/B 为 Float 0；Lerp 的 A/B/T 为 0/0/0.5；OneMinus 为 0；Clamp 的值/Min/Max 为 0/0/1；ComponentMask 输入为零 Vec4；NormalDecode 输入为 Vec3(0.5,0.5,1)。参数节点引用不存在的声明报错。UV0 以外的 TexCoord 索引报错。

恰好一个 SurfaceOutput。检查重复 ID、缺失节点、端口范围、重复输入连线和类型；可达节点发生环则失败，孤立环给警告且不生成代码。编译只遍历输出可达节点，拓扑顺序稳定，公共子表达式只生成一次。编辑位置和显示分组不进入程序键。第一版不支持 Custom HLSL/GLSL、材质函数、Time、静态开关、顶点位移或多 UV。

## 8. 渲染状态、Pass 与绑定

定义级设置：Surface 域、Lit/Unlit、Opaque/Masked/Alpha、TwoSided、MaskCutoff（默认 0.5）。实例第一版只覆盖暴露的数值和纹理，不覆盖渲染设置。MaskCutoff 是定义设置；修改它刷新有效参数，不生成新 GLSL。

- Opaque：写深度，无混合，无 alpha discard。
- Masked：写深度，无混合；颜色和阴影均调用同一个 EvaluateOpacity，并按 `opacity < cutoff` 丢弃。
- Alpha：保留当前排序、SRC_ALPHA 混合、深度只读和现有透明 MRT/后处理行为；阴影保留当前随机阈值 alpha 近似，求值改为同一透明度函数。本期不新增折射、透明专用 TAA/GTAO 或物理透射。
- TwoSided：每个 draw 明确设置剔除状态；颜色路径对背面一致翻转几何法线与切线坐标系。阴影路径使用相同剔除配置。
- Base Pass 保留 color/velocity/ambient 三个输出。Shadow Pass 只写深度；仅生成透明度依赖，不执行光照或不相关节点。
- 几何路径只支持 Static、Skinned、Instanced 三种；不支持 Skinned+Instanced 组合。共享顶点变换辅助代码保证当前姿态在颜色和阴影一致；保留已有运动矢量能力，前帧骨骼姿态的精确速度另列后续。

OpenGL 参数布局：新生成模板使用 `#version 430 core`；`std140`、UBO binding=2；每个数值参数占一个 Vec4 槽，Float/Vec2/Vec3 分别使用 x/xy/xyz。最多 64 槽，即 1024 字节，另有独立引擎材质设置 uniform。链接后验证激活字段的偏移、16 字节槽步长、块大小与 sampler 类型，不能只依赖源码注释反射；没有可达数值参数时允许块被优化掉。旧 Shader 兼容分支仍使用原有 uniform 上传方式。

纹理单元固定分配：材质 0..7；三级阴影 8..10；Irradiance/Prefilter/BRDF LUT 11..13；现有 light/cluster buffer 14..15。初始化查询 `GL_MAX_TEXTURE_IMAGE_UNITS` 和 UBO 限制，不足时报明确错误。GPU culling/天空/后处理使用自身绑定，返回材质绘制后重新绑定全部需要的材质与场景资源，不复用失效全局 uniform location。

无贴图默认：颜色/数据白图；法线编码图 (0.5,0.5,1)；自发光默认值为零。空路径表示使用默认图。非空纹理路径缺失时不让整个材质变粉：按采样语义回退并报告一次诊断；实例纹理类型不允许改变定义中的采样语义。同一图片被颜色/数据用途引用时，GPU 缓存必须区分 sRGB/Linear。

## 9. 缓存、更新与错误处理

程序缓存键含规范化图结构/字面常量/参数布局/纹理采样语义、模板展开内容、编译器版本、Pass、几何路径、Lit 与 BlendMode。TwoSided 是 GL 状态；cutoff、参数默认值和实例数值/纹理路径不进入程序键。哈希使用确定性算法，并以规范化内容比对解决碰撞，不使用不稳定的 std::hash 持久化键。

Shader 家族按使用的变体惰性生成；共享同一代码的实例复用程序。V1 只保存生成源码、布局和依赖内容摘要到 Saved/MaterialCache；不保存跨驱动 GL program binary，也不引入后台 Shader 编译进程。

MaterialSystem 保存有效 generation 与候选 generation。Reload 流程为：读候选资产 → 解析继承/图/布局 → CPU 编译 → 当前 GL 线程验证必需 Static Base/Shadow 和已使用的其它变体 → 一次替换有效快照。任一步失败，旧表面代码、布局、参数与纹理绑定一起保留；首次加载失败用独立的洋红 Unlit 错误材质。磁盘上的外部无效编辑保留为草稿，诊断携带资产路径、节点 ID/端口或 GLSL 行号。

编辑器“保存并编译”先验证候选，再原子写文件、发布 generation；失败保留编辑草稿与旧有效画面。实例参数变更通过类型检查后保存并刷新快照，不编译程序。第一版通过显式保存/重载更新，不增加文件监听线程。图和父材质更新使依赖子实例失效；参数默认值变更刷新值但复用程序。

CPU 资源加载可以在已有工作线程运行；MaterialSystem 的候选发布、动态实例修改与 Renderer 取快照均在主线程帧边界。绘制只读已生效快照，不能与 ResourceManager::Unload/Load 竞争。图材质的资源重载走 MaterialSystem，通用非材质资源 Reload 行为保持原样。

批次身份包括 mesh 身份/版本、程序描述、渲染状态、解析后的参数值、纹理资源身份/采样语义与有效版本。hash 相同仍检查内容。只有完全相同状态且满足现有几何约束的项才能合批。不同动态覆盖拆批，同值覆盖可以合批；不新增每实例材质参数 SSBO。

## 10. 编辑器与发布

沿用现有材质面板、图画布、资产浏览器。新增：父图“创建实例”；参数暴露和默认值/范围/分组编辑；实例覆盖、恢复继承、类型诊断；图节点真实输入/输出端口和端口标签；删除节点/连线、类型匹配提示；编译错误定位；用当前场景作为效果预览。第一版不增加独立预览球窗口、自动布局或材质函数编辑器。

将编辑与保存逻辑从 EditorApp 的大函数中提取到 MaterialAssetEditor/MaterialGraphEditor；这些模块读写草稿并发出保存/应用请求，EditorApp 继续提供资源与渲染上下文。

资产依赖索引补 `.mat` 的 definition/parent/纹理与 `.matgraph` 默认纹理；资产移动/重命名的结构化引用改写支持这些字段。Package/Cook 保留图和实例源资产；离线工具做 CPU 验证，运行时在目标 GPU 编译。模板外部文件和构建时嵌入数据由同一源生成，包含依赖变化必须触发 CMake 重新配置；无 assets/shaders 目录时仍能渲染图材质。

## 11. 文件责任清单

新增头/源（同名 `.hpp`/`.cpp`）：

- `include/MiniEngine/Render/MaterialTypes.hpp`：CPU 数据契约。
- `include/MiniEngine/Resource/MaterialDefinitionResource.hpp` + `src/Resource/MaterialDefinitionResource.cpp`：父图资源。
- `include/MiniEngine/Render/MaterialCompiler.hpp` + `src/Render/MaterialCompiler.cpp`：有类型图编译。
- `include/MiniEngine/Render/MaterialSystem.hpp` + `src/Render/MaterialSystem.cpp`：解析、快照、动态实例和更新。
- `src/Render/GlMaterialProgramCache.hpp/.cpp`：后端私有程序、UBO 与绑定缓存。
- `editor/MaterialAssetEditor.hpp/.cpp`、`editor/MaterialGraphEditor.hpp/.cpp`：编辑草稿和界面。
- `assets/shaders/material/Surface.glsl`、`Lighting.glsl`、`VertexTransform.glsl`、`BasePass.vert/.frag`、`ShadowPass.vert/.frag`：共享接口、现有光照和 Pass 模板。
- `cmake/EmbedMaterialShaders.cmake`：把同一模板与 include 内容嵌入备用路径。
- `tests/MaterialSystemTests.cpp`、`tests/MaterialRenderTests.cpp`：CPU 契约与真实 GL 渲染。

修改：Resource.hpp/Resource.cpp；ResourceManager 的材质加载衔接；AssetRegistry、AssetMaintenance；MaterialGraph；Renderer.hpp/.cpp；SdlGlRenderDevice.cpp 与必要测试入口；ShaderPipeline；BuiltinShaders.hpp.in；EditorApp.hpp/.cpp；CMakeLists.txt；AssimpImporter 的兼容验证；AssetCompiler 的材质验证；使用指南/资产格式文档。

不为了本项目拆分其它渲染、ECS、动画或项目服务。

## 12. 验收与完成条件

1. 旧 Demo 和 Assimp 生成的材质行为兼容；旧显式 override=false 不意外覆盖父值。
2. 创建父车漆图、红/蓝实例：同一 Shader 家族、不同参数；换色和换贴图不增加程序编译次数。
3. 父参数变化更新未覆盖子实例，已覆盖子值不变；三层继承、循环、中文空格路径都有确定行为。
4. 图材质与等价固定 PBR，在相同输入下 HDR 颜色相对误差 <= 1%，比较时关闭 TAA/Bloom/GTAO 并使用固定光照；此项只检验路径一致性。
5. Masked 颜色孔洞和阴影孔洞一致；TwoSided 与剔除状态在交替 draw 时不串用；蒙皮/实例几何进入正确变体。
6. 运行时两个动态实例互不影响，合批不混色；相同参数可复用程序与批次。
7. 图环、类型错、GLSL 编译错或错误重载保留上一有效 generation；从错误修复后可以恢复。
8. 程序绑定按实际 program 缓存位置；数值 UBO、材质纹理、三级阴影、IBL、light/cluster buffer 无槽位冲突。
9. 无外部 Shader 目录的嵌入模板测试通过；从不同工作目录启动的打包项目能加载材质依赖。
10. 现有 core/render_stability/render_builtin_shaders/import/editor 回归继续通过。全部阶段完成才称为第一版材质系统完成。

工作顺序：契约与兼容 → 父图/实例解析 → CPU 编译 → GL 程序/绑定 → Pass 与动态实例 → 编辑器 → 热更新/发布与回归。详见相邻实施计划。
