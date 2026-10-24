# Aster-Engine 资产与数据格式

Aster-Engine 将“可编辑源资产”和“运行时轻量资产”分离。FBX/glTF 等源文件只在 Assimp Authoring 流程中读取；Runtime 和发布包只消费转换后的资产。

## 1. 格式总表

| 扩展名/用途 | Magic / 当前版本 | 说明 |
|---|---|---|
| `.scene` / `.prefab` | `TINYSCENE 11` | ECS 实体、组件和父子关系；读取 1–11 |
| `.mesh` | `TINY_MESH 2` | 文本网格，含 Tangent 与四骨骼索引/权重 |
| Cooked Mesh | `TNYMSH2` | 小端运行时二进制；兼容 `TNYMSH1` |
| Cooked Texture | `TNYTEX1` | 小端纹理 Header 与 Payload |
| `.skeleton` | `TINY_SKELETON 1` | 骨骼名称、父索引和 Inverse Bind |
| `.skanim` | `TINY_SKANIM 3` | Raw/压缩 P/R/S、事件和 Root Motion；读取 1–3 |
| `.blendtree` | `TINY_BLENDTREE 1` | 1D 阈值样本 |
| `.animgraph` | `TINY_ANIMGRAPH 1` | 参数、状态、Motion 与转换 |
| `.vfx` | `TINY_VFX 1` | 发射器、Flipbook 和 CPU/GPU 模式 |
| `.btgraph` | `TINY_BTGRAPH 1` | 行为树节点、边和编辑器位置 |
| `.mat` | `TINY_MAT 3 graph` / `TINY_MAT 3 instance` | 材质节点图，或引用父材质的参数实例；仅接受版本 3 |
| `.env` | `TINY_ENV 1` | Irradiance、Prefilter 和 BRDF LUT 清单 |
| `.input` | `TINYINPUT 1` | 逻辑 Axis/Action 映射 |
| `.loc` | `TINYLOC 1` | Locale/Key 字符串表 |
| Chunk Manifest | `TINYCHUNK 1` | 总大小、块大小和块文件 |
| Cook Manifest | `TINYCOOK 1` | 相对路径、时间戳和大小 |
| 网络快照 | `TNYNET1` | Sequence、时间戳与实体状态 |
| Replay | `TNYRPLY1` | 有界快照帧序列 |
| `.tpak` | `TPAK 1` | 规范化相对路径和 Payload |
| `.meta` | 文本键值 | GUID、类型、导入设置和依赖 |

## 2. 场景兼容规则

- Writer 始终写 `TINYSCENE 11`。
- Reader 继续接受 1–11，并为旧版本缺失字段填默认值；v11 为 Light 增加 Directional/Point/Spot、颜色、强度、范围和内外锥角。
- `MigrateScenes` 会先创建 `.bak` 再重写当前版本。
- 新增持久化组件或字段时必须同时更新 SceneSerializer、迁移兼容和核心测试。
- Entity 保存完整 generation 信息；加载阶段重新建立父子引用。

## 3. 动画约束

- Skeleton 父索引必须小于当前骨骼索引或为 `-1`，确保单次前向遍历可构建世界矩阵。
- Assimp 导入和 OpenGL Skinning 的有效上限为 64 根 GPU 骨骼，每顶点最多四权重。
- Clip v3 可保存 Raw Keyframe 或逐骨骼压缩曲线，二者不能同时为空或同时作为主存储。
- 曲线时间必须有序且位于 `[0, duration]`。
- Root Motion 是独立 Vec3 曲线，循环提取会累计首尾位移差。
- 事件名称不能为空，事件时间必须位于 Clip 时长内。

## 4. 材质与纹理

所有材质使用 `.mat`，在 AssetRegistry 中统一分类为 `Material`。头部区分节点图与实例；旧的无版本 `.mat`、`TINY_MAT 2` 和 `.matgraph` 均不由引擎加载。

普通材质保存完整节点、端口连线、参数声明与渲染设置，例如：

```text
TINY_MAT 3 graph
settings Lit Opaque 0 0.5
parameter "BaseColor" Vec3 Linear 0.8 0.8 0.8
node 1 SurfaceOutput "Surface" 420 60 Float 0 0 0 0 "" ""
node 2 VectorParameter "Base Color" 40 60 Float 0 0 0 0 "BaseColor" ""
link 2 0 1 0
```

实例使用同一扩展名，引用一个父材质并覆盖有类型的参数：

```text
TINY_MAT 3 instance
parent "Base.mat"
parameter "Roughness" Float 0.4
texture "BaseMap" "textures/basecolor.png"
```

- 图参数声明包括 Float、Vec2、Vec3、Vec4、Texture2D；纹理声明还包含 SRgb、Linear 或 Normal 采样。
- 实例删除覆盖记录即可继承父值。循环父链和超过 32 个实例的链会被拒绝。
- 颜色纹理按 sRGB 使用；Normal、Metallic、Roughness、AO 和打包数据保持线性。
- `scripts/migrate_materials.py <项目目录>` 可预览一次性转换，增加 `--apply` 写入并备份原文件。迁移后重新生成 Cooked 和发布包；迁移器不参与运行时加载。
- Runtime 可直接上传 DDS/KTX2 中的 BC1–BC7 层级；离线编码器不内置在引擎中。

## 5. VFX

`.vfx` 保存容量、发射率、寿命、速度、扩散、尺寸、重力、颜色、纹理、Flipbook 行列/FPS/循环与模拟模式。

- CPU 模式维护粒子数组并按纹理实例合批。
- GPU 模式不生成 CPU 粒子，由 OpenGL Vertex Shader 根据实例编号和时间解析连续弹道。
- 当前 GPU 模式不持久化 Burst、碰撞、Ribbon 或 GPU 事件回读。

## 6. `.meta` 与依赖

每个 Authoring 资产可以带同名 `.meta`：

- GUID 在重导入时保持稳定。
- `type` 决定 AssetRegistry 分类。
- `source` 和 Import Settings 允许重导入。
- Dependencies 用于缺失引用诊断、流式依赖闭包和移动更新。

移动或重命名应通过 `AssetMaintenance` 或编辑器完成；直接移动文件可能留下失效文本引用。

## 7. 仓库资产策略

- Git 跟踪引擎运行必需的场景、材质、Shader、输入配置、环境资源及其 `.meta`。
- `assets/Imported/` 是编辑器在本机生成的导入结果，可能包含大量 Mesh、材质和缩略图，因此整个目录不进入主仓库。
- Bugatti 属于可选的大型手动测试样例，仍可保留在本机 `assets/Imported/bugatti/` 使用，但 Git 不再跟踪或分发它。
- 主仓库只保留 `tests/fixtures/Import/triangle.obj` 这一份极小源模型，用于 Assimp 同步与异步导入回归测试。
- 若将来需要共享 FBX、glTF 等大型兼容性样例，应放入独立测试资产包并使用 Git LFS，同时记录来源和分发许可。

## 8. Cook 与发布

平台 Cook 将 Mesh/Texture 转换为 `TNYMSH2`/`TNYTEX1`，展开 Shader Variant，并生成版本化平台清单和 `Game.tpak`。普通 ProjectPackager 会过滤 `.meta` 与 FBX/OBJ/glTF 等 Authoring 文件。

所有归档路径必须是规范化项目相对路径，不能为绝对路径或包含 `..`。读取器对实体数、骨骼数、字符串、帧数、包长和 Chunk 大小设置上限，外部数据校验失败时返回失败而不是部分接受。

## 9. 格式演进原则

- 只追加字段时优先提升版本并保留旧 Reader。
- 改变既有字段语义时必须提供显式迁移，不能静默复用旧版本号。
- 二进制格式固定小端，并对 Count/Length 先检查上限再分配。
- 文本格式允许 Git Diff，但解析器应拒绝未知结构 Token，避免错误文件被误认为有效资产。
- 新格式必须至少有 Save/Load Round-trip、损坏 Header、越界 Count 和缺失依赖测试。
