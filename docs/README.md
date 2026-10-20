# TinyEngine 文档

## 使用与开发

| 目标 | 文档 |
|---|---|
| 配置、构建、测试和安装引擎 | [构建指南](Guides/构建指南.md) |
| 编辑场景、组件和资源 | [使用指南](Guides/使用指南.md) |
| 创建、打开和打包项目 | [项目工作流](Guides/项目工作流.md) |
| 编译项目 C++、在 Play 中运行并调试 | [项目代码开发](Guides/项目代码开发.md) |
| 联网功能 | [联网指南](Guides/联网指南.md) |

## 格式与工具参考

- [命令行工具与项目右键菜单](Reference/工具参考.md)
- [资产与数据格式](Reference/资产与数据格式.md)
- [原生插件 API](Reference/原生插件API.md)
- [第三方依赖与许可](Reference/第三方依赖与许可.md)

## 架构与专题

- [总体设计](Architecture/设计.md)
- [项目架构](Architecture/项目架构.md)
- [源码阅读与注释规范](Architecture/源码阅读与注释规范.md)
- [材质系统](Architecture/材质系统.md)
- [材质系统验收](Reports/材质系统验收.md)

## 目录约定

`Guides` 放使用指南，`Reference` 放参数和格式，`Architecture` 放设计说明，`Reports` 放历史验收。`superpowers` 保留当时的设计与实施记录，其中的旧路径只代表记录当时的布局。旧的本地报告仍保留在本机，未自动纳入 Git。

引擎源码在 `src`，公开头文件在 `include`，编辑器在 `editor`。`Programs` 只放独立程序入口，`Scripts` 放构建、测试和维护脚本。游戏项目的 `Source`、`build` 目录约定保持不变。