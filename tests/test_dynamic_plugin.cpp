// 文件：tests/test_dynamic_plugin.cpp
// 作用：构造测试动态插件，用于验证插件 ABI 与生命周期。
// 约定：测试必须可重复执行，不依赖执行顺序或开发机上的隐式状态。
// 类型：实现文件。

#include "tinyengine/plugin/plugin_api.h"

#include <cstdint>
#include <cstring>

namespace
{
const TinyPluginHostAPI* hostApi = nullptr;

int TINY_PLUGIN_CALL Startup(const TinyPluginHostAPI* host)
{
    hostApi = host;
    static constexpr char kMessage[] = "dynamic plugin started";
    if (hostApi && hostApi->log)
        hostApi->log(hostApi->host_context, TINY_PLUGIN_LOG_INFO, kMessage, std::strlen(kMessage));
    return hostApi && hostApi->abi_version == TINY_PLUGIN_ABI_VERSION;
}

void TINY_PLUGIN_CALL shutdown()
{
    hostApi = nullptr;
}

void TINY_PLUGIN_CALL Tick(double)
{
    static constexpr char kMessage[] = "dynamic plugin tick";
    if (hostApi && hostApi->log)
        hostApi->log(hostApi->host_context, TINY_PLUGIN_LOG_INFO, kMessage, sizeof(kMessage) - 1);
}

const TinyPluginAPI api{
    TINY_PLUGIN_ABI_VERSION, sizeof(TinyPluginAPI), "TinyEngineDynamicRegressionPlugin", 1, &Startup, &shutdown, &Tick};
} // namespace

extern "C" TINY_PLUGIN_EXPORT const TinyPluginAPI* TINY_PLUGIN_CALL TinyPlugin_GetAPI(const std::uint32_t requestedAbi)
{
    return requestedAbi == TINY_PLUGIN_ABI_VERSION ? &api : nullptr;
}
