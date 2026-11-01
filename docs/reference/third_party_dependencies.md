# 第三方依赖与许可

Aster-Engine 使用下列第三方项目。依赖由本地源码树、系统包或 CMake `FetchContent` 提供，不会被直接复制到本仓库。

| 项目 | 许可证 | 用途 |
| --- | --- | --- |
| [SDL 2.32.x](https://www.libsdl.org/) | zlib License | 运行时窗口、输入和音频 |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT License | 编辑器与导入工具 |
| [Assimp](https://github.com/assimp/assimp) | BSD 3-Clause | 模型导入工具 |
| [stb / stb_vorbis](https://github.com/nothings/stb) | MIT License 或 Public Domain | 图像与运行时 OGG 解码 |
| [FreeType 2.13.3](https://freetype.org/) | FreeType License 或 GPLv2 | 全平台字体解析、度量与灰度栅格化 |
| [HarfBuzz 10.2.0](https://github.com/harfbuzz/harfbuzz) | MIT License | 全平台 OpenType 字形塑形、连字与字偶距 |
| [ICU4C 76.1](https://github.com/unicode-org/icu/releases/tag/release-76-1) | Unicode License V3 及包内第三方声明 | Unicode 字素/换行边界、脚本和双向排版 |
| [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | MIT License | 可选物理后端 |
| [Vulkan Headers / Loader](https://github.com/KhronosGroup/Vulkan-Loader) | 上游 Apache-2.0 / MIT 等声明 | 可选 Vulkan 1.3 后端的开发头文件与 loader 接口 |
| [glslang 16.4.0](https://github.com/KhronosGroup/glslang/tree/16.4.0) | 上游复合许可，见完整 `LICENSE.txt` | Vulkan GLSL → SPIR-V，静态源码构建 |
| [SPIRV-Reflect](https://github.com/KhronosGroup/SPIRV-Reflect/tree/vulkan-sdk-1.4.357.0) | Apache-2.0 | Vulkan SPIR-V descriptor 与 uniform 布局反射，静态源码构建 |

发布包必须保留所有已启用依赖的上游许可证文本。Assimp 和 Dear ImGui 仅用于编辑器或导入工具；SDL、stb_vorbis、FreeType、HarfBuzz 和 ICU 用于运行时；Jolt 仅在 `TINYENGINE_ENABLE_JOLT=ON` 时启用。采用 FreeType License 发布时须遵守 `FTL.TXT` 中的署名要求。

编辑器默认通过 FetchContent 获取固定的 Dear ImGui `v1.92.8-docking`（提交 `572f249ce1975f98ad9f8aabce512ffa12a52d6c`，归档 SHA-256 在 CMake 中校验）。也可将 `TINYENGINE_IMGUI_DIR` 指向 1.92.8 或更新的 docking 源码；普通 master 源码会自动回退到项目内固定版本，不修改公共源码树。

字体依赖固定归档由 `cmake/TextDependencies.cmake` 校验：

| 归档 | SHA-256 |
| --- | --- |
| FreeType `VER-2-13-3` GitHub zip | `a7165cb405dfce349ae64d6f59efb2567565ca8edbacbeff2a8d0e4cfa779cf5` |
| HarfBuzz `10.2.0` GitHub zip | `180cec309e817ec50953cd4d4208025c1931203231cf455418b0dd4021091951` |
| ICU `icu4c-76_1-Win64-MSVC2022.zip` | `bedba77dd1feca09e9ae9922109a285c0ecf46d09c80b65eae6eae63a4e155dc` |

默认 FreeType/HarfBuzz 构建静态库，FreeType 不反向依赖 HarfBuzz；ICU Windows 官方包使用动态库并包含 Unicode 数据 DLL。其他平台使用指定精确版本的 ICU 开发包。可用归档路径、FetchContent 源码目录、`ICU_ROOT` 或系统包覆盖进行离线构建，详见[构建指南](../guides/build_guide.md)。系统包版本由调用者的包管理器锁定，源码目录覆盖由调用者验证来源。

默认安装保留 FreeType `FTL.TXT`、HarfBuzz `COPYING` 与 ICU `LICENSE` 于 `share/tinyengine/licenses`；字体文件本身的授权独立于引擎和这些库，项目作者须随打包字体附带相应许可。

Vulkan 依赖仅在 `TINYENGINE_ENABLE_VULKAN=ON` 时参与引擎链接。glslang 固定为 16.4.0，归档 SHA-256 为 `2c6eb4a59cdff22eee2f5718686af700d863e40ff1f366a7103078ebdb0c64b4`；默认源码归档由 CMake 校验。SPIRV-Reflect 默认取自调用者的 `VULKAN_SDK/Source/SPIRV-Reflect`，本机参考 SDK 为 1.4.357.0，CI 固定同版本；自定义 SDK/源码覆盖由提供者锁定与验证。SDK 自带 glslang 预编译 C++ 库不用于引擎链接。

仓库保留 glslang 16.4.0 的[完整上游许可](licenses/glslang-LICENSE.txt)，以及 SPIRV-Reflect 的 [Apache-2.0 许可与版权声明](licenses/spirv-reflect-LICENSE.txt)。这些是运行时静态依赖，游戏与编辑器发布均须携带声明；不能只在开发机保留 SDK 许可目录。更换依赖版本时同时更新对应许可。GPU 驱动提供的系统 Vulkan loader 无需复制整个 SDK；若另行分发 loader，须附带所分发版本的上游声明。
