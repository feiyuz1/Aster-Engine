// 文件：src/render/renderer.cpp
// 作用：组织相机、可见性、光照、阴影、后处理和 UI 的逐帧渲染。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：实现文件。

#include "tinyengine/render/renderer.h"

#include "tinyengine/core/logger.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/resource/resource_manager.h"
#include "tinyengine/vfx/particle_system.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace tinyengine
{
bool NullRenderDevice::initialize()
{
    return true;
}
void NullRenderDevice::shutdown() {}
bool NullRenderDevice::pumpEvents()
{
    return true;
}
const InputState& NullRenderDevice::GetInputState() const
{
    return m_inputState;
}
float NullRenderDevice::getAspectRatio() const
{
    return 16.0f / 9.0f;
}
Vec2 NullRenderDevice::getViewportSize() const
{
    return {1280.0f, 720.0f};
}
void NullRenderDevice::beginShadowPass(const FrameView&, const std::size_t) {}
void NullRenderDevice::drawShadow(const RenderItem&) {}
void NullRenderDevice::drawShadowInstanced(const std::vector<RenderItem>&) {}
void NullRenderDevice::endShadowPass() {}
void NullRenderDevice::beginFrame(const FrameView&) {}
void NullRenderDevice::drawSkybox() {}
void NullRenderDevice::draw(const RenderItem&) {}
void NullRenderDevice::drawInstanced(const std::vector<RenderItem>&) {}
void NullRenderDevice::drawParticles(const std::vector<Particle>&) {}
void NullRenderDevice::drawGpuParticles(const std::vector<GpuParticleEmitter>&) {}
void NullRenderDevice::executePostProcess(const PostProcessPass) {}
void NullRenderDevice::endFrame() {}
void NullRenderDevice::drawUi(const std::vector<UIDrawCommand>&) {}
void NullRenderDevice::present() {}
std::size_t NullRenderDevice::getResidentGpuBytes() const
{
    return 0;
}
void NullRenderDevice::setGpuMemoryBudget(const std::size_t bytes)
{
    m_gpuBudget = bytes;
}
void NullRenderDevice::trimGpuResources() {}

void RenderQueue::addItem(const RenderItem& item)
{
    m_items.push_back(item);
}
void RenderQueue::sort()
{
    std::sort(m_items.begin(), m_items.end(),
              [](const RenderItem& lhs, const RenderItem& rhs)
              {
                  if (lhs.transparent != rhs.transparent)
                      return !lhs.transparent;
                  return lhs.transparent ? lhs.distanceToCamera > rhs.distanceToCamera
                                         : lhs.distanceToCamera < rhs.distanceToCamera;
              });
}
void RenderQueue::clear()
{
    m_items.clear();
}
const std::vector<RenderItem>& RenderQueue::GetItems() const
{
    return m_items;
}

namespace
{
// 用六个裁剪平面测试世界包围盒；只在整个盒子位于某个平面外时剔除。
bool intersectsFrustum(const Aabb& bounds, const Mat4& viewProjection)
{
    for (std::size_t axis = 0; axis < 3; ++axis)
        for (float sign : {-1.0f, 1.0f})
        {
            const Vec3 normal{viewProjection.at(3, 0) + sign * viewProjection.at(axis, 0),
                              viewProjection.at(3, 1) + sign * viewProjection.at(axis, 1),
                              viewProjection.at(3, 2) + sign * viewProjection.at(axis, 2)};
            const float offset = viewProjection.at(3, 3) + sign * viewProjection.at(axis, 3);
            const Vec3 support{normal.x >= 0 ? bounds.maximum.x : bounds.minimum.x,
                               normal.y >= 0 ? bounds.maximum.y : bounds.minimum.y,
                               normal.z >= 0 ? bounds.maximum.z : bounds.minimum.z};
            if (dot(normal, support) + offset < -0.00001f)
                return false;
        }
    return true;
}

float matrixDifference(const Mat4& left, const Mat4& right)
{
    float difference = 0;
    for (std::size_t index = 0; index < left.data.size(); ++index)
        difference = std::max(difference, std::abs(left.data[index] - right.data[index]));
    return difference;
}

Aabb transformBounds(const Aabb& bounds, const Mat4& matrix)
{
    const Vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
    const Vec3 extent = (bounds.maximum - bounds.minimum) * 0.5f;
    const Vec3 worldCenter{
        matrix.at(0, 0) * center.x + matrix.at(0, 1) * center.y + matrix.at(0, 2) * center.z + matrix.at(0, 3),
        matrix.at(1, 0) * center.x + matrix.at(1, 1) * center.y + matrix.at(1, 2) * center.z + matrix.at(1, 3),
        matrix.at(2, 0) * center.x + matrix.at(2, 1) * center.y + matrix.at(2, 2) * center.z + matrix.at(2, 3)};
    const Vec3 worldExtent{std::abs(matrix.at(0, 0)) * extent.x + std::abs(matrix.at(0, 1)) * extent.y +
                               std::abs(matrix.at(0, 2)) * extent.z,
                           std::abs(matrix.at(1, 0)) * extent.x + std::abs(matrix.at(1, 1)) * extent.y +
                               std::abs(matrix.at(1, 2)) * extent.z,
                           std::abs(matrix.at(2, 0)) * extent.x + std::abs(matrix.at(2, 1)) * extent.y +
                               std::abs(matrix.at(2, 2)) * extent.z};
    return {worldCenter - worldExtent, worldCenter + worldExtent};
}

std::vector<std::vector<RenderItem>> BuildInstanceBatches(const std::vector<RenderItem>& items)
{
    std::vector<std::vector<RenderItem>> batches;
    std::unordered_map<std::size_t, std::vector<std::size_t>> lookup;
    for (const auto& item : items)
    {
        if (item.transparent || !item.skinMatrices.empty() || !item.mesh)
        {
            batches.push_back({item});
            continue;
        }
        const auto key =
            std::hash<std::string>{}(item.mesh->GetPath()) ^
            (item.materialProxy ? item.materialProxy->valueHash
                                : std::hash<std::string>{}(item.material ? item.material->GetPath() : std::string{}));
        auto& candidates = lookup[key];
        const auto found = std::find_if(candidates.begin(), candidates.end(),
                                        [&](auto index)
                                        {
                                            const auto& first = batches[index].front();
                                            if (first.mesh->GetPath() != item.mesh->GetPath() ||
                                                bool(first.materialProxy) != bool(item.materialProxy))
                                                return false;
                                            if (item.materialProxy)
                                                return first.materialProxy->canonicalValueContent ==
                                                       item.materialProxy->canonicalValueContent;
                                            return (first.material ? first.material->GetPath() : std::string{}) ==
                                                   (item.material ? item.material->GetPath() : std::string{});
                                        });
        if (found == candidates.end())
        {
            candidates.push_back(batches.size());
            batches.push_back({item});
        }
        else
            batches[*found].push_back(item);
    }
    return batches;
}

float halton(std::uint64_t index, const std::uint32_t base)
{
    float result = 0.0f;
    float fraction = 1.0f;
    while (index > 0)
    {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}
} // namespace

void Renderer::setLogger(Logger* logger)
{
    m_logger = logger;
}
void Renderer::setResourceManager(ResourceManager* resources)
{
    m_resources = resources;
    m_materialSystem = resources ? std::make_unique<MaterialSystem>(*resources) : nullptr;
    m_dynamicMaterials.clear();
}
void Renderer::setDynamicMaterial(Entity entity, std::shared_ptr<DynamicMaterialInstance> instance)
{
    if (instance)
        m_dynamicMaterials[entity] = std::move(instance);
    else
        m_dynamicMaterials.erase(entity);
}

bool Renderer::initialize(std::unique_ptr<RenderDevice> device)
{
    m_device = std::move(device);
    return m_device && m_device->initialize();
}

void Renderer::shutdown()
{
    m_renderQueue.clear();
    m_shadowItems.clear();
    m_lastLodSwitches = 0;
    m_lastCulled = 0;
    m_renderGraph.reset();
    m_previousModels.clear();
    m_dynamicMaterials.clear();
    m_hasPreviousView = false;
    m_frameNumber = 0;
    if (m_device)
    {
        m_device->shutdown();
        m_device.reset();
    }
}

bool Renderer::pumpEvents()
{
    return m_device && m_device->pumpEvents();
}
const InputState& Renderer::GetInputState() const
{
    static const InputState empty{};
    return m_device ? m_device->GetInputState() : empty;
}

// 在提交 GPU 前拍平 ECS 数据，并在这里完成视锥、LOD、遮挡候选和材质资源解析。
void Renderer::buildRenderQueue(World& world)
{
    for (auto it = m_dynamicMaterials.begin(); it != m_dynamicMaterials.end();)
        if (!world.isAlive(it->first))
            it = m_dynamicMaterials.erase(it);
        else
            ++it;
    m_renderQueue.clear();
    m_shadowItems.clear();
    m_lastCulled = 0;
    m_lastLodSwitches = 0;
    Vec3 cameraPosition{0.0f, 0.0f, 5.0f};
    Vec3 cameraForward{0.0f, 0.0f, -1.0f};
    CameraComponent camera{};
    world.GetComponentManager().ForEach<CameraComponent>(
        [&](const Entity entity, CameraComponent& candidate)
        {
            if (!candidate.primary)
                return;
            camera = candidate;
            if (const auto* transform = world.GetComponent<TransformComponent>(entity))
            {
                cameraPosition = transform->position;
                const float pitch = degreesToRadians(transform->rotation.x);
                const float yaw = degreesToRadians(transform->rotation.y);
                cameraForward =
                    normalize({std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
            }
        });
    if (m_cameraOverrideEnabled)
    {
        cameraPosition = m_cameraOverridePosition;
        camera.fov = m_cameraOverrideFov;
        const float pitch = degreesToRadians(m_cameraOverrideRotation.x),
                    yaw = degreesToRadians(m_cameraOverrideRotation.y);
        cameraForward = normalize({std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
    }
    m_frameView.renderMode = m_cameraOverrideEnabled ? m_viewportRenderMode : ViewportRenderMode::Lit;
    m_frameView.cameraPosition = cameraPosition;
    m_frameView.environmentTexture =
        m_resources && !m_environmentPath.empty() ? m_resources->load<TextureResource>(m_environmentPath) : nullptr;
    m_frameView.environmentSky = m_resources && !m_environmentSkyPath.empty()
                                     ? m_resources->load<TextureResource>(m_environmentSkyPath)
                                     : nullptr;
    m_frameView.environmentIrradiance = m_resources && !m_environmentIrradiancePath.empty()
                                            ? m_resources->load<TextureResource>(m_environmentIrradiancePath)
                                            : nullptr;
    m_frameView.environmentPrefilter = m_resources && !m_environmentPrefilterPath.empty()
                                           ? m_resources->load<TextureResource>(m_environmentPrefilterPath)
                                           : m_frameView.environmentTexture;
    m_frameView.environmentBrdfLut = m_resources && !m_environmentBrdfPath.empty()
                                         ? m_resources->load<TextureResource>(m_environmentBrdfPath)
                                         : nullptr;
    m_frameView.view = lookAt(cameraPosition, cameraPosition + cameraForward, {0.0f, 1.0f, 0.0f});
    m_frameView.projection = perspective(camera.fov, m_device ? m_device->getAspectRatio() : 16.0f / 9.0f,
                                         camera.nearPlane, camera.farPlane);
    const Mat4 unjitteredProjection = m_frameView.projection;
    const Mat4 unjitteredViewProjection = unjitteredProjection * m_frameView.view;
    m_frameView.occlusionHistoryValid =
        m_hasPreviousView && matrixDifference(unjitteredViewProjection, m_previousUnjitteredViewProjection) < 0.000001f;
    m_frameView.resetTemporalHistory = !m_hasPreviousView || m_previousCameraOverride != m_cameraOverrideEnabled ||
                                       length(cameraPosition - m_previousCameraPosition) > 1.0f ||
                                       dot(cameraForward, m_previousCameraForward) < 0.95f ||
                                       matrixDifference(unjitteredProjection, m_previousProjection) > 0.00001f;
    const Vec2 viewport = m_device ? m_device->getViewportSize() : Vec2{1280.0f, 720.0f};
    const std::uint64_t jitterIndex = m_frameNumber % 8 + 1;
    // 编辑和点选需要稳定投影；关闭 TAA 时不得继续抖动画面。
    m_frameView.temporalJitter = {};
    if (!m_cameraOverrideEnabled && m_device && m_device->usesTemporalJitter())
        m_frameView.temporalJitter = {(halton(jitterIndex, 2) - 0.5f) / std::max(1.0f, viewport.x),
                                      (halton(jitterIndex, 3) - 0.5f) / std::max(1.0f, viewport.y)};
    m_frameView.projection.at(0, 2) += m_frameView.temporalJitter.x * 2.0f;
    m_frameView.projection.at(1, 2) += m_frameView.temporalJitter.y * 2.0f;
    const Mat4 currentViewProjection = m_frameView.projection * m_frameView.view;
    m_frameView.previousViewProjection = m_hasPreviousView ? m_previousViewProjection : currentViewProjection;
    m_frameView.lightCount = 0;
    std::vector<RenderLight> renderLights;
    world.GetComponentManager().ForEach<LightComponent>(
        [&](const Entity entity, LightComponent& light)
        {
            if (m_frameView.lightCount >= static_cast<int>(FrameView::kMaxLights))
                return;
            const int index = m_frameView.lightCount++;
            RenderLight renderLight;
            renderLight.type =
                light.type == LightType::Point
                    ? RenderLightType::Point
                    : (light.type == LightType::Spot ? RenderLightType::Spot : RenderLightType::Directional);
            if (const auto* transform = world.GetComponent<TransformComponent>(entity))
                renderLight.position = transform->position;
            renderLight.direction = normalize(light.direction);
            renderLight.color = light.color;
            renderLight.intensity = light.intensity;
            renderLight.range = std::max(0.01f, light.range);
            renderLight.innerConeCosine = std::cos(degreesToRadians(std::clamp(light.innerConeAngle, 0.0f, 89.0f)));
            renderLight.outerConeCosine =
                std::cos(degreesToRadians(std::clamp(light.outerConeAngle, light.innerConeAngle, 89.0f)));
            m_frameView.lights[index] = renderLight;
            renderLights.push_back(renderLight);
            if (index == 0)
            {
                m_frameView.lightDirection = renderLight.direction;
                m_frameView.lightColor = light.color;
                m_frameView.lightIntensity = light.intensity;
            }
        });
    if (m_frameView.lightCount == 0)
    {
        m_frameView.lightCount = 1;
        RenderLight fallback;
        fallback.direction = m_frameView.lightDirection;
        fallback.color = m_frameView.lightColor;
        fallback.intensity = m_frameView.lightIntensity;
        m_frameView.lights[0] = fallback;
        renderLights.push_back(fallback);
    }
    m_frameView.clusteredLights = ClusteredLightBuilder::build(
        renderLights, m_frameView.view, m_frameView.projection, static_cast<std::uint32_t>(viewport.x),
        static_cast<std::uint32_t>(viewport.y), camera.nearPlane, camera.farPlane);
    const Vec3 lightDirection = normalize(m_frameView.lightDirection);
    const Vec3 shadowUp =
        std::abs(dot(lightDirection, Vec3{0.0f, 1.0f, 0.0f})) > 0.95f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const float shadowFar = std::min(camera.farPlane, 200.0f), splitLambda = 0.72f;
    float cascadeNear = camera.nearPlane;
    for (std::size_t cascade = 0; cascade < FrameView::kMaxShadowCascades; ++cascade)
    {
        const float fraction = static_cast<float>(cascade + 1) / static_cast<float>(FrameView::kMaxShadowCascades);
        const float logarithmic = camera.nearPlane * std::pow(shadowFar / camera.nearPlane, fraction);
        const float uniform = camera.nearPlane + (shadowFar - camera.nearPlane) * fraction;
        const float cascadeFar = logarithmic * splitLambda + uniform * (1.0f - splitLambda);
        const float centerDistance = (cascadeNear + cascadeFar) * 0.5f;
        const Vec3 focus = cameraPosition + cameraForward * centerDistance;
        const float vertical = cascadeFar * std::tan(degreesToRadians(camera.fov) * 0.5f);
        const float radius = std::ceil(
            std::sqrt(vertical * vertical * (1.0f + std::pow(m_device ? m_device->getAspectRatio() : 1.0f, 2.0f))) +
            (cascadeFar - cascadeNear) * 0.5f);
        Mat4 shadowProjection = orthographic(-radius, radius, -radius, radius, 0.1f, radius * 4.0f + 20.0f);
        const Mat4 shadowView = lookAt(focus - lightDirection * (radius * 2.0f + 5.0f), focus, shadowUp);
        const Mat4 shadowViewProjection = shadowProjection * shadowView;
        const float texelScale = FrameView::kShadowMapResolution * 0.5f;
        // 把固定世界原点的投影对齐到阴影像素，消除相机亚像素移动造成的阴影游动。
        for (std::size_t axis = 0; axis < 2; ++axis)
        {
            const float origin = shadowViewProjection.at(axis, 3) * texelScale;
            shadowProjection.at(axis, 3) += (std::round(origin) - origin) / texelScale;
        }
        m_frameView.shadowViewProjections[cascade] = shadowProjection * shadowView;
        m_frameView.shadowCascadeSplits[cascade] = cascadeFar;
        cascadeNear = cascadeFar;
    }
    m_frameView.lightViewProjection = m_frameView.shadowViewProjections[0];

    world.GetComponentManager().ForEach<MeshComponent>(
        [&](const Entity entity, MeshComponent& meshComponent)
        {
            if (!m_resources)
                return;
            if (const auto* active = world.GetComponent<ActiveComponent>(entity); active && !active->active)
                return;
            auto loadTexture = [&](const std::string& path)
            { return path.empty() ? std::shared_ptr<TextureResource>{} : m_resources->load<TextureResource>(path); };
            const Mat4 model = world.getWorldMatrix(entity);
            const Vec3 position{model.at(0, 3), model.at(1, 3), model.at(2, 3)};
            const Vec3 offset = position - cameraPosition;
            const float distance = length(offset);
            std::string meshPath = meshComponent.meshPath;
            if (const auto* lod = world.GetComponent<LodComponent>(entity))
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
            auto mesh = m_resources->load<MeshResource>(meshPath);
            if (!mesh)
                return;
            const Aabb bounds = transformBounds(mesh->GetBounds(), model);
            const auto* animation = world.GetComponent<AnimationComponent>(entity);
            // 蒙皮可能超出静态网格包围盒；没有动态包围盒时保守保留，避免动画被误剔除。
            const bool skinned = animation && !animation->skinMatrices.empty();
            const bool sceneVisible = skinned || intersectsFrustum(bounds, unjitteredViewProjection);
            bool castsShadow = skinned;
            for (const auto& shadowMatrix : m_frameView.shadowViewProjections)
                castsShadow = castsShadow || intersectsFrustum(bounds, shadowMatrix);
            if (!sceneVisible)
                ++m_lastCulled;
            if (!sceneVisible && !castsShadow)
                return;
            auto material = m_resources->load<MaterialResource>(meshComponent.materialPath);
            const auto dynamic = m_dynamicMaterials.find(entity);
            auto proxy = m_materialSystem->BuildProxy(
                meshComponent.materialPath, dynamic == m_dynamicMaterials.end() ? nullptr : dynamic->second.get());
            const bool transparent = proxy && proxy->state.blendMode == MaterialBlendMode::Alpha;
            RenderItem item;
            item.entity = entity;
            item.distanceToCamera = distance;
            item.model = model;
            const auto previousModel = m_previousModels.find(entity);
            item.previousModel = previousModel != m_previousModels.end() ? previousModel->second : item.model;
            item.worldBounds = bounds;
            if (animation)
                item.skinMatrices = animation->skinMatrices;
            item.mesh = std::move(mesh);
            item.materialProxy = std::move(proxy);
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
            if (sceneVisible)
                m_renderQueue.addItem(item);
            if (castsShadow)
                m_shadowItems.push_back(std::move(item));
        });
    m_renderQueue.sort();
    m_previousViewProjection = currentViewProjection;
    m_previousUnjitteredViewProjection = unjitteredViewProjection;
    m_previousProjection = unjitteredProjection;
    m_previousCameraPosition = cameraPosition;
    m_previousCameraForward = cameraForward;
    m_previousCameraOverride = m_cameraOverrideEnabled;
    m_hasPreviousView = true;
    m_previousModels.clear();
    for (const auto& item : m_renderQueue.GetItems())
        m_previousModels[item.entity] = item.model;
    for (const auto& item : m_shadowItems)
        m_previousModels[item.entity] = item.model;
    ++m_frameNumber;
}

// 每帧重建 RenderGraph，明确表达阴影、深度层级、场景、GTAO、Bloom、TAA 和色调映射的依赖。
void Renderer::renderFrame(World& world)
{
    if (!m_device)
        return;
    const auto totalStart = std::chrono::steady_clock::now();
    buildRenderQueue(world);
    const auto queueEnd = std::chrono::steady_clock::now();
    const auto batches = BuildInstanceBatches(m_renderQueue.GetItems());
    const auto shadowBatches = BuildInstanceBatches(m_shadowItems);
    const auto particles = m_vfxSystem ? m_vfxSystem->BuildParticleList() : std::vector<Particle>{};
    const auto gpuEmitters = m_vfxSystem ? m_vfxSystem->BuildGpuEmitterList() : std::vector<GpuParticleEmitter>{};
    std::size_t particleCount = particles.size();
    for (const auto& emitter : gpuEmitters)
        particleCount += emitter.settings.maximumParticles;

    m_renderGraph.reset();
    const auto shadow0 =
        m_renderGraph.createResource({RenderGraphResourceType::Texture, "ShadowCascade0", 2048, 2048, 1, 1});
    const auto shadow1 =
        m_renderGraph.createResource({RenderGraphResourceType::Texture, "ShadowCascade1", 2048, 2048, 1, 1});
    const auto shadow2 =
        m_renderGraph.createResource({RenderGraphResourceType::Texture, "ShadowCascade2", 2048, 2048, 1, 1});
    const Vec2 viewport = m_device->getViewportSize();
    const auto hdr = m_renderGraph.createResource({RenderGraphResourceType::Texture, "SceneHDR",
                                                   static_cast<std::uint32_t>(viewport.x),
                                                   static_cast<std::uint32_t>(viewport.y), 1, 2});
    const auto ambient = m_renderGraph.createResource({RenderGraphResourceType::Texture, "SceneAmbient",
                                                       static_cast<std::uint32_t>(viewport.x),
                                                       static_cast<std::uint32_t>(viewport.y), 1, 2});
    const auto depth = m_renderGraph.createResource({RenderGraphResourceType::Texture, "SceneDepth",
                                                     static_cast<std::uint32_t>(viewport.x),
                                                     static_cast<std::uint32_t>(viewport.y), 1, 3});
    const auto velocity = m_renderGraph.createResource({RenderGraphResourceType::Texture, "MotionVectors",
                                                        static_cast<std::uint32_t>(viewport.x),
                                                        static_cast<std::uint32_t>(viewport.y), 1, 4});
    const auto hzb = m_renderGraph.createResource({RenderGraphResourceType::Texture, "HierarchicalDepth",
                                                   static_cast<std::uint32_t>(viewport.x),
                                                   static_cast<std::uint32_t>(viewport.y), 1, 5});
    const auto gtao = m_renderGraph.createResource({RenderGraphResourceType::Texture, "GTAO",
                                                    static_cast<std::uint32_t>(viewport.x / 2),
                                                    static_cast<std::uint32_t>(viewport.y / 2), 1, 6});
    const auto bloom = m_renderGraph.createResource({RenderGraphResourceType::Texture, "BloomPyramid",
                                                     static_cast<std::uint32_t>(viewport.x / 2),
                                                     static_cast<std::uint32_t>(viewport.y / 2), 5, 2});
    const auto history = m_renderGraph.importResource({RenderGraphResourceType::Texture, "TemporalHistory",
                                                       static_cast<std::uint32_t>(viewport.x),
                                                       static_cast<std::uint32_t>(viewport.y), 1, 2});
    const auto resolved = m_renderGraph.createResource({RenderGraphResourceType::Texture, "TemporalResolvedHDR",
                                                        static_cast<std::uint32_t>(viewport.x),
                                                        static_cast<std::uint32_t>(viewport.y), 1, 2});
    const auto output = m_renderGraph.importResource({RenderGraphResourceType::Texture, "SceneOutput",
                                                      static_cast<std::uint32_t>(viewport.x),
                                                      static_cast<std::uint32_t>(viewport.y), 1, 7});

    const std::array<RenderGraphResourceHandle, 3> shadows{shadow0, shadow1, shadow2};
    for (std::size_t cascade = 0; cascade < shadows.size(); ++cascade)
        m_renderGraph.addPass({"Shadow Cascade " + std::to_string(cascade),
                               {},
                               {shadows[cascade]},
                               [&, cascade]
                               {
                                   m_device->beginShadowPass(m_frameView, cascade);
                                   for (const auto& batch : shadowBatches)
                                       if (batch.size() > 1)
                                           m_device->drawShadowInstanced(batch);
                                       else
                                           m_device->drawShadow(batch.front());
                                   m_device->endShadowPass();
                               }});
    m_renderGraph.addPass({"Forward+ Scene",
                           {shadow0, shadow1, shadow2},
                           {hdr, ambient, depth, velocity},
                           [&]
                           {
                               m_device->beginFrame(m_frameView);
                               m_device->drawSkybox();
                               for (const auto& batch : batches)
                                   if (batch.size() > 1)
                                       m_device->drawInstanced(batch);
                                   else
                                       m_device->draw(batch.front());
                               m_device->drawParticles(particles);
                               m_device->drawGpuParticles(gpuEmitters);
                           }});
    m_renderGraph.addPass(
        {"GPU HZB", {depth}, {hzb}, [&] { m_device->executePostProcess(PostProcessPass::HierarchicalDepth); }});
    m_renderGraph.addPass({"GTAO", {depth, hzb}, {gtao}, [&] { m_device->executePostProcess(PostProcessPass::Gtao); }});
    m_renderGraph.addPass(
        {"Bloom Pyramid", {hdr}, {bloom}, [&] { m_device->executePostProcess(PostProcessPass::Bloom); }});
    m_renderGraph.addPass({"Motion Vector TAA", {hdr, ambient, depth, velocity, gtao, bloom, history}, {resolved}, [&] {
                               m_device->executePostProcess(PostProcessPass::TemporalResolve);
                           }});
    m_renderGraph.addPass(
        {"Tone Map", {resolved}, {output}, [&] { m_device->executePostProcess(PostProcessPass::ToneMap); }, true});
    const auto drawRuntimeUi = [&]
    {
        if (m_uiSystem)
        {
            m_uiSystem->setViewportSize(viewport.x, viewport.y);
            m_device->drawUi(m_uiSystem->BuildDrawList());
        }
    };
    std::string graphError;
    if (!m_renderGraph.compile(&graphError) || !m_renderGraph.execute(&graphError))
    {
        if (m_logger)
            m_logger->error(graphError);
        // 失败帧也必须完成 ImGui 帧和 Present：跳过会让按键/点击的边沿状态跨帧丢失，出现丢键与粘滞。
        // EndFrame 会把目标切回窗口默认帧缓冲并清屏，编辑器 overlay 在其上绘制控制台错误信息。
        m_device->endFrame();
        drawRuntimeUi();
        if (m_overlayCallback)
            m_overlayCallback();
        m_device->present();
        return;
    }
    const auto shadowEnd = std::chrono::steady_clock::now();
    m_device->endFrame();
    drawRuntimeUi();
    if (m_overlayCallback)
        m_overlayCallback();
    m_device->present();
    const auto sceneEnd = std::chrono::steady_clock::now();
    m_lastDrawCalls = batches.size() + particleCount;
    const auto milliseconds = [](const auto start, const auto end)
    { return std::chrono::duration<float, std::milli>(end - start).count(); };
    m_lastFrameProfile = {milliseconds(totalStart, queueEnd),
                          milliseconds(queueEnd, shadowEnd),
                          milliseconds(shadowEnd, sceneEnd),
                          milliseconds(totalStart, sceneEnd),
                          m_renderGraph.getPassCount(),
                          m_renderGraph.getPhysicalResourceCount(),
                          m_frameView.clusteredLights.lightIndices.size()};
    // 首帧和周期诊断保留可观测性；禁止正常运行时每帧写磁盘日志。
    if (m_logger && (m_frameNumber == 1 || m_frameNumber % 300 == 0))
        m_logger->debug("RenderFrame draw calls: " + std::to_string(m_lastDrawCalls));
}

std::size_t Renderer::getLastDrawCallCount() const
{
    return m_lastDrawCalls;
}
const FrameView& Renderer::GetFrameView() const
{
    return m_frameView;
}
RenderDevice* Renderer::getDevice()
{
    return m_device.get();
}
void Renderer::setOverlayCallback(std::function<void()> callback)
{
    m_overlayCallback = std::move(callback);
}
void Renderer::setUiSystem(UISystem* uiSystem)
{
    m_uiSystem = uiSystem;
}
void Renderer::setVfxSystem(VfxSystem* vfxSystem)
{
    m_vfxSystem = vfxSystem;
}
std::size_t Renderer::getLastCulledCount() const
{
    return m_lastCulled;
}
std::size_t Renderer::getLastLodSwitchCount() const
{
    return m_lastLodSwitches;
}
const FrameProfile& Renderer::GetLastFrameProfile() const
{
    return m_lastFrameProfile;
}
const RenderGraph& Renderer::GetLastRenderGraph() const
{
    return m_renderGraph;
}
void Renderer::setViewportRenderMode(ViewportRenderMode mode)
{
    if (m_viewportRenderMode != mode)
        m_hasPreviousView = false;
    m_viewportRenderMode = mode;
}

void Renderer::setCameraOverride(const bool enabled, const Vec3& position, const Vec3& rotation, const float fov)
{
    m_cameraOverrideEnabled = enabled;
    m_cameraOverridePosition = position;
    m_cameraOverrideRotation = rotation;
    m_cameraOverrideFov = fov;
}
void Renderer::setEnvironmentPath(std::string path)
{
    m_environmentPath = std::move(path);
    m_environmentSkyPath = m_environmentPath;
    m_environmentIrradiancePath.clear();
    m_environmentPrefilterPath.clear();
    m_environmentBrdfPath.clear();
    if (std::filesystem::path(m_environmentPath).extension() != ".env")
        return;
    std::istringstream input(PlatformFile::readAllText(m_environmentPath));
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
        else if (token == "sky")
            m_environmentSkyPath = resolved;
        else if (token == "prefilter")
            m_environmentPrefilterPath = resolved;
        else if (token == "brdf")
            m_environmentBrdfPath = resolved;
    }
    m_environmentPath.clear();
}
} // namespace tinyengine
