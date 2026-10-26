# Aster-Engine 构建指南

## 推荐构建入口

Windows 安装 VS 2022 的 C++ 桌面开发和 CMake 后，在引擎根目录执行：

```powershell
cmake --preset editor
cmake --build --preset editor-debug
ctest --preset editor-debug
.\intermediate\Build\Editor\Debug\tinyengine_editor.exe
```

引擎的构建文件位于 `intermediate/Build/Editor`，下载依赖位于 `intermediate/Dependencies`。`core` / `core-release` 用于不带编辑器的构建。Presets 默认关闭可选 Assimp 导入器；需要模型导入时，配置时追加 `-DTINYENGINE_BUILD_IMPORTERS=ON -DTINYENGINE_ASSIMP_DIR=<本机源码目录>`。

本机离线依赖通过配置参数，或未跟踪的 `CMakeUserPresets.json` 指定。例如给 SDL 指定 `FETCHCONTENT_SOURCE_DIR_SDL2`，给 ImGui 指定 `TINYENGINE_IMGUI_DIR`，给 stb 指定 `TINYENGINE_STB_DIR`。不要把本机磁盘路径写进共享 CMake 文件。

旧的手动 `cmake -S . -B ...` 入口仍然有效。历史缓存不能直接改名后继续使用；先完成新目录构建，再运行 `scripts/maintenance/organize_repository.ps1` 预览，确认后加 `-Apply` 归档。脚本保留仍在使用或被其他工作树引用的缓存，并为每次移动保存 `saved/RepositoryMaintenance/*.json`。回退使用 `-RollbackManifest <清单路径>`。游戏项目继续使用自己的 `build/`，本次只整理引擎仓库。

本文对应 Aster-Engine 0.27.0。

## 1. 前置条件

Windows 推荐环境：

- Visual Studio 2022，安装“使用 C++ 的桌面开发”和 Windows SDK。
- CMake 3.21 或更高版本。
- 支持 OpenGL 4.3 Core、Compute Shader 和 Shader Storage Buffer 的显卡驱动。
- 启用模型导入时提供 Assimp 源码目录，例如 `D:/Dependencies/assimp`。
- 编辑器默认自动获取固定的 Dear ImGui `v1.92.8-docking`；离线构建可提供本地 docking 源码目录。

依赖处理规则：

- SDL2：优先使用系统 CMake Package；没有时由 CMake FetchContent 获取 2.32.10。
- stb_vorbis：优先使用 `TINYENGINE_STB_DIR`；没有时由 FetchContent 获取 stb。
- Assimp：Editor/Importer 构建必须提供源码目录，不自动下载。
- Dear ImGui：Editor 默认通过 FetchContent 获取固定提交的 docking 版本并校验 SHA-256。`TINYENGINE_IMGUI_DIR` 可指定 1.92.8 或更新的 docking 源码；普通 master 或旧版本会回退到项目内固定版本。
- 字体系统：全部平台使用 FreeType 2.13.3、HarfBuzz 10.2.0、ICU 76.1。默认自动获取并校验固定 SHA-256 的 FreeType/HarfBuzz 源码，静态构建；FreeType 关闭 HarfBuzz 依赖，HarfBuzz 启用 FreeType 接口。
- Windows x64/MSVC 默认获取 ICU 76.1 官方 MSVC 2022 二进制包。Linux、macOS、Windows ARM64/其他工具链须预装 ICU 76.1 开发包，通过 `ICU_ROOT` 或 `CMAKE_PREFIX_PATH` 指定。ICU 只使用 C API；官方 Release DLL 也用于 Debug 引擎。
- Jolt：默认关闭；启用时必须提供 `TINYENGINE_JOLT_DIR`。

首次配置可能需要访问网络下载 SDL2、stb、Dear ImGui 和字体依赖。完全离线构建时，请预先安装 SDL2，将 `TINYENGINE_STB_DIR` 指向包含 `stb_vorbis.c` 的本地目录，并将 `TINYENGINE_IMGUI_DIR` 指向本地 docking 源码。

字体依赖可用本地原版归档（仍验证同一 SHA-256）：

```powershell
cmake --preset editor `
  -DTINYENGINE_FREETYPE_ARCHIVE=<归档目录>/freetype-2.13.3.zip `
  -DTINYENGINE_HARFBUZZ_ARCHIVE=<归档目录>/harfbuzz-10.2.0.zip `
  -DTINYENGINE_ICU_ARCHIVE=<归档目录>/icu4c-76_1-Win64-MSVC2022.zip
```

也可通过 `FETCHCONTENT_SOURCE_DIR_TINYENGINE_FREETYPE`、`FETCHCONTENT_SOURCE_DIR_TINYENGINE_HARFBUZZ`、`FETCHCONTENT_SOURCE_DIR_TINYENGINE_ICU` 指定解压源码/ICU 包。解压目录覆盖不再校验归档，版本和内容由提供者负责。ICU 安装包优先用 `-DICU_ROOT=<包根目录>`，自定义版本可设 `-DTINYENGINE_ICU_VERSION=<精确版本>`。需要使用已安装的 FreeType/HarfBuzz 时设 `-DTINYENGINE_TEXT_USE_SYSTEM_LIBS=ON`（至少 2.13.3/10.2.0，HarfBuzz 必须启用 FreeType）；这一路径的版本由包管理器锁定。所有本机路径只放在命令行或未跟踪的 `CMakeUserPresets.json`。

## 2. 完整开发构建

```powershell
cmake -S . -B build `
  -DTINYENGINE_BUILD_EDITOR=ON `
  -DTINYENGINE_BUILD_IMPORTERS=ON `
  -DTINYENGINE_ASSIMP_DIR=D:/OpenSourceProject/assimp-master

cmake --build build --config Debug -j 8
```

Release 构建：

```powershell
cmake --build build --config Release -j 8
```

不要同时启动多个针对同一 Visual Studio Build Tree 的构建进程，否则 `.pdb` 或 `.ilk` 可能发生锁冲突。

## 3. 构建选项

| 选项 | 默认值 | 作用 |
|---|---:|---|
| `TINYENGINE_BUILD_TESTS` | OFF | 本地有测试源码时可手动启用 |
| `TINYENGINE_BUILD_DEMO` | ON | 构建开发 Demo |
| `TINYENGINE_BUILD_GAME` | ON | 构建独立 Shipping Runtime 和 `package_game` |
| `TINYENGINE_BUILD_EDITOR` | ON | 构建 Dear ImGui 编辑器 |
| `TINYENGINE_BUILD_IMPORTERS` | ON | 构建 Assimp 导入库和模型导入命令 |
| `TINYENGINE_ENABLE_JOLT` | OFF | 链接可选 Jolt 后端 |
| `TINYENGINE_ASSIMP_DIR` | 空，启用时显式指定 | Assimp 源码根目录 |
| `TINYENGINE_IMGUI_DIR` | 空（自动获取） | 可选 Dear ImGui docking 源码根目录 |
| `TINYENGINE_JOLT_DIR` | 空，启用时显式指定 | Jolt Physics 源码根目录 |
| `TINYENGINE_STB_DIR` | 空，自动获取 | 包含 `stb_vorbis.c` 的目录 |
| `TINYENGINE_TEXT_USE_SYSTEM_LIBS` | OFF | 改用已安装的 FreeType/HarfBuzz 开发包 |
| `TINYENGINE_ICU_VERSION` | 76.1 | `find_package(ICU)` 要求的精确版本 |
| `TINYENGINE_ICU_STATIC` | OFF | 使用调用者提供的静态 ICU 包并定义 `U_STATIC_IMPLEMENTATION` |
| `ICU_ROOT` | 空 | ICU 开发包根目录；Windows x64/MSVC 未指定时获取固定包 |
| `TINYENGINE_FREETYPE_ARCHIVE` / `TINYENGINE_HARFBUZZ_ARCHIVE` / `TINYENGINE_ICU_ARCHIVE` | 固定上游 URL | 原版归档 URL 或本地文件 |
| `TINYENGINE_TEXT_RUNTIME_FILES` | 空，Windows ICU 自动发现 | 额外字体共享库文件列表；分号分隔，构建/安装时一起复制 |

只构建最小 Runtime 时可以关闭 Editor 和 Importer：

```powershell
cmake -S . -B build-runtime `
  -DTINYENGINE_BUILD_EDITOR=OFF `
  -DTINYENGINE_BUILD_IMPORTERS=OFF
cmake --build build-runtime --config Release -j 8
```

## 4. 主要目标

| 目标 | 产物/用途 |
|---|---|
| `tinyengine` | C++20 静态核心库 |
| `tinyengine_demo` | 开发场景运行入口；第一个参数可限制帧数 |
| `tinyengine_game` | 发布包使用的独立运行时；构建后同时生成兼容性 sidecar `.buildinfo` |
| `tinyengine_editor` | Dear ImGui 场景与资产编辑器 |
| `tinyengine_asset_tool` | 导入、扫描、Cook、TPAK、环境烘焙和资产维护 |
| `tinyengine_scene_tool` | 无界面场景查看与基础修改 |
| `tinyengine_package_tool` | 生成独立游戏目录 |
| `tinyengine_commandlet` | 资产/场景校验、迁移和平台 Cook |

`tinyengine_game` 的 POST_BUILD 步骤会在 EXE 旁写入 `.buildinfo`，包含 Engine 版本、ABI、场景格式、构建 ID、配置和编译器。`tinyengine_package_tool` 必须同时读到 Game EXE 与该 sidecar，且所有字段与当前工具匹配；旧二进制、缺失 sidecar 或跨配置/工具链混用都会在打包前被拒绝。不要手工修改 `.buildinfo`，应重新构建对应配置。

## 5. 运行冒烟

运行三帧 OpenGL 冒烟：

```powershell
.\build\Debug\tinyengine_demo.exe 3
```

也可使用 Commandlet 校验基础素材：

```powershell
.\build\Debug\tinyengine_commandlet.exe ValidateAssets assets
```

## 6. 安装与 CPack

```powershell
cmake --install build --config Release --prefix saved/Install
cpack --config build/CPackConfig.cmake -C Release
```

安装内容包括核心库、公开头文件、已启用的可执行目标、示例资产和项目文件。Importer 头文件不作为 Runtime SDK 安装。

默认 Windows 字体依赖中，FreeType/HarfBuzz 静态链接；`icuuc76.dll`、`icuin76.dll`、`icudt76.dll` 会复制到所有工具、游戏、编辑器和测试 EXE 旁，并进入 `ProjectRuntime` / `EditorRuntime` 安装组件的 `bin/<配置>`。ICU 数据位于 `icudt76.dll`，无需额外 `.dat` 文件。使用自定义共享依赖时，CMake 能建模的 Windows DLL 自动随构建暂存；额外 DLL 用 `TINYENGINE_TEXT_RUNTIME_FILES` 补充。其他平台应使用系统包或部署相应共享库，并配置运行时查找路径。

默认字体库许可也暂存于 EXE 旁（`freetype-LICENSE.txt`、`harfbuzz-LICENSE.txt`、`icu-LICENSE.txt`），项目打包时与依赖文件一并复制。静态 ICU 设置 `TINYENGINE_ICU_STATIC=ON` 并提供相应开发包；此时不获取 Windows 动态包。

直接链接安装的 `tinyengine` 静态归档的使用者仍须链接匹配的 FreeType、HarfBuzz、ICU `uc/i18n/data`；Project SDK 的游戏模块通过宿主 ABI 使用引擎，无需引入字体第三方头文件。默认字体依赖许可证安装到 `share/tinyengine/licenses`；自定义包须同时分发其上游许可证。

## 7. 常见问题

- `Assimp source tree not found`：修正 `TINYENGINE_ASSIMP_DIR`，或关闭 `TINYENGINE_BUILD_IMPORTERS`。
- `Dear ImGui source tree not found`：修正 `TINYENGINE_IMGUI_DIR`，或关闭 `TINYENGINE_BUILD_EDITOR`。
- OpenGL 初始化失败：更新显卡驱动，并确认设备支持 OpenGL 4.3 Core；不支持 Compute Shader 的旧设备不在 0.27 默认后端范围内。
- Windows `.pdb/.ilk` 无法打开：停止重复的 MSBuild、测试或 Demo 进程后串行重建。
- Package Tool 报构建信息缺失或不兼容：确认 Game EXE 旁存在同名 `.buildinfo`，并用同一源码、配置和编译器重建 `tinyengine_game` 与 `tinyengine_package_tool`。
- 找不到 ICU：安装所要求精确版本的开发包并设置 `ICU_ROOT`；更换版本时显式设置 `TINYENGINE_ICU_VERSION`，勿混用不同版本头文件/库/DLL。
- Windows 程序找不到 ICU DLL：重建该 EXE 以触发暂存；发布时确保三个 ICU DLL 都位于 EXE 旁。
- 离线环境获取 SDL/stb 失败：改用系统 SDL2 Package，并设置本地 `TINYENGINE_STB_DIR`。
