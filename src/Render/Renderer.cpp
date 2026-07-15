#include "MiniEngine/Render/Renderer.hpp"

#include "MiniEngine/Core/Logger.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"
#include "MiniEngine/VFX/ParticleSystem.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace MiniEngine
{
bool NullRenderDevice::Initialize()
{
    return true;
}
void NullRenderDevice::Shutdown()
{
}
bool NullRenderDevice::PumpEvents()
{
    return true;
}
const InputState &NullRenderDevice::GetInputState() const
{
    return m_inputState;
}
float NullRenderDevice::GetAspectRatio() const
{
    return 16.0f / 9.0f;
}
Vec2 NullRenderDevice::GetViewportSize() const
{
    return {1280.0f, 720.0f};
}
void NullRenderDevice::BeginShadowPass(const FrameView &, const std::size_t)
{
}
void NullRenderDevice::DrawShadow(const RenderItem &)
{
}
void NullRenderDevice::DrawShadowInstanced(const std::vector<RenderItem> &)
{
}
void NullRenderDevice::EndShadowPass()
{
}
void NullRenderDevice::BeginFrame(const FrameView &)
{
}
void NullRenderDevice::Draw(const RenderItem &)
{
}
void NullRenderDevice::DrawInstanced(const std::vector<RenderItem> &)
{
}
void NullRenderDevice::DrawParticles(const std::vector<Particle> &)
{
}
void NullRenderDevice::EndFrame()
{
}
void NullRenderDevice::DrawUi(const std::vector<UIDrawCommand> &)
{
}
void NullRenderDevice::Present()
{
}
std::size_t NullRenderDevice::GetResidentGpuBytes() const
{
    return 0;
}
void NullRenderDevice::SetGpuMemoryBudget(const std::size_t bytes)
{
    m_gpuBudget = bytes;
}
void NullRenderDevice::TrimGpuResources()
{
}

void RenderQueue::AddItem(const RenderItem &item)
{
    m_items.push_back(item);
}
void RenderQueue::Sort()
{
    std::sort(m_items.begin(), m_items.end(), [](const RenderItem &lhs, const RenderItem &rhs) {
        if (lhs.transparent != rhs.transparent)
            return !lhs.transparent;
        return lhs.transparent ? lhs.distanceToCamera > rhs.distanceToCamera : lhs.distanceToCamera < rhs.distanceToCamera;
    });
}
void RenderQueue::Clear()
{
    m_items.clear();
}
const std::vector<RenderItem> &RenderQueue::GetItems() const
{
    return m_items;
}

void RenderQueue::ApplyConservativeOcclusion(const Vec3 &cameraPosition)
{
    struct Occluder
    {
        Vec3 direction;
        float distance = 0.0f;
        float radius = 0.0f;
    };
    std::vector<Occluder> occluders;
    std::vector<RenderItem> visible;
    visible.reserve(m_items.size());
    for (const auto &item : m_items)
    {
        if (item.transparent)
        {
            visible.push_back(item);
            continue;
        }
        const Vec3 center = (item.worldBounds.minimum + item.worldBounds.maximum) * 0.5f;
        const float radius = Length(item.worldBounds.maximum - item.worldBounds.minimum) * 0.5f;
        const Vec3 offset = center - cameraPosition;
        const float distance = Length(offset);
        const Vec3 direction = distance > 0.0001f ? offset / distance : Vec3{0.0f, 0.0f, -1.0f};
        const float angularRadius = distance > radius ? std::asin(std::clamp(radius / distance, 0.0f, 1.0f)) : 3.14159265f;
        bool hidden = false;
        for (const auto &occluder : occluders)
        {
            if (occluder.distance + occluder.radius >= distance - radius)
                continue;
            const float separation = std::acos(std::clamp(Dot(direction, occluder.direction), -1.0f, 1.0f));
            const float occluderAngle =
                occluder.distance > occluder.radius ? std::asin(std::clamp(occluder.radius / occluder.distance, 0.0f, 1.0f)) : 3.14159265f;
            if (separation + angularRadius < occluderAngle * 0.92f)
            {
                hidden = true;
                break;
            }
        }
        if (!hidden)
        {
            visible.push_back(item);
            if (radius > 0.5f && distance > radius)
                occluders.push_back({direction, distance, radius});
        }
    }
    m_items = std::move(visible);
}

namespace
{
Aabb TransformBounds(const Aabb &bounds, const Mat4 &matrix)
{
    const Vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
    const Vec3 extent = (bounds.maximum - bounds.minimum) * 0.5f;
    const Vec3 worldCenter{matrix.At(0, 0) * center.x + matrix.At(0, 1) * center.y + matrix.At(0, 2) * center.z + matrix.At(0, 3),
                           matrix.At(1, 0) * center.x + matrix.At(1, 1) * center.y + matrix.At(1, 2) * center.z + matrix.At(1, 3),
                           matrix.At(2, 0) * center.x + matrix.At(2, 1) * center.y + matrix.At(2, 2) * center.z + matrix.At(2, 3)};
    const Vec3 worldExtent{
        std::abs(matrix.At(0, 0)) * extent.x + std::abs(matrix.At(0, 1)) * extent.y + std::abs(matrix.At(0, 2)) * extent.z,
        std::abs(matrix.At(1, 0)) * extent.x + std::abs(matrix.At(1, 1)) * extent.y + std::abs(matrix.At(1, 2)) * extent.z,
        std::abs(matrix.At(2, 0)) * extent.x + std::abs(matrix.At(2, 1)) * extent.y + std::abs(matrix.At(2, 2)) * extent.z};
    return {worldCenter - worldExtent, worldCenter + worldExtent};
}

std::vector<std::vector<RenderItem>> BuildInstanceBatches(const std::vector<RenderItem> &items)
{
    std::vector<std::vector<RenderItem>> batches;
    std::unordered_map<std::string, std::size_t> lookup;
    for (const auto &item : items)
    {
        if (item.transparent || !item.skinMatrices.empty() || !item.mesh)
        {
            batches.push_back({item});
            continue;
        }
        const std::string key = item.mesh->GetPath() + "|" + (item.material ? item.material->GetPath() : std::string{});
        const auto found = lookup.find(key);
        if (found == lookup.end())
        {
            lookup[key] = batches.size();
            batches.push_back({item});
        }
        else
            batches[found->second].push_back(item);
    }
    return batches;
}
} // namespace

void Renderer::SetLogger(Logger *logger)
{
    m_logger = logger;
}
void Renderer::SetResourceManager(ResourceManager *resources)
{
    m_resources = resources;
}

bool Renderer::Initialize(std::unique_ptr<RenderDevice> device)
{
    m_device = std::move(device);
    return m_device && m_device->Initialize();
}

void Renderer::Shutdown()
{
    m_renderQueue.Clear();
    m_lastLodSwitches = 0;
    m_lastCulled = 0;
    if (m_device)
    {
        m_device->Shutdown();
        m_device.reset();
    }
}

bool Renderer::PumpEvents()
{
    return m_device && m_device->PumpEvents();
}
const InputState &Renderer::GetInputState() const
{
    static const InputState empty{};
    return m_device ? m_device->GetInputState() : empty;
}

void Renderer::BuildRenderQueue(World &world)
{
    m_renderQueue.Clear();
    m_lastCulled = 0;
    m_lastLodSwitches = 0;
    Vec3 cameraPosition{0.0f, 0.0f, 5.0f};
    Vec3 cameraForward{0.0f, 0.0f, -1.0f};
    CameraComponent camera{};
    world.GetComponentManager().ForEach<CameraComponent>([&](const Entity entity, CameraComponent &candidate) {
        if (!candidate.primary)
            return;
        camera = candidate;
        if (const auto *transform = world.GetComponent<TransformComponent>(entity))
        {
            cameraPosition = transform->position;
            const float pitch = DegreesToRadians(transform->rotation.x);
            const float yaw = DegreesToRadians(transform->rotation.y);
            cameraForward = Normalize({std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
        }
    });
    if (m_cameraOverrideEnabled)
    {
        cameraPosition = m_cameraOverridePosition;
        camera.fov = m_cameraOverrideFov;
        const float pitch = DegreesToRadians(m_cameraOverrideRotation.x), yaw = DegreesToRadians(m_cameraOverrideRotation.y);
        cameraForward = Normalize({std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
    }
    m_frameView.cameraPosition = cameraPosition;
    m_frameView.environmentTexture =
        m_resources && !m_environmentPath.empty() ? m_resources->Load<TextureResource>(m_environmentPath) : nullptr;
    m_frameView.environmentIrradiance =
        m_resources && !m_environmentIrradiancePath.empty() ? m_resources->Load<TextureResource>(m_environmentIrradiancePath) : nullptr;
    m_frameView.environmentPrefilter = m_resources && !m_environmentPrefilterPath.empty()
                                           ? m_resources->Load<TextureResource>(m_environmentPrefilterPath)
                                           : m_frameView.environmentTexture;
    m_frameView.environmentBrdfLut =
        m_resources && !m_environmentBrdfPath.empty() ? m_resources->Load<TextureResource>(m_environmentBrdfPath) : nullptr;
    m_frameView.view = LookAt(cameraPosition, cameraPosition + cameraForward, {0.0f, 1.0f, 0.0f});
    m_frameView.projection =
        Perspective(camera.fov, m_device ? m_device->GetAspectRatio() : 16.0f / 9.0f, camera.nearPlane, camera.farPlane);
    m_frameView.lightCount = 0;
    world.GetComponentManager().ForEach<LightComponent>([&](Entity, LightComponent &light) {
        if (m_frameView.lightCount >= static_cast<int>(FrameView::MaxLights))
            return;
        const int index = m_frameView.lightCount++;
        m_frameView.lightDirections[index] = Normalize(light.direction);
        m_frameView.lightColors[index] = light.color;
        m_frameView.lightIntensities[index] = light.intensity;
        if (index == 0)
        {
            m_frameView.lightDirection = m_frameView.lightDirections[index];
            m_frameView.lightColor = light.color;
            m_frameView.lightIntensity = light.intensity;
        }
    });
    if (m_frameView.lightCount == 0)
    {
        m_frameView.lightCount = 1;
        m_frameView.lightDirections[0] = m_frameView.lightDirection;
        m_frameView.lightColors[0] = m_frameView.lightColor;
        m_frameView.lightIntensities[0] = m_frameView.lightIntensity;
    }
    const Vec3 lightDirection = Normalize(m_frameView.lightDirection);
    const Vec3 shadowUp = std::abs(Dot(lightDirection, Vec3{0.0f, 1.0f, 0.0f})) > 0.95f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const float shadowFar = std::min(camera.farPlane, 200.0f), splitLambda = 0.72f;
    float cascadeNear = camera.nearPlane;
    for (std::size_t cascade = 0; cascade < FrameView::MaxShadowCascades; ++cascade)
    {
        const float fraction = static_cast<float>(cascade + 1) / static_cast<float>(FrameView::MaxShadowCascades);
        const float logarithmic = camera.nearPlane * std::pow(shadowFar / camera.nearPlane, fraction);
        const float uniform = camera.nearPlane + (shadowFar - camera.nearPlane) * fraction;
        const float cascadeFar = logarithmic * splitLambda + uniform * (1.0f - splitLambda);
        const float centerDistance = (cascadeNear + cascadeFar) * 0.5f;
        const Vec3 focus = cameraPosition + cameraForward * centerDistance;
        const float vertical = cascadeFar * std::tan(DegreesToRadians(camera.fov) * 0.5f);
        const float radius =
            std::ceil(std::sqrt(vertical * vertical * (1.0f + std::pow(m_device ? m_device->GetAspectRatio() : 1.0f, 2.0f))) +
                      (cascadeFar - cascadeNear) * 0.5f);
        m_frameView.shadowViewProjections[cascade] = Orthographic(-radius, radius, -radius, radius, 0.1f, radius * 4.0f + 20.0f) *
                                                     LookAt(focus - lightDirection * (radius * 2.0f + 5.0f), focus, shadowUp);
        m_frameView.shadowCascadeSplits[cascade] = cascadeFar;
        cascadeNear = cascadeFar;
    }
    m_frameView.lightViewProjection = m_frameView.shadowViewProjections[0];

    world.GetComponentManager().ForEach<MeshComponent>([&](const Entity entity, MeshComponent &meshComponent) {
        if (!m_resources)
            return;
        if (const auto *active = world.GetComponent<ActiveComponent>(entity); active && !active->active)
            return;
        auto material = m_resources->Load<MaterialResource>(meshComponent.materialPath);
        auto loadTexture = [&](const std::string &path) {
            return path.empty() ? std::shared_ptr<TextureResource>{} : m_resources->Load<TextureResource>(path);
        };
        const auto *transform = world.GetComponent<TransformComponent>(entity);
        const Vec3 position = transform ? transform->position : Vec3{};
        const Vec3 offset = position - cameraPosition;
        const float distance = Length(offset);
        const float facing = distance > 0.001f ? Dot(offset / distance, cameraForward) : 1.0f;
        const float halfFov = DegreesToRadians(camera.fov * 0.65f);
        if (distance > camera.farPlane || (distance > camera.nearPlane && facing < std::cos(halfFov)))
        {
            ++m_lastCulled;
            return;
        }
        std::string meshPath = meshComponent.meshPath;
        if (const auto *lod = world.GetComponent<LodComponent>(entity))
        {
            if (distance >= lod->level2Distance && !lod->level2MeshPath.empty())
            {
                meshPath = lod->level2MeshPath;
                ++m_lastLodSwitches;
            }
            else if (distance >= lod->level1Distance && !lod->level1MeshPath.empty())
            {
                meshPath = lod->level1MeshPath;
                ++m_lastLodSwitches;
            }
        }
        auto mesh = m_resources->Load<MeshResource>(meshPath);
        if (!mesh)
            return;
        const bool transparent = material && material->IsTransparent();
        RenderItem item;
        item.entity = entity;
        item.distanceToCamera = distance;
        item.model = world.GetWorldMatrix(entity);
        item.worldBounds = TransformBounds(mesh->GetBounds(), item.model);
        if (const auto *animation = world.GetComponent<AnimationComponent>(entity))
            item.skinMatrices = animation->skinMatrices;
        item.mesh = std::move(mesh);
        item.transparent = transparent;
        if (material)
        {
            item.texture = loadTexture(material->GetTexturePath());
            item.normalTexture = loadTexture(material->GetNormalTexturePath());
            item.metallicTexture = loadTexture(material->GetMetallicTexturePath());
            item.roughnessTexture = loadTexture(material->GetRoughnessTexturePath());
            item.aoTexture = loadTexture(material->GetAoTexturePath());
            item.emissiveTexture = loadTexture(material->GetEmissiveTexturePath());
            item.packedTexture = loadTexture(material->GetPackedTexturePath());
        }
        item.material = std::move(material);
        m_renderQueue.AddItem(item);
    });
    m_renderQueue.Sort();
    const std::size_t beforeOcclusion = m_renderQueue.GetItems().size();
    m_renderQueue.ApplyConservativeOcclusion(cameraPosition);
    m_lastCulled += beforeOcclusion - m_renderQueue.GetItems().size();
}

void Renderer::RenderFrame(World &world)
{
    if (!m_device)
        return;
    const auto totalStart = std::chrono::steady_clock::now();
    BuildRenderQueue(world);
    const auto queueEnd = std::chrono::steady_clock::now();
    const auto batches = BuildInstanceBatches(m_renderQueue.GetItems());
    for (std::size_t cascade = 0; cascade < FrameView::MaxShadowCascades; ++cascade)
    {
        m_device->BeginShadowPass(m_frameView, cascade);
        for (const auto &batch : batches)
            if (batch.size() > 1)
                m_device->DrawShadowInstanced(batch);
            else
                m_device->DrawShadow(batch.front());
        m_device->EndShadowPass();
    }
    const auto shadowEnd = std::chrono::steady_clock::now();
    m_device->BeginFrame(m_frameView);
    for (const auto &batch : batches)
        if (batch.size() > 1)
            m_device->DrawInstanced(batch);
        else
            m_device->Draw(batch.front());
    std::size_t particleCount = 0;
    if (m_vfxSystem)
    {
        const auto particles = m_vfxSystem->BuildParticleList();
        particleCount = particles.size();
        m_device->DrawParticles(particles);
    }
    if (m_uiSystem)
    {
        const Vec2 viewport = m_device->GetViewportSize();
        m_uiSystem->SetViewportSize(viewport.x, viewport.y);
        m_device->DrawUi(m_uiSystem->BuildDrawList());
    }
    m_device->EndFrame();
    if (m_overlayCallback)
        m_overlayCallback();
    m_device->Present();
    const auto sceneEnd = std::chrono::steady_clock::now();
    m_lastDrawCalls = batches.size() + particleCount;
    const auto milliseconds = [](const auto start, const auto end) {
        return std::chrono::duration<float, std::milli>(end - start).count();
    };
    m_lastFrameProfile = {milliseconds(totalStart, queueEnd), milliseconds(queueEnd, shadowEnd), milliseconds(shadowEnd, sceneEnd),
                          milliseconds(totalStart, sceneEnd)};
    if (m_logger)
        m_logger->Debug("RenderFrame draw calls: " + std::to_string(m_lastDrawCalls));
}

std::size_t Renderer::GetLastDrawCallCount() const
{
    return m_lastDrawCalls;
}
const FrameView &Renderer::GetFrameView() const
{
    return m_frameView;
}
RenderDevice *Renderer::GetDevice()
{
    return m_device.get();
}
void Renderer::SetOverlayCallback(std::function<void()> callback)
{
    m_overlayCallback = std::move(callback);
}
void Renderer::SetUISystem(UISystem *uiSystem)
{
    m_uiSystem = uiSystem;
}
void Renderer::SetVfxSystem(VfxSystem *vfxSystem)
{
    m_vfxSystem = vfxSystem;
}
std::size_t Renderer::GetLastCulledCount() const
{
    return m_lastCulled;
}
std::size_t Renderer::GetLastLodSwitchCount() const
{
    return m_lastLodSwitches;
}
const FrameProfile &Renderer::GetLastFrameProfile() const
{
    return m_lastFrameProfile;
}
void Renderer::SetCameraOverride(const bool enabled, const Vec3 &position, const Vec3 &rotation, const float fov)
{
    m_cameraOverrideEnabled = enabled;
    m_cameraOverridePosition = position;
    m_cameraOverrideRotation = rotation;
    m_cameraOverrideFov = fov;
}
void Renderer::SetEnvironmentPath(std::string path)
{
    m_environmentPath = std::move(path);
    m_environmentIrradiancePath.clear();
    m_environmentPrefilterPath.clear();
    m_environmentBrdfPath.clear();
    if (std::filesystem::path(m_environmentPath).extension() != ".env")
        return;
    std::istringstream input(PlatformFile::ReadAllText(m_environmentPath));
    std::string magic, token, value;
    int version = 0;
    if (!(input >> magic >> version) || magic != "TINY_ENV" || version != 1)
        return;
    const auto root = std::filesystem::path(m_environmentPath).parent_path();
    while (input >> token >> std::quoted(value))
    {
        const std::string resolved = (root / value).lexically_normal().generic_string();
        if (token == "irradiance")
            m_environmentIrradiancePath = resolved;
        else if (token == "prefilter")
            m_environmentPrefilterPath = resolved;
        else if (token == "brdf")
            m_environmentBrdfPath = resolved;
    }
    m_environmentPath.clear();
}
} // namespace MiniEngine
