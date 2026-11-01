#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace tinyengine
{
class RenderDevice;

// Owns the ImGui renderer and the backend-specific texture registrations.
class EditorGuiRenderer
{
  public:
    EditorGuiRenderer();
    ~EditorGuiRenderer();
    bool initialize(RenderDevice& device);
    void shutdown();
    void newFrame();
    void renderDrawData();
    std::uintptr_t sceneTexture();
    std::uintptr_t uiTexture(const std::string& path);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace tinyengine
