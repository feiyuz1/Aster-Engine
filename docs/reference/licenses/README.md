# Vulkan 第三方声明来源

- `glslang-LICENSE.txt`：从 glslang 16.4.0 源码根目录 `LICENSE.txt` 原样复制，保留其完整复合许可。上游：[固定版本许可](https://github.com/KhronosGroup/glslang/blob/16.4.0/LICENSE.txt)。
- `spirv-reflect-LICENSE.txt`：保留 SDK 1.4.357.0 的 `spirv_reflect.c` 版权声明、完整 Apache-2.0 文本，以及 SDK 随附 `include/spirv/unified1/spirv.h` 的 Khronos MIT 声明。上游：[固定 SDK 标签](https://github.com/KhronosGroup/SPIRV-Reflect/tree/vulkan-sdk-1.4.357.0)。SDK 的 `Source/SPIRV-Reflect` 目录未附完整 `LICENSE`，因此仓库保留此副本。

glslang 与 SPIRV-Reflect 在启用 Vulkan 时用于运行时静态链接，发布游戏和编辑器时应携带这些声明。本目录不是整个引擎依赖的完整许可清单，其他依赖见[第三方依赖与许可](../third_party_dependencies.md)。
