#include "MiniEngine/Render/Renderer.hpp"

#include "MiniEngine/Core/Logger.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MiniEngine
{
    bool NullRenderDevice::Initialize() { return true; }
    void NullRenderDevice::Shutdown() {}
    bool NullRenderDevice::PumpEvents() { return true; }
    const InputState& NullRenderDevice::GetInputState() const { return m_inputState; }
    float NullRenderDevice::GetAspectRatio() const { return 16.0f / 9.0f; }
    void NullRenderDevice::BeginFrame(const FrameView&) {}
    void NullRenderDevice::Draw(const RenderItem&) {}
    void NullRenderDevice::EndFrame() {}
    void NullRenderDevice::Present() {}

    void RenderQueue::AddItem(const RenderItem& item) { m_items.push_back(item); }
    void RenderQueue::Sort()
    {
        std::sort(m_items.begin(), m_items.end(), [](const RenderItem& lhs, const RenderItem& rhs)
        {
            return lhs.distanceToCamera < rhs.distanceToCamera;
        });
    }
    void RenderQueue::Clear() { m_items.clear(); }
    const std::vector<RenderItem>& RenderQueue::GetItems() const { return m_items; }

    void Renderer::SetLogger(Logger* logger) { m_logger = logger; }
    void Renderer::SetResourceManager(ResourceManager* resources) { m_resources = resources; }

    bool Renderer::Initialize(std::unique_ptr<RenderDevice> device)
    {
        m_device = std::move(device);
        return m_device && m_device->Initialize();
    }

    void Renderer::Shutdown()
    {
        m_renderQueue.Clear();
        if (m_device) { m_device->Shutdown(); m_device.reset(); }
    }

    bool Renderer::PumpEvents() { return m_device && m_device->PumpEvents(); }
    const InputState& Renderer::GetInputState() const
    {
        static const InputState empty{};
        return m_device ? m_device->GetInputState() : empty;
    }

    void Renderer::BuildRenderQueue(World& world)
    {
        m_renderQueue.Clear();
        Vec3 cameraPosition{0.0f, 0.0f, 5.0f};
        Vec3 cameraForward{0.0f, 0.0f, -1.0f};
        CameraComponent camera{};
        world.GetComponentManager().ForEach<CameraComponent>([&](const Entity entity, CameraComponent& candidate)
        {
            if (!candidate.primary) return;
            camera = candidate;
            if (const auto* transform = world.GetComponent<TransformComponent>(entity))
            {
                cameraPosition = transform->position;
                const float pitch = DegreesToRadians(transform->rotation.x);
                const float yaw = DegreesToRadians(transform->rotation.y);
                cameraForward = Normalize({std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
            }
        });
        m_frameView.cameraPosition = cameraPosition;
        m_frameView.view = LookAt(cameraPosition, cameraPosition + cameraForward, {0.0f, 1.0f, 0.0f});
        m_frameView.projection = Perspective(camera.fov, m_device ? m_device->GetAspectRatio() : 16.0f / 9.0f, camera.nearPlane, camera.farPlane);
        world.GetComponentManager().ForEach<LightComponent>([&](Entity, LightComponent& light)
        {
            m_frameView.lightDirection = Normalize(light.direction);
            m_frameView.lightColor = light.color;
            m_frameView.lightIntensity = light.intensity;
        });

        world.GetComponentManager().ForEach<MeshComponent>([&](const Entity entity, MeshComponent& meshComponent)
        {
            if (!m_resources) return;
            auto mesh = m_resources->Load<MeshResource>(meshComponent.meshPath);
            if (!mesh) return;
            auto material = m_resources->Load<MaterialResource>(meshComponent.materialPath);
            std::shared_ptr<TextureResource> texture;
            if (material && !material->GetTexturePath().empty()) texture = m_resources->Load<TextureResource>(material->GetTexturePath());
            const auto* transform = world.GetComponent<TransformComponent>(entity);
            const Vec3 position = transform ? transform->position : Vec3{};
            m_renderQueue.AddItem({entity, Length(position - cameraPosition), world.GetWorldMatrix(entity), std::move(mesh), std::move(material), std::move(texture)});
        });
        m_renderQueue.Sort();
    }

    void Renderer::RenderFrame(World& world)
    {
        if (!m_device) return;
        BuildRenderQueue(world);
        m_device->BeginFrame(m_frameView);
        for (const auto& item : m_renderQueue.GetItems()) m_device->Draw(item);
        m_device->EndFrame();
        m_device->Present();
        m_lastDrawCalls = m_renderQueue.GetItems().size();
        if (m_logger) m_logger->Debug("RenderFrame draw calls: " + std::to_string(m_lastDrawCalls));
    }

    std::size_t Renderer::GetLastDrawCallCount() const { return m_lastDrawCalls; }
    const FrameView& Renderer::GetFrameView() const { return m_frameView; }
}
