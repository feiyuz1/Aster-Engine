# TinyEngine 命令行工具参考

下列示例使用 Debug 目录；Release 构建时替换为 `build/Release`。

## Asset Tool

```text
tinyengine_asset_tool import <source-model> <output-directory> [resource-prefix]
tinyengine_asset_tool bake-env <source-hdr> <output-directory>
tinyengine_asset_tool scan <asset-directory>
tinyengine_asset_tool cook <asset-directory> <cooked-directory> <output.tpak>
tinyengine_asset_tool pack <input-directory> <output.tpak>
tinyengine_asset_tool list <archive.tpak>
tinyengine_asset_tool thumbnail <asset> <output.ppm>
tinyengine_asset_tool chunk <asset> <output.chunkmanifest> [chunk-bytes]
tinyengine_asset_tool missing <asset-directory>
```

- `import` 只有在 `TINYENGINE_BUILD_IMPORTERS=ON` 时可用。
- `cook` 使用增量清单复制/删除产物并重建 TPAK。
- `missing` 无缺失时返回 0，发现缺失依赖时返回 3。
- 其余参数错误返回 1，执行失败通常返回 2。

## Scene Tool

```text
tinyengine_scene_tool <scene> list
tinyengine_scene_tool <scene> add <name>
tinyengine_scene_tool <scene> remove <index>
tinyengine_scene_tool <scene> transform <index> <px> <py> <pz> <rx> <ry> <rz> <sx> <sy> <sz>
```

`index` 来自同一次 `list` 输出的稳定排序，不是持久化 Entity ID。修改命令会用当前 `TINYSCENE` 版本重写场景。

## Commandlet

```text
tinyengine_commandlet help
tinyengine_commandlet ValidateAssets [asset-directory]
tinyengine_commandlet ValidateScenes [scene-directory]
tinyengine_commandlet MigrateScenes [scene-directory]
tinyengine_commandlet CookProject <project> <output> [platform] [configuration]
```

- `ValidateAssets` 检查 `.meta` 和依赖引用。
- `ValidateScenes` 无图形加载目录下所有 `.scene`。
- `MigrateScenes` 先生成 `.bak`，再用当前 Schema 重写。
- `CookProject` 支持 Windows/Linux Profile 与 Development/Shipping 配置。
- 成功返回 0；校验或执行失败返回 1；参数错误返回 2。

## Package Tool

```text
tinyengine_package_tool <project.tinyproject> <output-directory> [game-executable]
```

省略 `game-executable` 时，工具会从自身目录寻找 `tinyengine_game`。成功后输出可执行文件、项目文件、TPAK 路径、暂存文件数和归档字节数。

## Runtime 入口

```text
tinyengine_demo [maximum-frames]
tinyengine_game <project.tinyproject> [--frames=N]
tinyengine_editor [project.tinyproject]
```

- Demo 没有帧数参数时持续运行。
- Game 必须能找到项目文件；`--frames=N` 用于冒烟测试。
- Editor 省略项目时使用默认项目查找逻辑。

## CMake 辅助目标

```powershell
cmake --build build --config Release --target package_game
```

该目标依赖 `tinyengine_game` 与 `tinyengine_package_tool`，把仓库根目录的 `TinyEngine.tinyproject` 打包到 `build/Package/TinyEngineDemo`。
