// 文件：include/tinyengine/render/renderer.h
// 作用：组织相机、可见性、光照、阴影、后处理和 UI 的逐帧渲染。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：公开声明。

#pragma once

#include "tinyengine/math/math.h"
#include "tinyengine/render/clustered_lighting.h"
#include "tinyengine/render/material_system.h"
#include "tinyengine/render/render_graph.h"
#include "tinyengine/resource/resource.h"
#include "tinyengine/types.h"
#include "tinyengine/ui/ui_system.h"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tinyengine
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
    bool primaryPressed = false;
    bool sprint = false;
    bool jump = false;
    bool quitRequested = false;
    bool uiNavigateNext = false;
    bool uiAccept = false;
    bool uiBackspace = false;
    std::string textInput;
    float cursorX = 0.0f, cursorY = 0.0f;
    // 原始键位快照与本帧按下沿。输入映射只覆盖引擎既有动作，游戏需要换弹、暂停这类
    // 未映射按键时直接读 scancode；默认全为 false，后端不支持按键表时仍可运行。
    static constexpr std::size_t kMaxScancodes = 512;
    std::array<bool, kMaxScancodes> keysDown{};
    std::array<bool, kMaxScancodes> keysPressed{};
};

enum class ViewportRenderMode
{
    Lit,
    Unlit,
    Wireframe
};

// 一帧共享的视图常量。previousViewProjection 与 temporalJitter 共同支持运动矢量和 TAA。
struct FrameView
{
    ViewportRenderMode renderMode = ViewportRenderMode::Lit;
    static constexpr std::size_t kMaxLights = 64;
    static constexpr std::size_t kMaxShadowCascades = 3;
    static constexpr int kShadowMapResolution = 2048;
    Mat4 view = Mat4::identity();
    Mat4 projection = Mat4::identity();
    Mat4 previousViewProjection = Mat4::identity();
    Vec2 temporalJitter{};
    bool occlusionHistoryValid = false;
    bool resetTemporalHistory = true;
    Vec3 cameraPosition{0.0f, 0.0f, 5.0f};
    Vec3 lightDirection{-0.4f, -1.0f, -0.3f};
    Vec3 lightColor{1.0f, 1.0f, 1.0f};
    float lightIntensity = 1.0f;
    Mat4 lightViewProjection = Mat4::identity();
    std::array<Mat4, kMaxShadowCascades> shadowViewProjections{};
    std::array<float, kMaxShadowCascades> shadowCascadeSplits{};
    std::array<RenderLight, kMaxLights> lights{};
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
    Entity entity = kInvalidEntity;
    float distanceToCamera = 0.0f;
    Mat4 model = Mat4::identity();
    Mat4 previousModel = Mat4::identity();
    std::shared_ptr<MeshResource> mesh;
    std::shared_ptr<MaterialResource> material;
    std::shared_ptr<const MaterialRenderProxy> materialProxy;
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
    ToneMap,
    // 追加在尾部：既有枚举值保持稳定，旧后端即使不实现也不会错位。
    Pixelate
};

// 渲染后端边界。所有方法都由渲染/主线程调用，具体后端拥有 GPU 资源和图形上下文。
class RenderDevice
{
  public:
    /// @brief 析构 `RenderDevice` 对象并释放其持有资源。
    virtual ~RenderDevice() = default;
    virtual bool validateMaterial(const CompiledMaterial&, std::vector<MaterialDiagnostic>&)
    {
        return true;
    }
    /// @brief 初始化对象及其依赖资源。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    virtual bool initialize() = 0;
    /// @brief 停止服务并释放对象持有的资源。
    virtual void shutdown() = 0;
    /// @brief 轮询平台事件并更新输入状态。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    virtual bool pumpEvents() = 0;
    /// @brief 获取 `InputState`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    virtual const InputState& GetInputState() const = 0;
    /// @brief 获取 `AspectRatio`。
    /// @return 返回计算得到的数值、数量或索引。
    virtual float getAspectRatio() const = 0;
    /// @brief 获取 `ViewportSize`。
    /// @return 返回接口计算或查询得到的结果。
    virtual Vec2 getViewportSize() const = 0;
    // 独占鼠标（相对移动模式）由游戏在进入战斗时开启、暂停或退出时关闭。
    // 默认实现不改变平台状态，无窗口后端因此可以忽略该请求。
    /// @brief 设置是否独占鼠标。
    /// @param captured 是否捕获鼠标。
    virtual void setMouseCapture(bool captured)
    {
        (void)captured;
    }
    /// @brief 判断当前是否独占鼠标。
    /// @return 捕获状态；默认实现返回 `false`。
    virtual bool isMouseCaptured() const
    {
        return false;
    }
    /// @brief 开关复古像素化后处理通道。
    /// @param enabled 是否启用。
    /// @param blockSize 像素块边长（像素）。
    /// @param colorLevels 每通道色阶数。
    /// @note 默认实现忽略请求：无窗口后端不需要该通道，调用方应据 isRetroPostProcessEnabled 判断实际状态。
    virtual void setRetroPostProcess(bool enabled, float blockSize, float colorLevels)
    {
        (void)enabled;
        (void)blockSize;
        (void)colorLevels;
    }
    /// @brief 判断像素化通道是否真的可用。
    /// @return 默认实现返回 `false`。
    virtual bool isRetroPostProcessEnabled() const
    {
        return false;
    }
    /// @brief 获取当前配置的像素块边长，单位为像素。
    virtual float getRetroBlockSize() const
    {
        return 3.0f;
    }
    /// @brief 获取每通道色阶数量。
    virtual float getRetroColorLevels() const
    {
        return 32.0f;
    }
    virtual bool usesTemporalJitter() const
    {
        return false;
    }
    /// @brief 开始 `ShadowPass`。
    /// @param view 当前帧的相机与光照视图数据。
    /// @param cascadeIndex 目标阴影级联索引。
    virtual void beginShadowPass(const FrameView& view, std::size_t cascadeIndex) = 0;
    /// @brief 绘制 `Shadow`。
    /// @param item 要处理的单个元素。
    virtual void drawShadow(const RenderItem& item) = 0;
    /// @brief 绘制 `ShadowInstanced`。
    /// @param items 要处理的元素集合。
    virtual void drawShadowInstanced(const std::vector<RenderItem>& items) = 0;
    /// @brief 结束 `ShadowPass`。
    virtual void endShadowPass() = 0;
    /// @brief 开始一帧处理并准备帧级状态。
    /// @param view 当前帧的相机与光照视图数据。
    virtual void beginFrame(const FrameView& view) = 0;
    /// @brief 绘制环境天空背景。
    virtual void drawSkybox() = 0;
    /// @brief 提交或绘制当前对象。
    /// @param item 要处理的单个元素。
    virtual void draw(const RenderItem& item) = 0;
    /// @brief 绘制 `Instanced`。
    /// @param items 要处理的元素集合。
    virtual void drawInstanced(const std::vector<RenderItem>& items) = 0;
    /// @brief 绘制 `Particles`。
    /// @param particles 要渲染或更新的粒子集合。
    virtual void drawParticles(const std::vector<Particle>& particles) = 0;
    /// @brief 绘制 `GpuParticles`。
    /// @param emitters 要渲染或更新的发射器集合。
    virtual void drawGpuParticles(const std::vector<GpuParticleEmitter>& emitters) = 0;
    /// @brief 执行指定后处理通道。
    /// @param pass 要执行的渲染通道。
    virtual void executePostProcess(PostProcessPass pass) = 0;
    /// @brief 结束一帧处理并提交最终结果。
    virtual void endFrame() = 0;
    /// @brief 绘制 `Ui`。
    /// @param commands 要提交的绘制命令集合。
    virtual void drawUi(const std::vector<UIDrawCommand>& commands) = 0;
    /// @brief 将已渲染画面提交到显示设备。
    virtual void present() = 0;
    /// @brief 获取 `ResidentGpuBytes`。
    /// @return 返回计算得到的数值、数量或索引。
    virtual std::size_t getResidentGpuBytes() const = 0;
    /// @brief 设置 `GpuMemoryBudget`。
    /// @param bytes 要处理的字节数据或字节数量。
    virtual void setGpuMemoryBudget(std::size_t bytes) = 0;
    /// @brief 回收 `GpuResources`。
    virtual void trimGpuResources() = 0;
};

// 无窗口、无 GPU 的后端，用于单元测试、服务器和命令行工具。
class NullRenderDevice final : public RenderDevice
{
  public:
    /// @brief 初始化对象及其依赖资源。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool initialize() override;
    /// @brief 停止服务并释放对象持有的资源。
    void shutdown() override;
    /// @brief 轮询平台事件并更新输入状态。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool pumpEvents() override;
    /// @brief 获取 `InputState`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const InputState& GetInputState() const override;
    /// @brief 获取 `AspectRatio`。
    /// @return 返回计算得到的数值、数量或索引。
    float getAspectRatio() const override;
    /// @brief 获取 `ViewportSize`。
    /// @return 返回接口计算或查询得到的结果。
    Vec2 getViewportSize() const override;
    /// @brief 开始 `ShadowPass`。
    /// @param view 当前帧的相机与光照视图数据。
    /// @param cascadeIndex 目标阴影级联索引。
    void beginShadowPass(const FrameView& view, std::size_t cascadeIndex) override;
    /// @brief 绘制 `Shadow`。
    /// @param item 要处理的单个元素。
    void drawShadow(const RenderItem& item) override;
    /// @brief 绘制 `ShadowInstanced`。
    /// @param items 要处理的元素集合。
    void drawShadowInstanced(const std::vector<RenderItem>& items) override;
    /// @brief 结束 `ShadowPass`。
    void endShadowPass() override;
    /// @brief 开始一帧处理并准备帧级状态。
    /// @param view 当前帧的相机与光照视图数据。
    void beginFrame(const FrameView& view) override;
    /// @brief 绘制环境天空背景。
    void drawSkybox() override;
    /// @brief 提交或绘制当前对象。
    /// @param item 要处理的单个元素。
    void draw(const RenderItem& item) override;
    /// @brief 绘制 `Instanced`。
    /// @param items 要处理的元素集合。
    void drawInstanced(const std::vector<RenderItem>& items) override;
    /// @brief 绘制 `Particles`。
    /// @param particles 要渲染或更新的粒子集合。
    void drawParticles(const std::vector<Particle>& particles) override;
    /// @brief 绘制 `GpuParticles`。
    /// @param emitters 要渲染或更新的发射器集合。
    void drawGpuParticles(const std::vector<GpuParticleEmitter>& emitters) override;
    /// @brief 执行指定后处理通道。
    /// @param pass 要执行的渲染通道。
    void executePostProcess(PostProcessPass pass) override;
    /// @brief 结束一帧处理并提交最终结果。
    void endFrame() override;
    /// @brief 绘制 `Ui`。
    /// @param commands 要提交的绘制命令集合。
    void drawUi(const std::vector<UIDrawCommand>& commands) override;
    /// @brief 将已渲染画面提交到显示设备。
    void present() override;
    /// @brief 获取 `ResidentGpuBytes`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getResidentGpuBytes() const override;
    /// @brief 设置 `GpuMemoryBudget`。
    /// @param bytes 要处理的字节数据或字节数量。
    void setGpuMemoryBudget(std::size_t bytes) override;
    /// @brief 回收 `GpuResources`。
    void trimGpuResources() override;

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
    void addItem(const RenderItem& item);
    /// @brief 按照接口约定对数据排序。
    void sort();
    /// @brief 清空对象保存的状态和数据。
    void clear();
    /// @brief 获取 `Items`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<RenderItem>& GetItems() const;

  private:
    std::vector<RenderItem> m_items;
};

// 高层渲染编排器：从 World 构建可见集合，再通过 RenderGraph 驱动阴影、场景和后处理。
class Renderer
{
  public:
    /// @brief 设置 `Logger`。
    /// @param logger 用于输出诊断信息的日志服务。
    void setLogger(Logger* logger);
    /// @brief 设置 `ResourceManager`。
    /// @param resources 相关文件、资源或目录路径。
    void setResourceManager(ResourceManager* resources);
    MaterialSystem& GetMaterialSystem()
    {
        return *m_materialSystem;
    }
    void setDynamicMaterial(Entity entity, std::shared_ptr<DynamicMaterialInstance> instance);
    /// @brief 初始化对象及其依赖资源。
    /// @param device 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool initialize(std::unique_ptr<RenderDevice> device);
    /// @brief 停止服务并释放对象持有的资源。
    void shutdown();
    /// @brief 轮询平台事件并更新输入状态。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool pumpEvents();
    /// @brief 获取 `InputState`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const InputState& GetInputState() const;
    /// @brief 构建 `RenderQueue`。
    /// @param world 要读取或修改的 World。
    void buildRenderQueue(World& world);
    /// @brief 渲染 `Frame`。
    /// @param world 要读取或修改的 World。
    void renderFrame(World& world);
    /// @brief 获取 `LastDrawCallCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getLastDrawCallCount() const;
    /// @brief 获取 `FrameView`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const FrameView& GetFrameView() const;
    /// @brief 获取 `Device`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    RenderDevice* getDevice();
    /// @brief 请求独占鼠标；设备缺失时静默忽略。
    /// @param captured 是否捕获鼠标。
    void setMouseCapture(bool captured);
    /// @brief 判断当前是否独占鼠标。
    /// @return 捕获状态；无设备时返回 `false`。
    bool isMouseCaptured() const;
    /// @brief 开关复古像素化后处理；设备不支持时静默忽略。
    /// @param enabled 是否启用。
    /// @param blockSize 像素块边长（像素）。
    /// @param colorLevels 每通道色阶数。
    void setRetroPostProcess(bool enabled, float blockSize, float colorLevels);
    /// @brief 判断像素化通道在当前设备上是否生效。
    bool isRetroPostProcessEnabled() const;
    float getRetroBlockSize() const;
    float getRetroColorLevels() const;
    /// @brief 设置 `OverlayCallback`。
    /// @param callback 满足条件时调用的回调函数。
    void setOverlayCallback(std::function<void()> callback);
    /// @brief 设置 `UISystem`。
    /// @param uiSystem 接口所需的输入参数。
    void setUiSystem(UISystem* uiSystem);
    /// @brief 设置 `VfxSystem`。
    /// @param vfxSystem 接口所需的输入参数。
    void setVfxSystem(VfxSystem* vfxSystem);
    /// @brief 获取 `LastCulledCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getLastCulledCount() const;
    /// @brief 获取 `LastLodSwitchCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getLastLodSwitchCount() const;
    /// @brief 获取 `LastFrameProfile`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const FrameProfile& GetLastFrameProfile() const;
    /// @brief 获取 `LastRenderGraph`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const RenderGraph& GetLastRenderGraph() const;
    /// @brief 设置 `CameraOverride`。
    /// @param enabled 是否启用对应功能。
    /// @param position 世界空间或局部空间位置。
    /// @param rotation 旋转值，单位与空间由接口约定。
    /// @param fov 本次操作使用的数值。
    void setCameraOverride(bool enabled, const Vec3& position = {}, const Vec3& rotation = {}, float fov = 60.0f);
    void setViewportRenderMode(ViewportRenderMode mode);
    /// @brief 设置 `EnvironmentPath`。
    /// @param path 文件、资源或目录路径。
    void setEnvironmentPath(std::string path);

  private:
    Logger* m_logger = nullptr;
    ResourceManager* m_resources = nullptr;
    std::unique_ptr<MaterialSystem> m_materialSystem;
    std::unordered_map<Entity, std::shared_ptr<DynamicMaterialInstance>> m_dynamicMaterials;
    std::unique_ptr<RenderDevice> m_device;
    RenderQueue m_renderQueue;
    std::vector<RenderItem> m_shadowItems;
    FrameView m_frameView;
    ViewportRenderMode m_viewportRenderMode = ViewportRenderMode::Lit;
    std::size_t m_lastDrawCalls = 0;
    std::size_t m_lastCulled = 0;
    std::size_t m_lastLodSwitches = 0;
    std::function<void()> m_overlayCallback;
    UISystem* m_uiSystem = nullptr;
    VfxSystem* m_vfxSystem = nullptr;
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
    Mat4 m_previousViewProjection = Mat4::identity();
    Mat4 m_previousUnjitteredViewProjection = Mat4::identity();
    Mat4 m_previousProjection = Mat4::identity();
    Vec3 m_previousCameraPosition{}, m_previousCameraForward{0, 0, -1};
    bool m_previousCameraOverride = false;
    bool m_hasPreviousView = false;
    std::unordered_map<Entity, Mat4> m_previousModels;
    std::uint64_t m_frameNumber = 0;
};
} // namespace tinyengine
