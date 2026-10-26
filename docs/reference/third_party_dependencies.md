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
