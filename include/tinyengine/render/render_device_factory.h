#pragma once
#include "tinyengine/render/renderer.h"
namespace tinyengine
{
std::unique_ptr<RenderDevice> createRenderDevice(RenderBackend backend, int width, int height,
                                                 const std::string& title);
}
