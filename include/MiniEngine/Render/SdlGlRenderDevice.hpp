#pragma once

#include "MiniEngine/Render/Renderer.hpp"

#include <memory>
#include <functional>
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
        const InputState& GetInputState() const override;
        float GetAspectRatio() const override;
        void BeginFrame(const FrameView& view) override;
        void Draw(const RenderItem& item) override;
        void EndFrame() override;
        void DrawUi(const std::vector<UIDrawCommand>& commands) override;
        void Present() override;
        void* GetNativeWindow() const;
        void* GetGlContext() const;
        void SetEventCallback(std::function<void(const void*)> callback);
        bool EnableSceneRenderTarget(bool enabled, int width = 960, int height = 540);
        bool ResizeSceneRenderTarget(int width, int height);
        std::uintptr_t GetSceneColorTexture() const;
        int GetSceneTargetWidth() const;
        int GetSceneTargetHeight() const;

    private:
        struct Impl;

    private:
        std::unique_ptr<Impl> m_impl;
        int m_width;
        int m_height;
        std::string m_title;
        float m_aspect = 16.0f / 9.0f;
        InputState m_inputState;
        std::function<void(const void*)> m_eventCallback;
    };
}
