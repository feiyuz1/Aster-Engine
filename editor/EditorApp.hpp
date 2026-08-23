// 文件：editor/EditorApp.hpp
// 作用：定义编辑器窗口、面板状态、选择、Gizmo、资产浏览和撤销重做。
// 约定：编辑器状态与运行时 World 分离；所有写操作都应进入撤销或明确标记为临时预览。
// 类型：公开声明。

#pragma once

#include "MiniEngine/AI/BehaviorTree.hpp"
#include "MiniEngine/Animation/SkeletalAnimation.hpp"
#include "MiniEngine/Render/MaterialGraph.hpp"
#include "MiniEngine/Runtime/Engine.hpp"
#include "MiniEngine/VFX/ParticleSystem.hpp"
#ifdef TINYENGINE_HAS_ASSIMP
#include "MiniEngine/Import/AssimpImporter.hpp"
#endif

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
class SdlGlRenderDevice;

// 单窗口编辑器外壳。EditorApp 拥有 UI 状态，场景与各运行时服务仍由 Engine 统一持有。
class EditorApp
{
  public:
    /// @brief 初始化对象及其依赖资源。
    /// @param projectFile TinyEngine 项目文件路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Initialize(const std::filesystem::path &projectFile);
    /// @brief 运行主循环或任务流程。
    /// @param maximumFrames 最大运行帧数；为零表示不限制。
    void Run(std::uint32_t maximumFrames = 0);
    /// @brief 停止服务并释放对象持有的资源。
    void Shutdown();

  private:
    /// @brief 提交或绘制当前对象。
    void Draw();
    /// @brief 绘制 `MenuBar`。
    void DrawMenuBar();
    /// @brief 绘制 `Settings`。
    void DrawSettings();
    /// @brief 绘制 `Hierarchy`。
    void DrawHierarchy();
    /// @brief 绘制 `Inspector`。
    void DrawInspector();
    /// @brief 绘制 `SceneView`。
    void DrawSceneView();
    /// @brief 绘制 `Assets`。
    void DrawAssets();
    /// @brief 绘制 `Console`。
    void DrawConsole();
    /// @brief 绘制 `AnimationEditor`。
    void DrawAnimationEditor();
    /// @brief 绘制 `ShaderEditor`。
    void DrawShaderEditor();
    /// @brief 绘制 `MaterialGraphEditor`。
    void DrawMaterialGraphEditor();
    /// @brief 绘制 `EffectEditor`。
    void DrawEffectEditor();
    /// @brief 绘制 `BehaviorTreeEditor`。
    void DrawBehaviorTreeEditor();
    /// @brief 绘制 `SceneTabs`。
    void DrawSceneTabs();
    /// @brief 绘制未命名场景首次保存和另存为对话框。
    void DrawSceneSaveDialog();
    /// @brief 绘制脏场景标签关闭确认对话框。
    void DrawSceneCloseDialog();
    /// @brief 记录 `Undo`。
    void RecordUndo();
    /// @brief 撤销最近一次可撤销操作。
    void Undo();
    /// @brief 重做最近一次被撤销的操作。
    void Redo();
    /// @brief 保存 `CurrentScene`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SaveCurrentScene();
    /// @brief 请求为当前场景选择新的保存路径。
    /// @return 成功打开对话框时返回 `true`，否则返回 `false`。
    bool RequestSaveCurrentSceneAs();
    /// @brief 将当前 World 保存到指定场景文档及路径。
    /// @param documentIndex 目标场景文档索引。
    /// @param path 经过验证的场景文件绝对路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SaveSceneDocumentTo(std::size_t documentIndex, const std::filesystem::path &path);
    /// @brief 校验另存为输入并解析为资产目录内的绝对路径。
    /// @param error 校验失败时写入面向用户的错误消息。
    /// @return 校验成功时返回目标路径，否则返回空路径。
    std::filesystem::path ResolveSceneSavePath(std::string &error);
    /// @brief 在场景已成功保存后执行项目打包。
    void PackageProject();
    /// @brief 将当前编辑状态自动保存到恢复文件。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Autosave();
    /// @brief 恢复 `Autosave`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool RestoreAutosave();
    /// @brief 获取 `AutosavePath`。
    /// @return 返回对应的文本或路径。
    std::filesystem::path GetAutosavePath();
    /// @brief 获取 `EditorSettingsPath`。
    /// @return 返回对应的文本或路径。
    std::filesystem::path GetEditorSettingsPath();
    /// @brief 加载 `EditorSettings`。
    void LoadEditorSettings();
    /// @brief 保存 `EditorSettings`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SaveEditorSettings();
    /// @brief 更新 `WindowTitle`。
    void UpdateWindowTitle();
    /// @brief 根据当前界面语言选择对应文本。
    /// @param english 接口所需的输入参数。
    /// @param simplifiedChinese 接口所需的输入参数。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const char *Localize(const char *english, const char *simplifiedChinese) const;
    /// @brief 本地化 `Reflected`。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @return 返回对应的文本或路径。
    std::string LocalizeReflected(std::string_view text) const;
    /// @brief 打开 `SceneDocument`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool OpenSceneDocument(const std::filesystem::path &path);
    /// @brief 新建 `SceneDocument`。
    void NewSceneDocument();
    /// @brief 切换 `SceneDocument`。
    /// @param index 目标元素或骨骼索引。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SwitchSceneDocument(std::size_t index);
    /// @brief 关闭指定场景文档；最后一个标签关闭后创建空白文档，保持编辑器状态有效。
    /// @param index 要关闭的场景文档索引。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool CloseSceneDocument(std::size_t index);
    // 资产放置是一个可撤销事务：根据资产类型创建或修改实体，并将场景标记为脏。
    /// @brief 放置 `AssetInScene`。
    /// @param asset 要加载、维护或放置的资产信息。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool PlaceAssetInScene(const AssetInfo &asset);
    /// @brief 获取 `ThumbnailPath`。
    /// @param asset 要加载、维护或放置的资产信息。
    /// @return 返回对应的文本或路径。
    std::filesystem::path GetThumbnailPath(const AssetInfo &asset) const;
    /// @brief 判断是否满足 `ThumbnailReady` 条件。
    /// @param asset 要加载、维护或放置的资产信息。
    /// @param path 文件、资源或目录路径。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsThumbnailReady(const AssetInfo &asset, const std::filesystem::path &path) const;
    /// @brief 请求 `Thumbnail`。
    /// @param asset 要加载、维护或放置的资产信息。
    /// @param force 是否启用对应行为。
    void RequestThumbnail(const AssetInfo &asset, bool force = false);
    // 选择与 Gizmo 使用同一套编辑器相机参数，避免视图显示与射线计算不一致。
    /// @brief 拾取 `Entity`。
    /// @param normalizedX 接口所需的输入参数。
    /// @param normalizedY 接口所需的输入参数。
    /// @return 返回实体句柄；失败时返回 `InvalidEntity`。
    Entity PickEntity(float normalizedX, float normalizedY) const;
    /// @brief 绘制 `Gizmo`。
    /// @param minimumX 目标尺寸、数量、索引或限制值。
    /// @param minimumY 目标尺寸、数量、索引或限制值。
    /// @param width 目标宽度。
    /// @param height 目标高度。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool DrawGizmo(float minimumX, float minimumY, float width, float height);

    enum class GizmoMode
    {
        Translate,
        Rotate,
        Scale
    };
    enum class GizmoSpace
    {
        World,
        Local
    };
    enum class PivotMode
    {
        Center,
        Active,
        Individual
    };
    enum class InterfaceLanguage
    {
        English,
        SimplifiedChinese
    };
    enum class AssetViewMode
    {
        List,
        Grid
    };

    Engine m_engine;
    SdlGlRenderDevice *m_device = nullptr;
    Entity m_selected = InvalidEntity;
    std::vector<Entity> m_selection;
    std::vector<std::string> m_undo;
    std::vector<std::string> m_redo;
    std::vector<std::string> m_messages;
    std::array<char, 256> m_consoleInput{};
    bool m_initialized = false;
    InterfaceLanguage m_interfaceLanguage = InterfaceLanguage::SimplifiedChinese;
    bool m_showSettings = false;
    Vec3 m_editorCameraPosition{0.0f, 2.5f, 7.0f};
    Vec3 m_editorCameraRotation{-10.0f, 0.0f, 0.0f};
    float m_editorCameraFov = 60.0f;
    GizmoMode m_gizmoMode = GizmoMode::Translate;
    GizmoSpace m_gizmoSpace = GizmoSpace::World;
    PivotMode m_pivotMode = PivotMode::Center;
    int m_activeGizmoAxis = -1;
    bool m_gizmoSnap = false;
    float m_translateSnap = 0.5f;
    float m_rotateSnap = 15.0f;
    float m_scaleSnap = 0.1f;
    float m_gizmoAccumulatedMotion = 0.0f;
    float m_gizmoAppliedMotion = 0.0f;
    bool m_marqueeActive = false;
    Vec2 m_marqueeStart{};
    std::string m_selectedAsset;
    std::vector<std::string> m_selectedAssets;
    AssetViewMode m_assetViewMode = AssetViewMode::Grid;
    float m_assetThumbnailSize = 96.0f;
    std::array<char, 256> m_assetSearch{};
    std::string m_assetTypeFilter;
    std::filesystem::path m_assetFolder;
    std::unordered_map<std::string, JobHandle> m_thumbnailJobs;
    std::string m_openShaderAsset;
    std::vector<char> m_shaderSource;
    std::string m_openMaterialGraphAsset;
    MaterialGraph m_materialGraph;
    std::string m_openAnimationAsset;
    SkeletalClip m_editSkeletalClip;
    BlendTree1D m_editBlendTree;
    AnimationGraph m_editAnimationGraph;
    std::string m_openEffectAsset;
    ParticleEmitterSettings m_editEffect;
    std::string m_openBehaviorAsset;
    BehaviorTreeGraph m_editBehaviorGraph;
    bool m_sceneDirty = false;
    double m_lastAutosaveTime = 0.0;
    bool m_lastTitleDirty = false;
    bool m_titleInitialized = false;
    // 每个标签页保存自己的序列化快照；切换前先回写当前 World，切换后再恢复目标快照。
    struct SceneDocument
    {
        std::filesystem::path path;
        std::string snapshot;
        bool dirty = false;
    };
    std::vector<SceneDocument> m_sceneDocuments;
    std::size_t m_activeSceneDocument = 0;
    static constexpr std::size_t InvalidSceneDocument = static_cast<std::size_t>(-1);
    // 程序化创建/打开标签后，下一帧显式要求 ImGui 选中它，避免 UI 又切回旧文档。
    std::size_t m_pendingSelectedSceneDocument = InvalidSceneDocument;
    // Save As 是异步 UI 流程；记录发起保存的文档，禁止对话框期间把路径写给其他标签。
    std::size_t m_sceneSaveDocument = InvalidSceneDocument;
    bool m_requestSceneSavePopup = false;
    bool m_packageAfterSceneSave = false;
    std::array<char, 1024> m_sceneSavePath{};
    std::string m_sceneSaveError;
    std::filesystem::path m_sceneOverwriteTarget;
    // 关闭脏标签是独立事务；Save As 成功后可继续完成最初的关闭请求。
    std::size_t m_sceneCloseDocument = InvalidSceneDocument;
    bool m_requestSceneClosePopup = false;
    bool m_closeAfterSceneSave = false;
#ifdef TINYENGINE_HAS_ASSIMP
    AssetImportQueue m_importQueue;
    std::uint64_t m_activeImportTask = 0;
    std::array<char, 1024> m_importSourcePath{};
    std::filesystem::path m_importSource;
    std::filesystem::path m_lastImportDirectory;
    std::string m_importValidationError;
    float m_importScale = 1.0f;
    bool m_importFlipUvs = false;
    bool m_importCompressAnimations = true;
    float m_importPositionTolerance = 0.001f;
    float m_importRotationTolerance = 0.1f;
    float m_importScaleTolerance = 0.001f;
#endif
};
} // namespace MiniEngine
