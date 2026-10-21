// 文件：include/MiniEngine/Render/SdlGlRenderDevice.hpp
// 作用：实现基于 SDL2 与 OpenGL 的渲染设备、窗口输入和 GPU 资源提交。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：公开声明。

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
    /// @brief 构造 `SdlGlRenderDevice` 对象。
    /// @param width 目标宽度。
    /// @param height 目标高度。
    /// @param title 接口所需的输入参数。
    SdlGlRenderDevice(int width = 1280, int height = 720, std::string title = "Aster-Engine SDL2 + OpenGL");
    /// @brief 析构 `SdlGlRenderDevice` 对象并释放其持有资源。
    ~SdlGlRenderDevice() override;

    /// @brief 初始化对象及其依赖资源。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Initialize() override;
    bool ValidateMaterial(const CompiledMaterial &, std::vector<MaterialDiagnostic> &) override;
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
    /// @brief 获取 `NativeWindow`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    void *GetNativeWindow() const;
    /// @brief 获取 `GlContext`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    void *GetGlContext() const;
    /// @brief 设置 `EventCallback`。
    /// @param callback 满足条件时调用的回调函数。
    void SetEventCallback(std::function<void(const void *)> callback);
    /// @brief 只在需要编辑文字时启用系统输入法，避免游戏按键被输入法接管。
    void SetTextInputEnabled(bool enabled);
    /// @brief 设置窗口关闭请求处理器；未设置时保持运行时直接退出行为。
    void SetCloseRequestCallback(std::function<void()> callback);
    /// @brief 启用 `SceneRenderTarget`。
    /// @param enabled 是否启用对应功能。
    /// @param width 目标宽度。
    /// @param height 目标高度。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool EnableSceneRenderTarget(bool enabled, int width = 960, int height = 540);
    /// @brief 调整尺寸 `SceneRenderTarget`。
    /// @param width 目标宽度。
    /// @param height 目标高度。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ResizeSceneRenderTarget(int width, int height);
    /// @brief 获取 `SceneColorTexture`。
    /// @return 返回计算得到的数值、数量或索引。
    std::uintptr_t GetSceneColorTexture() const;
    /// @brief 获取 `UiTexture`。
    /// @param path 文件、资源或目录路径。
    /// @return 返回计算得到的数值、数量或索引。
    std::uintptr_t GetUiTexture(const std::string &path);
    /// @brief 获取 `SceneTargetWidth`。
    /// @return 返回计算得到的数值、数量或索引。
    int GetSceneTargetWidth() const;
    /// @brief 获取 `SceneTargetHeight`。
    /// @return 返回计算得到的数值、数量或索引。
    int GetSceneTargetHeight() const;
    /// @brief 重新加载 `Shaders`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool ReloadShaders();
    /// @brief 验证 `Shader`。
    /// @param path 文件、资源或目录路径。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool ValidateShader(const std::string &path);
    /// @brief 设置 `Exposure`。
    /// @param exposure 本次操作使用的数值。
    void SetExposure(float exposure);
    /// @brief 获取 `Exposure`。
    /// @return 返回计算得到的数值、数量或索引。
    float GetExposure() const;
    /// @brief 获取 `LastShaderError`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetLastShaderError() const;
    /// @brief 设置 `FxaaEnabled`。
    /// @param enabled 是否启用对应功能。
    void SetFxaaEnabled(bool enabled);
    /// @brief 判断是否满足 `FxaaEnabled` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsFxaaEnabled() const;
    /// @brief 设置 `BloomEnabled`。
    /// @param enabled 是否启用对应功能。
    void SetBloomEnabled(bool enabled);
    /// @brief 判断是否满足 `BloomEnabled` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsBloomEnabled() const;
    /// @brief 设置 `SsaoEnabled`。
    /// @param enabled 是否启用对应功能。
    void SetSsaoEnabled(bool enabled);
    /// @brief 判断是否满足 `SsaoEnabled` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsSsaoEnabled() const;
    /// @brief 设置 `GtaoEnabled`。
    /// @param enabled 是否启用对应功能。
    void SetGtaoEnabled(bool enabled);
    /// @brief 判断是否满足 `GtaoEnabled` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsGtaoEnabled() const;
    /// @brief 设置 `TaaEnabled`。
    /// @param enabled 是否启用对应功能。
    void SetTaaEnabled(bool enabled);
    /// @brief 判断是否满足 `TaaEnabled` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsTaaEnabled() const;
    bool UsesTemporalJitter() const override { return IsTaaEnabled(); }
    /// @brief 获取 `LastGpuFrameMilliseconds`。
    /// @return 返回计算得到的数值、数量或索引。
    float GetLastGpuFrameMilliseconds() const;
    /// @brief 获取 `HzbLevelCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetHzbLevelCount() const;
    /// @brief 判断是否满足 `IndirectDrawSupported` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsIndirectDrawSupported() const;
    /// @brief 判断是否满足 `GpuDrivenCullingSupported` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsGpuDrivenCullingSupported() const;
    /// @brief 触发震动 `Controller`。
    /// @param lowFrequency 接口所需的输入参数。
    /// @param highFrequency 接口所需的输入参数。
    /// @param durationMilliseconds 时间值，单位按接口约定为秒。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool RumbleController(float lowFrequency, float highFrequency, std::uint32_t durationMilliseconds);
    /// @brief 判断是否满足 `ControllerConnected` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsControllerConnected() const;
    /// @brief 设置是否允许 ESC 键退出主循环。
    /// @param enabled 是否启用对应行为。
    void SetEscapeQuits(bool enabled);
    /// @brief 获取 `DpiScale`。
    /// @return 返回当前显示 DPI 相对 96 的缩放比例。
    float GetDpiScale() const;
#ifdef TINYENGINE_TEST_HOOKS
    std::vector<Vec4> ReadHdrAttachmentForTests(std::size_t attachment = 0) const;
    std::size_t GetMaterialProgramCompileCountForTests(MaterialPass pass = MaterialPass::Base) const;
    const std::vector<MaterialDiagnostic> &GetMaterialDiagnosticsForTests() const;
    float ReadShadowDepthForTests(int x,int y,std::size_t cascade=0) const;
#endif

  private:
    struct Impl;

  private:
    std::unique_ptr<Impl> m_impl;
    int m_width;
    int m_height;
    std::string m_title;
    float m_aspect = 16.0f / 9.0f;
    float m_dpiScale = 1.0f;
    bool m_escapeQuits = true;
    InputState m_inputState;
    std::function<void(const void *)> m_eventCallback;
    std::function<void()> m_closeRequestCallback;
};
} // 结束 MiniEngine 命名空间
