# TinyEngine 构建指南

本文对应 TinyEngine 0.27.0。

## 1. 前置条件

Windows 推荐环境：

- Visual Studio 2022，安装“使用 C++ 的桌面开发”和 Windows SDK。
- CMake 3.20 或更高版本。
- 支持 OpenGL 4.3 Core、Compute Shader 和 Shader Storage Buffer 的显卡驱动。
- Assimp 源码目录，默认 `D:/OpenSourceProject/assimp-master`。
- Dear ImGui 源码目录，默认 `D:/OpenSourceProject/imgui-master`。

依赖处理规则：

- SDL2：优先使用系统 CMake Package；没有时由 CMake FetchContent 获取 2.32.10。
- stb_vorbis：优先使用 `TINYENGINE_STB_DIR`；没有时由 FetchContent 获取 stb。
- Assimp：Editor/Importer 构建必须提供源码目录，不自动下载。
- Dear ImGui：Editor 构建必须提供源码目录，不自动下载。
- FreeType：非 Windows 构建必须能被 `find_package(Freetype)` 找到。
- Jolt：默认关闭；启用时必须提供 `TINYENGINE_JOLT_DIR`。

首次配置可能需要访问网络下载 SDL2 或 stb。需要完全离线构建时，请预先安装 SDL2，并把 `TINYENGINE_STB_DIR` 指向包含 `stb_vorbis.c` 的本地目录。

## 2. 完整开发构建

```powershell
cmake -S . -B build `
  -DTINYENGINE_BUILD_EDITOR=ON `
  -DTINYENGINE_BUILD_IMPORTERS=ON `
  -DTINYENGINE_BUILD_TESTS=ON `
  -DTINYENGINE_ASSIMP_DIR=D:/OpenSourceProject/assimp-master `
  -DTINYENGINE_IMGUI_DIR=D:/OpenSourceProject/imgui-master

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
| `TINYENGINE_BUILD_TESTS` | ON | 构建核心测试；启用 Importer 时同时构建真实模型兼容测试 |
| `TINYENGINE_BUILD_DEMO` | ON | 构建开发 Demo |
| `TINYENGINE_BUILD_GAME` | ON | 构建独立 Shipping Runtime 和 `package_game` |
| `TINYENGINE_BUILD_EDITOR` | ON | 构建 Dear ImGui 编辑器 |
| `TINYENGINE_BUILD_IMPORTERS` | ON | 构建 Assimp 导入库和模型导入命令 |
| `TINYENGINE_ENABLE_JOLT` | OFF | 链接可选 Jolt 后端 |
| `TINYENGINE_ASSIMP_DIR` | 本机默认路径 | Assimp 源码根目录 |
| `TINYENGINE_IMGUI_DIR` | 本机默认路径 | Dear ImGui 源码根目录 |
| `TINYENGINE_JOLT_DIR` | 本机默认路径 | Jolt Physics 源码根目录 |
| `TINYENGINE_STB_DIR` | 本机默认路径 | 包含 `stb_vorbis.c` 的目录 |

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
| `tinyengine` | C++17 静态核心库 |
| `tinyengine_demo` | 开发场景运行入口；第一个参数可限制帧数 |
| `tinyengine_game` | 发布包使用的独立运行时 |
| `tinyengine_editor` | Dear ImGui 场景与资产编辑器 |
| `tinyengine_asset_tool` | 导入、扫描、Cook、TPAK、环境烘焙和资产维护 |
| `tinyengine_scene_tool` | 无界面场景查看与基础修改 |
| `tinyengine_package_tool` | 生成独立游戏目录 |
| `tinyengine_commandlet` | 资产/场景校验、迁移和平台 Cook |
| `tinyengine_tests` | 核心自动化测试 |
| `tinyengine_import_compatibility_tests` | 真实 FBX/glTF 导入矩阵 |
| `tinyengine_extended_tests` | 渲染、资产、物理、音频、输入、UI、导航、联网、插件、打包与压力专项回归 |

## 5. 测试与冒烟

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

只运行核心测试：

```powershell
ctest --test-dir build -C Debug -R tinyengine_core --output-on-failure
```

只运行真实模型兼容测试：

```powershell
ctest --test-dir build -C Debug -R tinyengine_import_compatibility --output-on-failure
```

只运行扩展专项回归：

```powershell
ctest --test-dir build -C Debug -R tinyengine_extended --output-on-failure
```

Windows 编辑器鼠标回归：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/EditorUiAdvancedRegression.ps1 -Scenario GizmoMarquee
powershell -NoProfile -ExecutionPolicy Bypass -File tests/EditorUiAdvancedRegression.ps1 -Scenario ShaderMaterial
```

扩展测试会生成 `Saved/ExtendedRegression/REPORT.md`。测试构建通过 `TINYENGINE_TEST_HOOKS` 暴露少量确定性观测接口；这些接口均有 `TEST HOOK` 标注，关闭 `TINYENGINE_BUILD_TESTS` 后不会编译。

运行三帧 OpenGL 冒烟：

```powershell
.\build\Debug\tinyengine_demo.exe 3
```

兼容测试会读取 Assimp 自带的 `test/models`，并把本机报告写入 `Saved/ImportCompatibility/REPORT.md`。

## 6. 安装与 CPack

```powershell
cmake --install build --config Release --prefix Saved/Install
cpack --config build/CPackConfig.cmake -C Release
```

安装内容包括核心库、公开头文件、已启用的可执行目标、示例资产和项目文件。Importer 头文件不作为 Runtime SDK 安装。

## 7. 常见问题

- `Assimp source tree not found`：修正 `TINYENGINE_ASSIMP_DIR`，或关闭 `TINYENGINE_BUILD_IMPORTERS`。
- `Dear ImGui source tree not found`：修正 `TINYENGINE_IMGUI_DIR`，或关闭 `TINYENGINE_BUILD_EDITOR`。
- OpenGL 初始化失败：更新显卡驱动，并确认设备支持 OpenGL 4.3 Core；不支持 Compute Shader 的旧设备不在 0.27 默认后端范围内。
- Windows `.pdb/.ilk` 无法打开：停止重复的 MSBuild、测试或 Demo 进程后串行重建。
- 非 Windows 找不到 FreeType：安装开发包并确保 CMake 可以发现 `Freetype::Freetype`。
- 离线环境获取 SDL/stb 失败：改用系统 SDL2 Package，并设置本地 `TINYENGINE_STB_DIR`。
