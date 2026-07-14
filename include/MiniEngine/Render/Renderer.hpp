#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Resource/Resource.hpp"
#include "MiniEngine/Types.hpp"
#include "MiniEngine/UI/UISystem.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MiniEngine
{
class Logger;
class ResourceManager;
class World;

struct InputState
{
    float moveX = 0.0f, moveY = 0.0f, moveZ = 0.0f;
    float rotateX = 0.0f, rotateY = 0.0f, rotateZ = 0.0f;
    float scale = 0.0f;
    float mouseX = 0.0f, mouseY = 0.0f;
    bool primaryAction = false;
    bool sprint = false;
    bool quitRequested = false;
    bool uiNavigateNext = false;
    bool uiAccept = false;
    float cursorX = 0.0f, cursorY = 0.0f;
};

struct FrameView
{
    static constexpr std::size_t MaxLights = 4;
    Mat4 view = Mat4::Identity();
    Mat4 projection = Mat4::Identity();
    Vec3 cameraPosition{0.0f, 0.0f, 5.0f};
    Vec3 lightDirection{-0.4f, -1.0f, -0.3f};
    Vec3 lightColor{1.0f, 1.0f, 1.0f};
    float lightIntensity = 1.0f;
    Mat4 lightViewProjection = Mat4::Identity();
    std::array<Vec3, MaxLights> lightDirections{};
    std::array<Vec3, MaxLights> lightColors{};
    std::array<float, MaxLights> lightIntensities{};
    int lightCount = 0;
    std::shared_ptr<TextureResource> environmentTexture;
};

struct RenderItem
{
    Entity entity = InvalidEntity;
    float distanceToCamera = 0.0f;
    Mat4 model = Mat4::Identity();
    std::shared_ptr<MeshResource> mesh;
    std::shared_ptr<MaterialResource> material;
    std::shared_ptr<TextureResource> texture;
    std::shared_ptr<TextureResource> normalTexture;
    std::shared_ptr<TextureResource> metallicTexture;
    std::shared_ptr<TextureResource> roughnessTexture;
    std::shared_ptr<TextureResource> aoTexture;
    std::shared_ptr<TextureResource> emissiveTexture;
    bool transparent = false;
};

struct FrameProfile
{
    float queueMilliseconds = 0.0f;
    float shadowMilliseconds = 0.0f;
    float sceneMilliseconds = 0.0f;
    float totalMilliseconds = 0.0f;
};

class RenderDevice
{
  public:
    virtual ~RenderDevice() = default;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual bool PumpEvents() = 0;
    virtual const InputState &GetInputState() const = 0;
    virtual float GetAspectRatio() const = 0;
    virtual Vec2 GetViewportSize() const = 0;
    virtual void BeginShadowPass(const FrameView &view) = 0;
    virtual void DrawShadow(const RenderItem &item) = 0;
    virtual void EndShadowPass() = 0;
    virtual void BeginFrame(const FrameView &view) = 0;
    virtual void Draw(const RenderItem &item) = 0;
    virtual void EndFrame() = 0;
    virtual void DrawUi(const std::vector<UIDrawCommand> &commands) = 0;
    virtual void Present() = 0;
};

class NullRenderDevice final : public RenderDevice
{
  public:
    bool Initialize() override;
    void Shutdown() override;
    bool PumpEvents() override;
    const InputState &GetInputState() const override;
    float GetAspectRatio() const override;
    Vec2 GetViewportSize() const override;
    void BeginShadowPass(const FrameView &view) override;
    void DrawShadow(const RenderItem &item) override;
    void EndShadowPass() override;
    void BeginFrame(const FrameView &view) override;
    void Draw(const RenderItem &item) override;
    void EndFrame() override;
    void DrawUi(const std::vector<UIDrawCommand> &commands) override;
    void Present() override;

  private:
    InputState m_inputState;
};

class RenderQueue
{
  public:
    void AddItem(const RenderItem &item);
    void Sort();
    void Clear();
    const std::vector<RenderItem> &GetItems() const;

  private:
    std::vector<RenderItem> m_items;
};

class Renderer
{
  public:
    void SetLogger(Logger *logger);
    void SetResourceManager(ResourceManager *resources);
    bool Initialize(std::unique_ptr<RenderDevice> device);
    void Shutdown();
    bool PumpEvents();
    const InputState &GetInputState() const;
    void BuildRenderQueue(World &world);
    void RenderFrame(World &world);
    std::size_t GetLastDrawCallCount() const;
    const FrameView &GetFrameView() const;
    RenderDevice *GetDevice();
    void SetOverlayCallback(std::function<void()> callback);
    void SetUISystem(UISystem *uiSystem);
    std::size_t GetLastCulledCount() const;
    std::size_t GetLastLodSwitchCount() const;
    const FrameProfile &GetLastFrameProfile() const;
    void SetCameraOverride(bool enabled, const Vec3 &position = {}, const Vec3 &rotation = {}, float fov = 60.0f);
    void SetEnvironmentPath(std::string path);

  private:
    Logger *m_logger = nullptr;
    ResourceManager *m_resources = nullptr;
    std::unique_ptr<RenderDevice> m_device;
    RenderQueue m_renderQueue;
    FrameView m_frameView;
    std::size_t m_lastDrawCalls = 0;
    std::size_t m_lastCulled = 0;
    std::size_t m_lastLodSwitches = 0;
    std::function<void()> m_overlayCallback;
    UISystem *m_uiSystem = nullptr;
    bool m_cameraOverrideEnabled = false;
    Vec3 m_cameraOverridePosition{0.0f, 2.5f, 7.0f};
    Vec3 m_cameraOverrideRotation{-10.0f, 0.0f, 0.0f};
    float m_cameraOverrideFov = 60.0f;
    std::string m_environmentPath;
    FrameProfile m_lastFrameProfile;
};
} // namespace MiniEngine
