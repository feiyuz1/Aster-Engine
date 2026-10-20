#include "MiniEngine/Plugin/PluginAPI.h"
extern "C" __declspec(dllimport) int TinyDependencyValue();
namespace {
int TINY_PLUGIN_CALL Startup(const TinyPluginHostAPI *) { return TinyDependencyValue() == 42; }
void TINY_PLUGIN_CALL Shutdown() {}
const TinyPluginAPI api{TINY_PLUGIN_ABI_VERSION,sizeof(TinyPluginAPI),"AppDirectoryDependencyPlugin",1,&Startup,&Shutdown,nullptr};
}
extern "C" TINY_PLUGIN_EXPORT const TinyPluginAPI *TINY_PLUGIN_CALL TinyPlugin_GetAPI(uint32_t abi)
{ return abi==TINY_PLUGIN_ABI_VERSION ? &api : nullptr; }
