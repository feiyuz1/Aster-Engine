// 文件：include/MiniEngine/Render/Renderer.hpp
// 作用：组织相机、可见性、光照、阴影、后处理和 UI 的逐帧渲染。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Render/ClusteredLighting.hpp"
#include "MiniEngine/Render/RenderGraph.hpp"
#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Types.hpp"
#include "MiniEngine/UI/UISystem.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
class Logger;
class ResourceManager;
class World;
class VfxSystem;
struct Particle;
struct GpuParticleEmitter;

struct InputState
{
    float moveX = 0.0f, moveY = 0.0f, moveZ = 0.0f;
    float rotateX = 0.0f, rotateY = 0.0f, rotateZ = 0.0f;
    float scale = 0.0f;
    float mouseX = 0.0f, mouseY = 0.0f;
    bool primaryAction = false;
    bool sprint = false;
    bool jump = false;
    bool quitRequested = false;
    bool uiNavigateNext = false;
    bool uiAccept = false;
    bool uiBackspace = false;
    std::string textInput;
    float cursorX = 0.0f, cursorY = 0.0f;
};

// 一帧共享的视图常量。previousViewProjection 与 temporalJitter 共同支持运动矢量和 TAA。
struct FrameView
{
    static constexpr std::size_t MaxLights = 64;
    static constexpr std::size_t MaxShadowCascades = 3;
    static constexpr int ShadowMapResolution = 2048;
    Mat4 view = Mat4::Identity();
    Mat4 projection = Mat4::Identity();
    Mat4 previousViewProjection = Mat4::Identity();
    Vec2 temporalJitter{};
    bool occlusionHistoryValid = false;
    bool resetTemporalHistory = true;
    Vec3 cameraPosition{0.0f, 0.0f, 5.0f};
    Vec3 lightDirection{-0.4f, -1.0f, -0.3f};
    Vec3 lightColor{1.0f, 1.0f, 1.0f};
    float lightIntensity = 1.0f;
    Mat4 lightViewProjection = Mat4::Identity();
    std::array<Mat4, MaxShadowCascades> shadowViewProjections{};
    std::array<float, MaxShadowCascades> shadowCascadeSplits{};
    std::array<RenderLight, MaxLights> lights{};
    int lightCount = 0;
    ClusteredLightGrid clusteredLights;
    std::shared_ptr<TextureResource> environmentTexture;
    std::shared_ptr<TextureResource> environmentSky;
    std::shared_ptr<TextureResource> environmentIrradiance;
    std::shared_ptr<TextureResource> environmentPrefilter;
    std::shared_ptr<TextureResource> environmentBrdfLut;
};

// 从 ECS 抽取出的不可变绘制快照；渲染设备不直接读取 World，避免提交期间结构变化。
struct RenderItem
{
    Entity entity = InvalidEntity;
    float distanceToCamera = 0.0f;
    Mat4 model = Mat4::Identity();
    Mat4 previousModel = Mat4::Identity();
    std::shared_ptr<MeshResource> mesh;
    std::shared_ptr<MaterialResource> material;
    std::shared_ptr<TextureResource> texture;
    std::shared_ptr<TextureResource> normalTexture;
    std::shared_ptr<TextureResource> metallicTexture;
    std::shared_ptr<TextureResource> roughnessTexture;
    std::shared_ptr<TextureResource> aoTexture;
    std::shared_ptr<TextureResource> emissiveTexture;
    std::shared_ptr<TextureResource> packedTexture;
    std::vector<Mat4> skinMatrices;
    Aabb worldBounds{};
    bool transparent = false;
};

struct FrameProfile
{
    float queueMilliseconds = 0.0f;
    float shadowMilliseconds = 0.0f;
    float sceneMilliseconds = 0.0f;
    float totalMilliseconds = 0.0f;
    std::size_t graphPasses = 0;
    std::size_t graphPhysicalResources = 0;
    std::size_t clusteredLightReferences = 0;
};

enum class PostProcessPass
{
    HierarchicalDepth,
    Gtao,
    Bloom,
    TemporalResolve,
    ToneMap
};

// 渲染后端边界。所有方法都由渲染/主线程调用，具体后端拥有 GPU 资源和图形上下文。
class RenderDevice
{
  public:
    /// @brief 析构 `RenderDevice` 对象并释放其持有资源。
    virtual ~RenderDevice() = default;
    /// @brief 初始化对象及其依赖资源。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    virtual bool Initialize() = 0;
    /// @brief 停止服务并释放对象持有的资源。
    virtual void Shutdown() = 0;
    /// @brief 轮询平台事件并更新输入状态。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    virtual bool PumpEvents() = 0;
    /// @brief 获取 `InputState`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    virtual const InputState &GetInputState() const = 0;
    /// @brief 获取 `AspectRatio`。
    /// @return 返回计算得到的数值、数量或索引。
    virtual float GetAspectRatio() const = 0;
    /// @brief 获取 `ViewportSize`。
    /// @return 返回接口计算或查询得到的结果。
    virtual Vec2 GetViewportSize() const = 0;
    virtual bool UsesTemporalJitter() const
    {
        return false;
    }
    /// @brief 开始 `ShadowPass`。
    /// @param view 当前帧的相机与光照视图数据。
    /// @param cascadeIndex 目标阴影级联索引。
    virtual void BeginShadowPass(const FrameView &view, std::size_t cascadeIndex) = 0;
    /// @brief 绘制 `Shadow`。
    /// @param item 要处理的单个元素。
    virtual void DrawShadow(const RenderItem &item) = 0;
    /// @brief 绘制 `ShadowInstanced`。
    /// @param items 要处理的元素集合。
    virtual void DrawShadowInstanced(const std::vector<RenderItem> &items) = 0;
    /// @brief 结束 `ShadowPass`。
    virtual void EndShadowPass() = 0;
    /// @brief 开始一帧处理并准备帧级状态。
    /// @param view 当前帧的相机与光照视图数据。
    virtual void BeginFrame(const FrameView &view) = 0;
    /// @brief 绘制环境天空背景。
    virtual void DrawSkybox() = 0;
    /// @brief 提交或绘制当前对象。
    /// @param item 要处理的单个元素。
    virtual void Draw(const RenderItem &item) = 0;
    /// @brief 绘制 `Instanced`。
    /// @param items 要处理的元素集合。
    virtual void DrawInstanced(const std::vector<RenderItem> &items) = 0;
    /// @brief 绘制 `Particles`。
    /// @param particles 要渲染或更新的粒子集合。
    virtual void DrawParticles(const std::vector<Particle> &particles) = 0;
    /// @brief 绘制 `GpuParticles`。
    /// @param emitters 要渲染或更新的发射器集合。
    virtual void DrawGpuParticles(const std::vector<GpuParticleEmitter> &emitters) = 0;
    /// @brief 执行指定后处理通道。
    /// @param pass 要执行的渲染通道。
    virtual void ExecutePostProcess(PostProcessPass pass) = 0;
    /// @brief 结束一帧处理并提交最终结果。
    virtual void EndFrame() = 0;
    /// @brief 绘制 `Ui`。
    /// @param commands 要提交的绘制命令集合。
    virtual void DrawUi(const std::vector<UIDrawCommand> &commands) = 0;
    /// @brief 将已渲染画面提交到显示设备。
    virtual void Present() = 0;
    /// @brief 获取 `ResidentGpuBytes`。
    /// @return 返回计算得到的数值、数量或索引。
    virtual std::size_t GetResidentGpuBytes() const = 0;
    /// @brief 设置 `GpuMemoryBudget`。
    /// @param bytes 要处理的字节数据或字节数量。
    virtual void SetGpuMemoryBudget(std::size_t bytes) = 0;
    /// @brief 回收 `GpuResources`。
    virtual void TrimGpuResources() = 0;
};

// 无窗口、无 GPU 的后端，用于单元测试、服务器和命令行工具。
class NullRenderDevice final : public RenderDevice
{
  public:
    /// @brief 初始化对象及其依赖资源。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Initialize() override;
    /// @brief 停止服务并释放对象持有的资源。
    void Shutdown() override;
    /// @brief 轮询平台事件并更新输入状态。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool PumpEvents() override;
    /// @brief 获取 `InputState`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const InputState &GetInputState() const override;
    /// @brief 获取 `AspectRatio`。
    /// @return 返回计算得到的数值、数量或索引。
    float GetAspectRatio() const override;
    /// @brief 获取 `ViewportSize`。
    /// @return 返回接口计算或查询得到的结果。
    Vec2 GetViewportSize() const override;
    /// @brief 开始 `ShadowPass`。
    /// @param view 当前帧的相机与光照视图数据。
    /// @param cascadeIndex 目标阴影级联索引。
    void BeginShadowPass(const FrameView &view, std::size_t cascadeIndex) override;
    /// @brief 绘制 `Shadow`。
    /// @param item 要处理的单个元素。
    void DrawShadow(const RenderItem &item) override;
    /// @brief 绘制 `ShadowInstanced`。
    /// @param items 要处理的元素集合。
    void DrawShadowInstanced(const std::vector<RenderItem> &items) override;
    /// @brief 结束 `ShadowPass`。
    void EndShadowPass() override;
    /// @brief 开始一帧处理并准备帧级状态。
    /// @param view 当前帧的相机与光照视图数据。
    void BeginFrame(const FrameView &view) override;
    /// @brief 绘制环境天空背景。
    void DrawSkybox() override;
    /// @brief 提交或绘制当前对象。
    /// @param item 要处理的单个元素。
    void Draw(const RenderItem &item) override;
    /// @brief 绘制 `Instanced`。
    /// @param items 要处理的元素集合。
    void DrawInstanced(const std::vector<RenderItem> &items) override;
    /// @brief 绘制 `Particles`。
    /// @param particles 要渲染或更新的粒子集合。
    void DrawParticles(const std::vector<Particle> &particles) override;
    /// @brief 绘制 `GpuParticles`。
    /// @param emitters 要渲染或更新的发射器集合。
    void DrawGpuParticles(const std::vector<GpuParticleEmitter> &emitters) override;
    /// @brief 执行指定后处理通道。
    /// @param pass 要执行的渲染通道。
    void ExecutePostProcess(PostProcessPass pass) override;
    /// @brief 结束一帧处理并提交最终结果。
    void EndFrame() override;
    /// @brief 绘制 `Ui`。
    /// @param commands 要提交的绘制命令集合。
    void DrawUi(const std::vector<UIDrawCommand> &commands) override;
    /// @brief 将已渲染画面提交到显示设备。
    void Present() override;
    /// @brief 获取 `ResidentGpuBytes`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetResidentGpuBytes() const override;
    /// @brief 设置 `GpuMemoryBudget`。
    /// @param bytes 要处理的字节数据或字节数量。
    void SetGpuMemoryBudget(std::size_t bytes) override;
    /// @brief 回收 `GpuResources`。
    void TrimGpuResources() override;

  private:
    InputState m_inputState;
    std::size_t m_gpuBudget = 0;
};

// 保存一帧绘制项，并按不透明状态、材质和距离排序以兼顾批处理与透明混合正确性。
class RenderQueue
{
  public:
    /// @brief 添加 `Item`。
    /// @param item 要处理的单个元素。
    void AddItem(const RenderItem &item);
    /// @brief 按照接口约定对数据排序。
    void Sort();
    /// @brief 清空对象保存的状态和数据。
    void Clear();
    /// @brief 获取 `Items`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<RenderItem> &GetItems() const;

  private:
    std::vector<RenderItem> m_items;
};

// 高层渲染编排器：从 World 构建可见集合，再通过 RenderGraph 驱动阴影、场景和后处理。
class Renderer
{
  public:
    /// @brief 设置 `Logger`。
    /// @param logger 用于输出诊断信息的日志服务。
    void SetLogger(Logger *logger);
    /// @brief 设置 `ResourceManager`。
    /// @param resources 相关文件、资源或目录路径。
    void SetResourceManager(ResourceManager *resources);
    /// @brief 初始化对象及其依赖资源。
    /// @param device 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Initialize(std::unique_ptr<RenderDevice> device);
    /// @brief 停止服务并释放对象持有的资源。
    void Shutdown();
    /// @brief 轮询平台事件并更新输入状态。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool PumpEvents();
    /// @brief 获取 `InputState`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const InputState &GetInputState() const;
    /// @brief 构建 `RenderQueue`。
    /// @param world 要读取或修改的 World。
    void BuildRenderQueue(World &world);
    /// @brief 渲染 `Frame`。
    /// @param world 要读取或修改的 World。
    void RenderFrame(World &world);
    /// @brief 获取 `LastDrawCallCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetLastDrawCallCount() const;
    /// @brief 获取 `FrameView`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const FrameView &GetFrameView() const;
    /// @brief 获取 `Device`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    RenderDevice *GetDevice();
    /// @brief 设置 `OverlayCallback`。
    /// @param callback 满足条件时调用的回调函数。
    void SetOverlayCallback(std::function<void()> callback);
    /// @brief 设置 `UISystem`。
    /// @param uiSystem 接口所需的输入参数。
    void SetUISystem(UISystem *uiSystem);
    /// @brief 设置 `VfxSystem`。
    /// @param vfxSystem 接口所需的输入参数。
    void SetVfxSystem(VfxSystem *vfxSystem);
    /// @brief 获取 `LastCulledCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetLastCulledCount() const;
    /// @brief 获取 `LastLodSwitchCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetLastLodSwitchCount() const;
    /// @brief 获取 `LastFrameProfile`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const FrameProfile &GetLastFrameProfile() const;
    /// @brief 获取 `LastRenderGraph`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const RenderGraph &GetLastRenderGraph() const;
    /// @brief 设置 `CameraOverride`。
    /// @param enabled 是否启用对应功能。
    /// @param position 世界空间或局部空间位置。
    /// @param rotation 旋转值，单位与空间由接口约定。
    /// @param fov 本次操作使用的数值。
    void SetCameraOverride(bool enabled, const Vec3 &position = {}, const Vec3 &rotation = {}, float fov = 60.0f);
    /// @brief 设置 `EnvironmentPath`。
    /// @param path 文件、资源或目录路径。
    void SetEnvironmentPath(std::string path);

  private:
    Logger *m_logger = nullptr;
    ResourceManager *m_resources = nullptr;
    std::unique_ptr<RenderDevice> m_device;
    RenderQueue m_renderQueue;
    std::vector<RenderItem> m_shadowItems;
    FrameView m_frameView;
    std::size_t m_lastDrawCalls = 0;
    std::size_t m_lastCulled = 0;
    std::size_t m_lastLodSwitches = 0;
    std::function<void()> m_overlayCallback;
    UISystem *m_uiSystem = nullptr;
    VfxSystem *m_vfxSystem = nullptr;
    bool m_cameraOverrideEnabled = false;
    Vec3 m_cameraOverridePosition{0.0f, 2.5f, 7.0f};
    Vec3 m_cameraOverrideRotation{-10.0f, 0.0f, 0.0f};
    float m_cameraOverrideFov = 60.0f;
    std::string m_environmentPath;
    std::string m_environmentSkyPath;
    std::string m_environmentIrradiancePath;
    std::string m_environmentPrefilterPath;
    std::string m_environmentBrdfPath;
    FrameProfile m_lastFrameProfile;
    RenderGraph m_renderGraph;
    Mat4 m_previousViewProjection = Mat4::Identity();
    Mat4 m_previousUnjitteredViewProjection = Mat4::Identity();
    Mat4 m_previousProjection = Mat4::Identity();
    Vec3 m_previousCameraPosition{}, m_previousCameraForward{0, 0, -1};
    bool m_previousCameraOverride = false;
    bool m_hasPreviousView = false;
    std::unordered_map<Entity, Mat4> m_previousModels;
    std::uint64_t m_frameNumber = 0;
};
} // namespace MiniEngine
