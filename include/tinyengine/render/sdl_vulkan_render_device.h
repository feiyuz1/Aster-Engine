#pragma once
#include "tinyengine/render/renderer.h"
#include <vulkan/vulkan.h>

namespace tinyengine
{
struct VulkanInterop
{
    VkInstance instance{};
    VkPhysicalDevice physicalDevice{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t queueFamily = 0;
    VkFormat colorFormat{};
    std::uint32_t imageCount = 0;
    VkCommandBuffer commandBuffer{};
    VkImageView colorView{};
    VkExtent2D extent{};
};
class SdlVulkanRenderDevice final : public RenderDevice
{
  public:
    SdlVulkanRenderDevice(int width = 1280, int height = 720, std::string title = "Aster-Engine Vulkan");
    ~SdlVulkanRenderDevice() override;
    bool initialize() override;
    void shutdown() override;
    bool pumpEvents() override;
    const InputState& GetInputState() const override;
    float getAspectRatio() const override;
    Vec2 getViewportSize() const override;
    bool supportsDeferredRendering() const override;
    void beginGBufferPass() override;
    void endGBufferPass() override;
    void executeDeferredLighting() override;
    void beginShadowPass(const FrameView&, std::size_t) override;
    void drawShadow(const RenderItem&) override;
    void drawShadowInstanced(const std::vector<RenderItem>&) override;
    void endShadowPass() override;
    void beginFrame(const FrameView&) override;
    void drawSkybox() override;
    void draw(const RenderItem&) override;
    void drawInstanced(const std::vector<RenderItem>&) override;
    void drawParticles(const std::vector<Particle>&) override;
    void drawGpuParticles(const std::vector<GpuParticleEmitter>&) override;
    void executePostProcess(PostProcessPass) override;
    void endFrame() override;
    void drawUi(const std::vector<UIDrawCommand>&) override;
    void present() override;
    bool validateMaterial(const CompiledMaterial&, std::vector<MaterialDiagnostic>&) override;
    std::size_t getResidentGpuBytes() const override;
    void setGpuMemoryBudget(std::size_t) override;
    void trimGpuResources() override;
    bool usesTemporalJitter() const override;
    void setRetroPostProcess(bool, float, float) override;
    bool isRetroPostProcessEnabled() const override;
    float getRetroBlockSize() const override;
    float getRetroColorLevels() const override;
    void setMouseCapture(bool) override;
    bool isMouseCaptured() const override;
    RenderBackend getBackend() const override;
    void* getNativeWindow() const;
    void setEventCallback(std::function<void(const void*)>);
    void setCloseRequestCallback(std::function<void()>);
    void setTextInputEnabled(bool);
    void setTextInputRect(const UIRect&) override;
    GlyphAtlasStats getGlyphAtlasStats() const override;
    void setEscapeQuits(bool);
    float getDpiScale() const;
    bool rumbleController(float, float, std::uint32_t);
    bool isControllerConnected() const;
    bool enableSceneRenderTarget(bool, int width = 960, int height = 540);
    bool resizeSceneRenderTarget(int, int);
    int getSceneTargetWidth() const;
    int getSceneTargetHeight() const;
    std::uintptr_t getSceneColorTexture() const;
    std::uintptr_t getUiTexture(const std::string&);
    void setExposure(float);
    float getExposure() const;
    void setFxaaEnabled(bool);
    bool isFxaaEnabled() const;
    void setBloomEnabled(bool);
    bool isBloomEnabled() const;
    void setGtaoEnabled(bool);
    bool isGtaoEnabled() const;
    void setSsaoEnabled(bool);
    bool isSsaoEnabled() const;
    void setTaaEnabled(bool);
    bool isTaaEnabled() const;
    float getLastGpuFrameMilliseconds() const;
    std::size_t getHzbLevelCount() const;
    bool isIndirectDrawSupported() const;
    bool isGpuDrivenCullingSupported() const;
    bool reloadShaders();
    bool validateShader(const std::string&);
    const std::string& GetLastShaderError() const;
    VulkanInterop getVulkanInterop() const;
    VkImageView getSceneImageView() const;
    VkImageView getUiImageView(const std::string&);
    std::uint64_t getSceneGeneration() const;
    std::uint32_t getValidationErrorCount() const;
    std::vector<Vec4> readHdrAttachment(std::size_t attachment = 0);
    std::vector<std::uint8_t> readSceneColorPixels();
    std::vector<float> readDepthPixels();
    std::size_t getCachedProgramCountForTests() const;
    float readShadowDepthForTests(int x, int y, std::size_t cascade = 0);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace tinyengine
