// 文件：include/tinyengine/render/sdl_gl_render_device.h
// 作用：实现基于 SDL2 与 OpenGL 的渲染设备、窗口输入和 GPU 资源提交。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：公开声明。

#pragma once

#include "tinyengine/render/renderer.h"

#include <functional>
#include <memory>
#include <string>

namespace tinyengine
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
    bool initialize() override;
    bool validateMaterial(const CompiledMaterial&, std::vector<MaterialDiagnostic>&) override;
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
    /// @brief 切换独占鼠标；开启时隐藏光标并使用 SDL 相对鼠标模式。
    /// @param captured 是否捕获鼠标。
    void setMouseCapture(bool captured) override;
    /// @brief 判断当前是否独占鼠标。
    /// @return 捕获状态。
    bool isMouseCaptured() const override;
    /// @brief 开关复古像素化后处理通道。
    /// @param enabled 是否启用。
    /// @param blockSize 像素块边长（像素，夹紧到 1..16）。
    /// @param colorLevels 每通道色阶数（夹紧到 2..256）。
    void setRetroPostProcess(bool enabled, float blockSize, float colorLevels) override;
    /// @brief 判断像素化通道是否可用且已启用。
    bool isRetroPostProcessEnabled() const override;
    float getRetroBlockSize() const override;
    float getRetroColorLevels() const override;
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
    /// @brief 获取 `NativeWindow`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    void* getNativeWindow() const;
    /// @brief 获取 `GlContext`。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    void* getGlContext() const;
    /// @brief 设置 `EventCallback`。
    /// @param callback 满足条件时调用的回调函数。
    void setEventCallback(std::function<void(const void*)> callback);
    /// @brief 只在需要编辑文字时启用系统输入法，避免游戏按键被输入法接管。
    void setTextInputEnabled(bool enabled);
    /// @brief 设置窗口关闭请求处理器；未设置时保持运行时直接退出行为。
    void setCloseRequestCallback(std::function<void()> callback);
    /// @brief 启用 `SceneRenderTarget`。
    /// @param enabled 是否启用对应功能。
    /// @param width 目标宽度。
    /// @param height 目标高度。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool enableSceneRenderTarget(bool enabled, int width = 960, int height = 540);
    /// @brief 调整尺寸 `SceneRenderTarget`。
    /// @param width 目标宽度。
    /// @param height 目标高度。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool resizeSceneRenderTarget(int width, int height);
    /// @brief 获取 `SceneColorTexture`。
    /// @return 返回计算得到的数值、数量或索引。
    std::uintptr_t getSceneColorTexture() const;
    /// @brief 获取 `UiTexture`。
    /// @param path 文件、资源或目录路径。
    /// @return 返回计算得到的数值、数量或索引。
    std::uintptr_t getUiTexture(const std::string& path);
    /// @brief 获取 `SceneTargetWidth`。
    /// @return 返回计算得到的数值、数量或索引。
    int getSceneTargetWidth() const;
    /// @brief 获取 `SceneTargetHeight`。
    /// @return 返回计算得到的数值、数量或索引。
    int getSceneTargetHeight() const;
    /// @brief 重新加载 `Shaders`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool reloadShaders();
    /// @brief 验证 `Shader`。
    /// @param path 文件、资源或目录路径。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool validateShader(const std::string& path);
    /// @brief 设置 `Exposure`。
    /// @param exposure 本次操作使用的数值。
    void setExposure(float exposure);
    /// @brief 获取 `Exposure`。
    /// @return 返回计算得到的数值、数量或索引。
    float getExposure() const;
    /// @brief 获取 `LastShaderError`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string& GetLastShaderError() const;
    /// @brief 设置 `FxaaEnabled`。
    /// @param enabled 是否启用对应功能。
    void setFxaaEnabled(bool enabled);
    /// @brief 判断是否满足 `FxaaEnabled` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isFxaaEnabled() const;
    /// @brief 设置 `BloomEnabled`。
    /// @param enabled 是否启用对应功能。
    void setBloomEnabled(bool enabled);
    /// @brief 判断是否满足 `BloomEnabled` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isBloomEnabled() const;
    /// @brief 设置 `SsaoEnabled`。
    /// @param enabled 是否启用对应功能。
    void setSsaoEnabled(bool enabled);
    /// @brief 判断是否满足 `SsaoEnabled` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isSsaoEnabled() const;
    /// @brief 设置 `GtaoEnabled`。
    /// @param enabled 是否启用对应功能。
    void setGtaoEnabled(bool enabled);
    /// @brief 判断是否满足 `GtaoEnabled` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isGtaoEnabled() const;
    /// @brief 设置 `TaaEnabled`。
    /// @param enabled 是否启用对应功能。
    void setTaaEnabled(bool enabled);
    /// @brief 判断是否满足 `TaaEnabled` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isTaaEnabled() const;
    bool usesTemporalJitter() const override
    {
        // Pixel quantization needs a fixed raster grid; subpixel Halton offsets
        // become whole pixel-block jumps after nearest sampling and quantization.
        return isTaaEnabled() && !isRetroPostProcessEnabled();
    }
    /// @brief 获取 `LastGpuFrameMilliseconds`。
    /// @return 返回计算得到的数值、数量或索引。
    float getLastGpuFrameMilliseconds() const;
    /// @brief 获取 `HzbLevelCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getHzbLevelCount() const;
    /// @brief 判断是否满足 `IndirectDrawSupported` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isIndirectDrawSupported() const;
    /// @brief 判断是否满足 `GpuDrivenCullingSupported` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isGpuDrivenCullingSupported() const;
    /// @brief 触发震动 `Controller`。
    /// @param lowFrequency 接口所需的输入参数。
    /// @param highFrequency 接口所需的输入参数。
    /// @param durationMilliseconds 时间值，单位按接口约定为秒。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool rumbleController(float lowFrequency, float highFrequency, std::uint32_t durationMilliseconds);
    /// @brief 判断是否满足 `ControllerConnected` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isControllerConnected() const;
    /// @brief 设置是否允许 ESC 键退出主循环。
    /// @param enabled 是否启用对应行为。
    void setEscapeQuits(bool enabled);
    /// @brief 获取 `DpiScale`。
    /// @return 返回当前显示 DPI 相对 96 的缩放比例。
    float getDpiScale() const;
#ifdef TINYENGINE_TEST_HOOKS
    std::vector<Vec4> ReadHdrAttachmentForTests(std::size_t attachment = 0) const;
    std::size_t getMaterialProgramCompileCountForTests(MaterialPass pass = MaterialPass::Base) const;
    const std::vector<MaterialDiagnostic>& GetMaterialDiagnosticsForTests() const;
    float readShadowDepthForTests(int x, int y, std::size_t cascade = 0) const;
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
    std::function<void(const void*)> m_eventCallback;
    std::function<void()> m_closeRequestCallback;
};
} // namespace tinyengine
