# Aster-Engine 原生插件 ABI

Aster-Engine 0.27 提供 ABI v1 的稳定 C 接口，适合将原生 Gameplay、工具桥接或第三方库封装为 DLL/Shared Object。它不是 C++ 模块 ABI，也不会跨库传递 STL、异常、RTTI 或引擎内部类。

项目自己的 C++ 开发请参见[项目代码 SDK（P0）](../guides/project_code_development.md)。它使用独立的 `TinyProject_GetAPI` 入口，负责项目身份、Play 会话和场景服务。这里的通用 `TinyPlugin_GetAPI` 继续用于通用插件；仅加载通用插件不会自动接通项目组件或 Play。每个通用插件的 HostAPI 在 startup 之后仍保持地址稳定，直到 shutdown 完成。

## 1. ABI 入口

插件必须导出：

```c
TINY_PLUGIN_EXPORT const TinyPluginAPI *TINY_PLUGIN_CALL
TinyPlugin_GetAPI(uint32_t requested_abi);
```

当 `requested_abi` 不受支持时返回空指针。返回结构必须满足：

- `abi_version == TINY_PLUGIN_ABI_VERSION`。
- `struct_size >= sizeof(TinyPluginAPI)`。
- `name`、`startup` 和 `shutdown` 非空。
- 名称在当前 PluginManager 中唯一。

## 2. 最小插件示例

```cpp
#include "tinyengine/plugin/plugin_api.h"

#include <cstring>

namespace
{
const TinyPluginHostAPI *g_host = nullptr;

int TINY_PLUGIN_CALL Startup(const TinyPluginHostAPI *host)
{
    if (!host || host->abi_version != TINY_PLUGIN_ABI_VERSION ||
        host->struct_size < sizeof(TinyPluginHostAPI))
        return 0;
    g_host = host;
    const char message[] = "ExamplePlugin started";
    host->log(host->host_context, TINY_PLUGIN_LOG_INFO, message, sizeof(message) - 1);
    return 1;
}

void TINY_PLUGIN_CALL Shutdown()
{
    g_host = nullptr;
}

void TINY_PLUGIN_CALL Tick(double delta_seconds)
{
    (void)delta_seconds;
}

const TinyPluginAPI Api{
    TINY_PLUGIN_ABI_VERSION,
    sizeof(TinyPluginAPI),
    "ExamplePlugin",
    1,
    &Startup,
    &Shutdown,
    &Tick,
};
} // namespace

extern "C" TINY_PLUGIN_EXPORT const TinyPluginAPI *TINY_PLUGIN_CALL
TinyPlugin_GetAPI(uint32_t requested_abi)
{
    return requested_abi == TINY_PLUGIN_ABI_VERSION ? &Api : nullptr;
}
```

## 3. 加载与生命周期

```cpp
auto &plugins = engine.GetPluginManager();
if (!plugins.Load("Plugins/ExamplePlugin.dll"))
    logger.Error(plugins.GetLastError());
```

生命周期顺序：

```text
Load library
→ locate TinyPlugin_GetAPI
→ ABI/size/name validation
→ startup(host)
→ per-frame tick(delta)
→ shutdown()
→ unload library
```

EngineContext 暴露 `PluginManager*`。Engine 在核心服务就绪后驱动 Tick，并在其他服务销毁前 `UnloadAll`。多个插件按加载顺序 Tick、按逆序 Shutdown。

`RegisterStatic` 可用于静态链接平台或自动化测试，它执行与动态插件相同的校验和生命周期，但不会卸载动态库句柄。

## 4. 内存和字符串规则

- 插件不能在一侧 `new`、另一侧 `delete`。
- 需要跨边界的内存必须使用 Host `allocate/deallocate`，并传回相同 Alignment。
- 字符串以 `pointer + length` 传递，不要求 NUL 结尾。
- Host 回调只在调用期间保证输入指针有效，插件需要长期保存时必须复制。
- ABI 回调不能让 C++ 异常跨边界传播。
- 插件卸载后不能保留 Host 或引擎回调中的悬空函数指针。

## 5. 版本演进

ABI v1 当前只包含日志、对齐分配、Startup/Shutdown/Tick。新增能力时应：

1. 在结构尾部追加函数指针。
2. 调用前同时检查 `abi_version` 和 `struct_size`。
3. 旧字段语义保持不变。
4. 发生破坏性变化时提升 ABI Version，并允许插件拒绝未知版本。

组件注册、反射、渲染扩展和编辑器面板尚未进入 ABI v1，不能向插件暴露私有 C++ 类型来绕过这一限制。
