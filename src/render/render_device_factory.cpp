#include "tinyengine/render/render_device_factory.h"
#include "tinyengine/render/sdl_gl_render_device.h"
#ifdef TINYENGINE_HAS_VULKAN
#include "tinyengine/render/sdl_vulkan_render_device.h"
#endif
#include <stdexcept>
namespace tinyengine
{
std::unique_ptr<RenderDevice> createRenderDevice(RenderBackend backend, int width, int height, const std::string& title)
{
    if (backend == RenderBackend::Null)
        return std::make_unique<NullRenderDevice>();
    if (backend == RenderBackend::OpenGL)
        return std::make_unique<SdlGlRenderDevice>(width, height, title);
#ifdef TINYENGINE_HAS_VULKAN
    if (backend == RenderBackend::Vulkan)
        return std::make_unique<SdlVulkanRenderDevice>(width, height, title);
#endif
    throw std::runtime_error("Vulkan backend was not built; configure with TINYENGINE_ENABLE_VULKAN=ON and Vulkan SDK");
}
} // namespace tinyengine
