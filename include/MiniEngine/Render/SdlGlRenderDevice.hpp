#pragma once

#include "MiniEngine/Render/Renderer.hpp"

#include <functional>
#include <memory>
#include <string>

namespace MiniEngine
{
class SdlGlRenderDevice final : public RenderDevice
{
  public:
    SdlGlRenderDevice(int width = 1280, int height = 720, std::string title = "TinyEngine SDL2 + OpenGL");
    ~SdlGlRenderDevice() override;

    bool Initialize() override;
    void Shutdown() override;
    bool PumpEvents() override;
    const InputState &GetInputState() const override;
    float GetAspectRatio() const override;
    Vec2 GetViewportSize() const override;
    void BeginShadowPass(const FrameView &view, std::size_t cascadeIndex) override;
    void DrawShadow(const RenderItem &item) override;
    void DrawShadowInstanced(const std::vector<RenderItem> &items) override;
    void EndShadowPass() override;
    void BeginFrame(const FrameView &view) override;
    void Draw(const RenderItem &item) override;
    void DrawInstanced(const std::vector<RenderItem> &items) override;
    void DrawParticles(const std::vector<Particle> &particles) override;
    void DrawGpuParticles(const std::vector<GpuParticleEmitter> &emitters) override;
    void ExecutePostProcess(PostProcessPass pass) override;
    void EndFrame() override;
    void DrawUi(const std::vector<UIDrawCommand> &commands) override;
    void Present() override;
    std::size_t GetResidentGpuBytes() const override;
    void SetGpuMemoryBudget(std::size_t bytes) override;
    void TrimGpuResources() override;
    void *GetNativeWindow() const;
    void *GetGlContext() const;
    void SetEventCallback(std::function<void(const void *)> callback);
    bool EnableSceneRenderTarget(bool enabled, int width = 960, int height = 540);
    bool ResizeSceneRenderTarget(int width, int height);
    std::uintptr_t GetSceneColorTexture() const;
    std::uintptr_t GetUiTexture(const std::string &path);
    int GetSceneTargetWidth() const;
    int GetSceneTargetHeight() const;
    bool ReloadShaders();
    bool ValidateShader(const std::string &path);
    void SetExposure(float exposure);
    float GetExposure() const;
    const std::string &GetLastShaderError() const;
    void SetFxaaEnabled(bool enabled);
    bool IsFxaaEnabled() const;
    void SetBloomEnabled(bool enabled);
    bool IsBloomEnabled() const;
    void SetSsaoEnabled(bool enabled);
    bool IsSsaoEnabled() const;
    void SetGtaoEnabled(bool enabled);
    bool IsGtaoEnabled() const;
    void SetTaaEnabled(bool enabled);
    bool IsTaaEnabled() const;
    float GetLastGpuFrameMilliseconds() const;
    std::size_t GetHzbLevelCount() const;
    bool IsIndirectDrawSupported() const;
    bool IsGpuDrivenCullingSupported() const;
    bool RumbleController(float lowFrequency, float highFrequency, std::uint32_t durationMilliseconds);
    bool IsControllerConnected() const;

  private:
    struct Impl;

  private:
    std::unique_ptr<Impl> m_impl;
    int m_width;
    int m_height;
    std::string m_title;
    float m_aspect = 16.0f / 9.0f;
    InputState m_inputState;
    std::function<void(const void *)> m_eventCallback;
};
} // namespace MiniEngine
