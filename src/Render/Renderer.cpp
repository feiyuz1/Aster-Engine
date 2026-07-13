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
    void NullRenderDevice::BeginShadowPass(const FrameView&) {}
    void NullRenderDevice::DrawShadow(const RenderItem&) {}
    void NullRenderDevice::EndShadowPass() {}
    void NullRenderDevice::BeginFrame(const FrameView&) {}
    void NullRenderDevice::Draw(const RenderItem&) {}
    void NullRenderDevice::EndFrame() {}
    void NullRenderDevice::DrawUi(const std::vector<UIDrawCommand>&) {}
    void NullRenderDevice::Present() {}

    void RenderQueue::AddItem(const RenderItem& item) { m_items.push_back(item); }
    void RenderQueue::Sort()
    {
        std::sort(m_items.begin(), m_items.end(), [](const RenderItem& lhs, const RenderItem& rhs)
        {
            if (lhs.transparent != rhs.transparent) return !lhs.transparent;
            return lhs.transparent ? lhs.distanceToCamera > rhs.distanceToCamera : lhs.distanceToCamera < rhs.distanceToCamera;
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
        m_lastCulled = 0;
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
        if (m_cameraOverrideEnabled)
        {
            cameraPosition = m_cameraOverridePosition; camera.fov = m_cameraOverrideFov;
            const float pitch = DegreesToRadians(m_cameraOverrideRotation.x), yaw = DegreesToRadians(m_cameraOverrideRotation.y);
            cameraForward = Normalize({std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
        }
        m_frameView.cameraPosition = cameraPosition;
        m_frameView.view = LookAt(cameraPosition, cameraPosition + cameraForward, {0.0f, 1.0f, 0.0f});
        m_frameView.projection = Perspective(camera.fov, m_device ? m_device->GetAspectRatio() : 16.0f / 9.0f, camera.nearPlane, camera.farPlane);
        world.GetComponentManager().ForEach<LightComponent>([&](Entity, LightComponent& light)
        {
            m_frameView.lightDirection = Normalize(light.direction);
            m_frameView.lightColor = light.color;
            m_frameView.lightIntensity = light.intensity;
        });
        const Vec3 shadowFocus = cameraPosition + cameraForward * 10.0f;
        const Vec3 lightDirection = Normalize(m_frameView.lightDirection);
        const Vec3 shadowUp = std::abs(Dot(lightDirection, Vec3{0.0f, 1.0f, 0.0f})) > 0.95f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
        m_frameView.lightViewProjection = Orthographic(-24.0f, 24.0f, -24.0f, 24.0f, 0.1f, 80.0f) *
            LookAt(shadowFocus - lightDirection * 35.0f, shadowFocus, shadowUp);

        world.GetComponentManager().ForEach<MeshComponent>([&](const Entity entity, MeshComponent& meshComponent)
        {
            if (!m_resources) return;
            if (const auto* active = world.GetComponent<ActiveComponent>(entity); active && !active->active) return;
            auto mesh = m_resources->Load<MeshResource>(meshComponent.meshPath);
            if (!mesh) return;
            auto material = m_resources->Load<MaterialResource>(meshComponent.materialPath);
            std::shared_ptr<TextureResource> texture;
            if (material && !material->GetTexturePath().empty()) texture = m_resources->Load<TextureResource>(material->GetTexturePath());
            const auto* transform = world.GetComponent<TransformComponent>(entity);
            const Vec3 position = transform ? transform->position : Vec3{};
            const Vec3 offset = position - cameraPosition;
            const float distance = Length(offset);
            const float facing = distance > 0.001f ? Dot(offset / distance, cameraForward) : 1.0f;
            const float halfFov = DegreesToRadians(camera.fov * 0.65f);
            if (distance > camera.farPlane || (distance > camera.nearPlane && facing < std::cos(halfFov))) { ++m_lastCulled; return; }
            const bool transparent = material && material->IsTransparent();
            m_renderQueue.AddItem({entity, distance, world.GetWorldMatrix(entity), std::move(mesh), std::move(material), std::move(texture), transparent});
        });
        m_renderQueue.Sort();
    }

    void Renderer::RenderFrame(World& world)
    {
        if (!m_device) return;
        BuildRenderQueue(world);
        m_device->BeginShadowPass(m_frameView);
        for (const auto& item : m_renderQueue.GetItems()) if (!item.transparent) m_device->DrawShadow(item);
        m_device->EndShadowPass();
        m_device->BeginFrame(m_frameView);
        for (const auto& item : m_renderQueue.GetItems()) m_device->Draw(item);
        if (m_uiSystem) m_device->DrawUi(m_uiSystem->BuildDrawList());
        m_device->EndFrame();
        if (m_overlayCallback) m_overlayCallback();
        m_device->Present();
        m_lastDrawCalls = m_renderQueue.GetItems().size();
        if (m_logger) m_logger->Debug("RenderFrame draw calls: " + std::to_string(m_lastDrawCalls));
    }

    std::size_t Renderer::GetLastDrawCallCount() const { return m_lastDrawCalls; }
    const FrameView& Renderer::GetFrameView() const { return m_frameView; }
    RenderDevice* Renderer::GetDevice() { return m_device.get(); }
    void Renderer::SetOverlayCallback(std::function<void()> callback) { m_overlayCallback = std::move(callback); }
    void Renderer::SetUISystem(UISystem* uiSystem) { m_uiSystem = uiSystem; }
    std::size_t Renderer::GetLastCulledCount() const { return m_lastCulled; }
    void Renderer::SetCameraOverride(const bool enabled, const Vec3& position, const Vec3& rotation, const float fov)
    {
        m_cameraOverrideEnabled = enabled; m_cameraOverridePosition = position; m_cameraOverrideRotation = rotation; m_cameraOverrideFov = fov;
    }
}
