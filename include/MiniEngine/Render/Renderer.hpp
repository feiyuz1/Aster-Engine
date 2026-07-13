#pragma once

#include "MiniEngine/Types.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace MiniEngine
{
    class Logger;
    class World;

    class RenderContext
    {
    };

    class RenderPass
    {
    };

    struct InputState
    {
        float moveX = 0.0f;
        float moveY = 0.0f;
        float moveZ = 0.0f;
        float rotateX = 0.0f;
        float rotateY = 0.0f;
        float rotateZ = 0.0f;
        float scale = 0.0f;
        bool sprint = false;
        bool quitRequested = false;
    };

    struct RenderItem
    {
        Entity entity = InvalidEntity;
        float distanceToCamera = 0.0f;
        float worldX = 0.0f;
        float worldY = 0.0f;
        float worldZ = 0.0f;
        float rotationX = 0.0f;
        float rotationY = 0.0f;
        float rotationZ = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float scaleZ = 1.0f;
        std::string meshPath;
    };

    class RenderDevice
    {
    public:
        virtual ~RenderDevice() = default;
        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;
        virtual bool PumpEvents() = 0;
        virtual const InputState& GetInputState() const = 0;
        virtual void BeginFrame() = 0;
        virtual void Draw(const RenderItem& item) = 0;
        virtual void EndFrame() = 0;
        virtual void Present() = 0;
    };

    class NullRenderDevice final : public RenderDevice
    {
    public:
        bool Initialize() override;
        void Shutdown() override;
        bool PumpEvents() override;
        const InputState& GetInputState() const override;
        void BeginFrame() override;
        void Draw(const RenderItem& item) override;
        void EndFrame() override;
        void Present() override;

    private:
        InputState m_inputState;
    };

    class RenderQueue
    {
    public:
        void AddItem(const RenderItem& item);
        void Sort();
        void Clear();
        const std::vector<RenderItem>& GetItems() const;

    private:
        std::vector<RenderItem> m_items;
    };

    class ShaderManager
    {
    public:
        void Initialize();
        void Shutdown();
    };

    class MaterialSystem
    {
    public:
        void Initialize();
        void Shutdown();
    };

    class Renderer
    {
    public:
        void SetLogger(Logger* logger);
        bool Initialize(std::unique_ptr<RenderDevice> device);
        void Shutdown();

        bool PumpEvents();
        const InputState& GetInputState() const;
        void BuildRenderQueue(World& world);
        void RenderFrame(World& world);
        std::size_t GetLastDrawCallCount() const;

    private:
        Logger* m_logger = nullptr;
        std::unique_ptr<RenderDevice> m_device;
        RenderQueue m_renderQueue;
        ShaderManager m_shaderManager;
        MaterialSystem m_materialSystem;
        std::size_t m_lastDrawCalls = 0;
    };
}
