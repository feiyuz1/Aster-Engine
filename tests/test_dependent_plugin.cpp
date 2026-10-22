#include "tinyengine/plugin/plugin_api.h"
extern "C" __declspec(dllimport) int tinyDependencyValue();
namespace
{
int TINY_PLUGIN_CALL Startup(const TinyPluginHostAPI*)
{
    return tinyDependencyValue() == 42;
}
void TINY_PLUGIN_CALL shutdown() {}
const TinyPluginAPI api{
    TINY_PLUGIN_ABI_VERSION, sizeof(TinyPluginAPI), "AppDirectoryDependencyPlugin", 1, &Startup, &shutdown, nullptr};
} // namespace
extern "C" TINY_PLUGIN_EXPORT const TinyPluginAPI* TINY_PLUGIN_CALL TinyPlugin_GetAPI(uint32_t abi)
{
    return abi == TINY_PLUGIN_ABI_VERSION ? &api : nullptr;
}
