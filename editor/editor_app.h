// 文件：editor/editor_app.h
// 作用：定义编辑器窗口、面板状态、选择、Gizmo、资产浏览和撤销重做。
// 约定：编辑器状态与运行时 World 分离；所有写操作都应进入撤销或明确标记为临时预览。
// 类型：公开声明。

#pragma once

#include "asset_reference_graph.h"
#include "material_asset_editor.h"
#include "material_graph_editor.h"
#include "tinyengine/ai/behavior_tree.h"
#include "tinyengine/animation/skeletal_animation.h"
#include "tinyengine/ecs/reflection.h"
#include "tinyengine/project/project_packager.h"
#include "tinyengine/project/solution_generator.h"
#include "tinyengine/render/material_graph.h"
#include "tinyengine/runtime/engine.h"
#include "tinyengine/vfx/particle_system.h"
#ifdef TINYENGINE_HAS_ASSIMP
#include "tinyengine/import/assimp_importer.h"
#endif

#include <array>
#include <filesystem>
#include <future>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tinyengine
{
class SdlGlRenderDevice;

// 单窗口编辑器外壳。EditorApp 拥有 UI 状态，场景与各运行时服务仍由 Engine 统一持有。
class EditorApp
{
  public:
    /// @brief 初始化对象及其依赖资源。
    /// @param projectFile Aster-Engine 项目文件路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool initialize(const std::filesystem::path& projectFile);
    /// @brief 运行主循环或任务流程。
    /// @param maximumFrames 最大运行帧数；为零表示不限制。
    void Run(std::uint32_t maximumFrames = 0);
    /// @brief 停止服务并释放对象持有的资源。
    void shutdown();
    std::filesystem::path takeRequestedProject();

  private:
#ifdef TINYENGINE_TEST_HOOKS
    friend struct EditorInteractionTests;
#endif
    void handleShortcuts();
    void requestEditorExit(bool continueExit = false);
    void resetSceneInteraction();
    void recordPropertyUndo();
    bool restoreSceneSnapshot(const std::string& snapshot);
    /// @brief 提交或绘制当前对象。
    void draw();
    /// @brief 绘制 `MenuBar`。
    void drawMenuBar();
    void drawProjectLauncher();
    void startProjectCreation();
    void startCreatedProjectBuild();
    void pollProjectCreation();
    void drawSolutionStatus();
    void drawPackagingStatus();
    void startSolutionGeneration();
    /// @brief 绘制 `Settings`。
    void drawSettings();
    void drawViewCube(float x, float y, float width, float height, bool hovered);
    /// @brief 绘制 `Hierarchy`。
    void drawHierarchy();
    /// @brief 绘制 `Inspector`。
    void drawInspector();
    void drawEntityInspector(World& world);
    std::vector<Entity> GetInspectorTargets() const;
    bool applyInspectorProperty(const ReflectedComponent& component, const ReflectedProperty& property,
                                const void* value, bool continuous = true, int axis = -1, bool record = true);
    bool applyInspectorAsset(const ReflectedComponent& component, const ReflectedProperty& property,
                             const std::string& path, std::uint32_t fieldId);
    bool canApplyMaterialToSelection() const;
    bool applyMaterialToMeshes(const std::string& path, const std::vector<Entity>& targets);
    void resetInspectorComponent(const ReflectedComponent& component);
    void selectAsset(const std::string& path, bool extend);
    void openAsset(const AssetInfo& asset);
    void drawAssetPreview();
    enum class FeedbackKind
    {
        Info,
        Success,
        Warning,
        Error
    };
    void notify(std::string message, FeedbackKind kind = FeedbackKind::Info);
    void drawFeedback();
    /// @brief 绘制 `SceneView`。
    void drawSceneView();
    struct WorkspaceLayout
    {
        float left, right, upper, assets;
    };
    WorkspaceLayout getWorkspaceLayout(float width, float height) const;
    void drawWorkspaceSplitters();
    void drawWorkspace();
    void drawPerformance();
    void applyEditorStyle();
    bool beginEditorPanel(const char* english, const char* chinese, bool& visible, Vec2 position, Vec2 size,
                          bool maximized = false);
    void resetWorkspace();
    bool saveWorkspace();
    bool restoreWorkspace();
    void locateAsset(const std::string& path);
    bool moveEditorAsset(const std::string& source, const std::filesystem::path& destination, std::string& error);
    void updateSceneCamera(bool hovered, bool focused);
    void focusSceneEntity(Entity entity);
    void requestOpenScene();
    void drawSceneOpenDialog();
    /// @brief 绘制 `Assets`。
    void drawAssets();
    void drawAssetCreationMenu(const std::filesystem::path& directory);
    void drawAssetFolderDialog();
    void requestDeleteAssets();
    void drawAssetDeleteDialog();
    std::vector<std::string> AssetDeletionBlockers(const std::vector<std::string>& paths);
    bool restoreDeletedAssets();
    void loadAssetTrashHistory();
    void openAssetReferences(const std::string& path);
    void drawAssetReferenceViewer();
    void pollAssetImport();
    /// @brief 绘制 `Console`。
    void drawConsole();
    /// @brief 绘制 `AnimationEditor`。
    void drawAnimationEditor();
    /// @brief 绘制 `ShaderEditor`。
    void drawShaderEditor();
    void drawShaderCloseDialog();
    bool openShaderDocument(const std::string& path);
    bool isShaderDirty() const;
    bool saveShaderDocument();
    enum class ShaderAction
    {
        None,
        Open,
        Close,
        Exit
    };
    enum class ShaderDecision
    {
        Save,
        Discard,
        Cancel
    };
    void requestShaderAction(ShaderAction action, const std::string& path = {});
    bool resolveShaderAction(ShaderDecision decision);
    void completeShaderAction();
    /// @brief 绘制 `MaterialGraphEditor`。
    void drawMaterialGraphEditor();
    /// @brief 绘制 `EffectEditor`。
    void drawEffectEditor();
    /// @brief 绘制 `BehaviorTreeEditor`。
    void drawBehaviorTreeEditor();
    /// @brief 绘制 `SceneTabs`。
    void drawSceneTabs();
    /// @brief 绘制未命名场景首次保存和另存为对话框。
    void drawSceneSaveDialog();
    /// @brief 绘制脏场景标签关闭确认对话框。
    void drawSceneCloseDialog();
    /// @brief 绘制退出编辑器确认对话框。
    void drawExitDialog();
    /// @brief 将已创建的快照提交为撤销记录并标记场景脏。
    /// @param snapshot 变更前创建的场景快照；仅在确认变更发生时提交。
    void commitUndoSnapshot(std::string snapshot);
    /// @brief 记录 `Undo`。
    void recordUndo();
    /// @brief 撤销最近一次可撤销操作。
    void undo();
    /// @brief 重做最近一次被撤销的操作。
    void redo();
    /// @brief 保存 `CurrentScene`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool saveCurrentScene();
    /// @brief 请求为当前场景选择新的保存路径。
    /// @return 成功打开对话框时返回 `true`，否则返回 `false`。
    bool requestSaveCurrentSceneAs();
    /// @brief 将当前 World 保存到指定场景文档及路径。
    /// @param documentIndex 目标场景文档索引。
    /// @param path 经过验证的场景文件绝对路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool saveSceneDocumentTo(std::size_t documentIndex, const std::filesystem::path& path);
    /// @brief 校验另存为输入并解析为资产目录内的绝对路径。
    /// @param error 校验失败时写入面向用户的错误消息。
    /// @return 校验成功时返回目标路径，否则返回空路径。
    std::filesystem::path resolveSceneSavePath(std::string& error);
    /// @brief 在场景已成功保存后执行项目打包。
    void packageProject();
    /// @brief 将当前编辑状态自动保存到恢复文件。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool autosave();
    /// @brief 恢复 `Autosave`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool restoreAutosave();
    /// @brief 获取 `AutosavePath`。
    /// @return 返回对应的文本或路径。
    std::filesystem::path getAutosavePath();
    /// @brief 获取 `EditorSettingsPath`。
    /// @return 返回对应的文本或路径。
    std::filesystem::path getEditorSettingsPath();
    /// @brief 加载 `EditorSettings`。
    void loadEditorSettings();
    /// @brief 保存 `EditorSettings`。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool saveEditorSettings();
    /// @brief 更新 `WindowTitle`。
    void updateWindowTitle();
    /// @brief 根据当前界面语言选择对应文本。
    /// @param english 接口所需的输入参数。
    /// @param simplifiedChinese 接口所需的输入参数。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const char* localize(const char* english, const char* simplifiedChinese) const;
    /// @brief 本地化 `Reflected`。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @return 返回对应的文本或路径。
    std::string localizeReflected(std::string_view text) const;
    /// @brief 打开 `SceneDocument`。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool openSceneDocument(const std::filesystem::path& path);
    /// @brief 新建 `SceneDocument`。
    void newSceneDocument();
    /// @brief 切换 `SceneDocument`。
    /// @param index 目标元素或骨骼索引。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool switchSceneDocument(std::size_t index);
    /// @brief 关闭指定场景文档；最后一个标签关闭后创建空白文档，保持编辑器状态有效。
    /// @param index 要关闭的场景文档索引。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool closeSceneDocument(std::size_t index);
    // 资产放置是一个可撤销事务：根据资产类型创建或修改实体，并将场景标记为脏。
    /// @brief 放置 `AssetInScene`。
    /// @param asset 要加载、维护或放置的资产信息。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool placeAssetInScene(const AssetInfo& asset, const Vec3* position = nullptr);
    bool getScenePlacement(float normalizedX, float normalizedY, Vec3& position) const;
    void drawSceneOverlays(float x, float y, float width, float height, bool hovered);
    /// @brief 获取 `ThumbnailPath`。
    /// @param asset 要加载、维护或放置的资产信息。
    /// @return 返回对应的文本或路径。
    std::filesystem::path getThumbnailPath(const AssetInfo& asset) const;
    /// @brief 判断是否满足 `ThumbnailReady` 条件。
    /// @param asset 要加载、维护或放置的资产信息。
    /// @param path 文件、资源或目录路径。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isThumbnailReady(const AssetInfo& asset, const std::filesystem::path& path) const;
    /// @brief 请求 `Thumbnail`。
    /// @param asset 要加载、维护或放置的资产信息。
    /// @param force 是否启用对应行为。
    void requestThumbnail(const AssetInfo& asset, bool force = false);
    // 选择与 Gizmo 使用同一套编辑器相机参数，避免视图显示与射线计算不一致。
    /// @brief 拾取 `Entity`。
    /// @param normalizedX 接口所需的输入参数。
    /// @param normalizedY 接口所需的输入参数。
    /// @return 返回实体句柄；失败时返回 `InvalidEntity`。
    Entity pickEntity(float normalizedX, float normalizedY) const;
    /// @brief 收集射线上按距离排序的全部命中实体，用于重叠对象循环选择。
    /// @param normalizedX 归一化视口横坐标。
    /// @param normalizedY 归一化视口纵坐标。
    /// @return 返回按命中距离升序（同距按包围盒更小优先）排列的实体列表。
    std::vector<Entity> PickEntityHits(float normalizedX, float normalizedY) const;
    /// @brief 按用户界面缩放设置调整逻辑布局尺寸；显示器 DPI 由渲染后端处理。
    /// @param value 逻辑像素值。
    /// @return 返回应用界面缩放后的逻辑尺寸。
    float scaled(float value) const;
    /// @brief 绘制 `Gizmo`。
    /// @param minimumX 目标尺寸、数量、索引或限制值。
    /// @param minimumY 目标尺寸、数量、索引或限制值。
    /// @param width 目标宽度。
    /// @param height 目标高度。
    /// @param sceneHovered 鼠标是否位于未被遮挡的场景图像上；用于阻止命中穿透浮动窗口。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool drawGizmo(float minimumX, float minimumY, float width, float height, bool sceneHovered);

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
    bool m_projectLauncherMode = false;
    std::filesystem::path m_requestedProject;
    std::array<char, 128> m_newProjectName{};
    std::array<char, 1024> m_newProjectParent{};
    std::array<char, 1024> m_existingProjectPath{};
    std::string m_projectLauncherError;
    std::filesystem::path m_createdProject;
    std::filesystem::path m_creationSDKRoot;
    std::future<SolutionResult> m_projectCreationFuture;
    std::string m_projectCreationOutput;
    std::future<SolutionResult> m_solutionFuture;
    std::string m_solutionStatus;
    std::future<PackageResult> m_packageFuture;
    SdlGlRenderDevice* m_device = nullptr;
    Entity m_selected = kInvalidEntity;
    std::vector<Entity> m_selection;
    std::vector<std::string> m_undo;
    std::vector<std::string> m_redo;
    bool m_sceneCommandsFocused = false;
    bool m_inspectorFocused = false;
    std::uint32_t m_inspectorEditId = 0;
    std::array<char, 128> m_componentSearch{};
    std::array<char, 256> m_assetPickerSearch{};
    std::unordered_map<std::uint32_t, std::string> m_inspectorErrors;
    std::unordered_map<std::uint32_t, std::string> m_inspectorAssetDrafts;
    std::vector<Entity> m_inspectorSelection;
    struct Feedback
    {
        std::string message;
        FeedbackKind kind;
        double expiresAt;
    };
    std::vector<Feedback> m_feedback;
    std::vector<std::string> m_messages;
    std::size_t m_consoleRenderedMessageCount = 0;
    std::array<char, 256> m_consoleInput{};
    bool m_initialized = false;
    InterfaceLanguage m_interfaceLanguage = InterfaceLanguage::SimplifiedChinese;
    bool m_showSettings = false;
    Vec3 m_editorCameraPosition{0.0f, 2.5f, 7.0f};
    Vec3 m_editorCameraRotation{-10.0f, 0.0f, 0.0f};
    float m_editorCameraFov = 60.0f;
    float m_dpiScale = 1.0f;
    // 操作由实际抓取的手柄决定；拖拽中保持固定，不作为显示模式。
    GizmoMode m_gizmoDragMode = GizmoMode::Translate;
    GizmoSpace m_gizmoSpace = GizmoSpace::World;
    PivotMode m_pivotMode = PivotMode::Center;
    int m_activeGizmoAxis = -1;
    bool m_gizmoSnap = false;
    float m_translateSnap = 0.5f;
    float m_rotateSnap = 15.0f;
    float m_scaleSnap = 0.1f;
    float m_gizmoAccumulatedMotion = 0.0f;
    float m_gizmoAppliedMotion = 0.0f;
    bool m_gizmoUndoRecorded = false;
    float m_leftPanelRatio = 0.18f;
    float m_rightPanelRatio = 0.24f;
    float m_upperPanelRatio = 0.66f;
    float m_assetsPanelRatio = 0.62f;
    bool m_sceneMaximized = false;
    bool m_showHierarchy = true, m_showInspector = true, m_showAssets = true, m_showConsole = true,
         m_showPerformance = true;
    bool m_resetWorkspace = false;
    std::string m_workspaceIniPath;
    float m_uiScale = 1.0f;
    float m_cameraSpeed = 1.0f;
    bool m_showSceneGrid = true, m_showSelectionBounds = true;
    ViewportRenderMode m_viewportRenderMode = ViewportRenderMode::Lit;
    bool m_scenePanActive = false, m_sceneAltOrbitActive = false;
    Vec3 m_sceneOrbitPivot{};
    bool m_viewCubeActive = false, m_viewCubeDragged = false, m_viewCubeHome = false;
    Vec2 m_viewCubeMouseStart{}, m_viewCubeMouseLast{};
    Vec3 m_viewCubePivot{}, m_viewCubeDirection{};
    std::string m_scenePreviewAsset;
    Aabb m_scenePreviewBounds{};
    bool m_scenePreviewBoundsValid = false;
    bool m_assetsFocused = false, m_assetRenameRequested = false, m_assetRevealPending = false, m_focusAssets = false;
    std::array<char, 128> m_hierarchySearch{};
    // 右键轨道采用“按下即捕获”语义：拖拽期间光标滑出场景视图不中断旋转。
    bool m_sceneOrbitActive = false;
    bool m_marqueeActive = false;
    Vec2 m_marqueeStart{};
    // 重叠命中循环：同一位置连续点击可在多个命中实体间轮换选择。
    Vec2 m_lastPickScreen{};
    bool m_lastPickValid = false;
    std::vector<Entity> m_lastPickHits;
    std::size_t m_lastPickIndex = 0;
    std::string m_selectedAsset;
    std::vector<std::string> m_selectedAssets;
    std::string m_editingAsset;
    std::string m_previewAsset;
    bool m_showAssetPreview = false;
    std::string m_assetMoveError;
    AssetViewMode m_assetViewMode = AssetViewMode::Grid;
    float m_assetThumbnailSize = 96.0f;
    std::array<char, 256> m_assetSearch{};
    std::set<std::string> m_assetTypeFilters;
    std::filesystem::path m_assetFolder;
    std::filesystem::path m_assetCreateParent;
    std::array<char, 256> m_assetFolderName{};
    std::string m_assetFolderError;
    bool m_assetFolderCreateRequested = false;
    bool m_assetDeleteRequested = false;
    std::vector<std::string> m_assetDeletePaths;
    std::vector<std::string> m_assetDeleteHistory;
    std::filesystem::path m_assetTrashProject;
    std::string m_assetDeleteError;
    bool m_showAssetReferences = false, m_assetReferencesDirty = false, m_assetReferencesFit = false;
    std::string m_assetReferenceRoot;
    AssetReferenceGraph m_assetReferenceGraph;
    int m_assetReferenceDepth = 1;
    float m_assetReferenceZoom = 1.0f;
    Vec2 m_assetReferencePan{};
    std::unordered_map<std::string, JobHandle> m_thumbnailJobs;
    std::string m_openShaderAsset;
    bool m_shaderEditorHidden = false;
    std::vector<char> m_shaderSource;
    std::string m_savedShaderSource;
    bool m_requestSceneOpenPopup = false;
    std::array<char, 1024> m_sceneOpenPath{};
    std::string m_sceneOpenError;
    std::vector<std::filesystem::path> m_sceneOpenFiles;
    Vec2 m_gizmoScreenDirection{};
    float m_gizmoScreenLength = 1.0f;
    Vec3 m_gizmoDragAxis{};
    Vec3 m_gizmoDragOrigin{};
    float m_gizmoDragHandleLength = 1.0f;
    FrameView m_gizmoDragView;
    Vec3 m_gizmoRotationVector{};
    int m_shaderDocumentRevision = 0;
    std::string m_shaderError;
    std::string m_shaderCompileError;
    ShaderAction m_shaderAction = ShaderAction::None;
    std::string m_pendingShaderPath;
    bool m_requestShaderPopup = false;
    bool m_shaderFocused = false;
    std::string m_openMaterialGraphAsset;
    MaterialGraphEditor m_materialGraphEditor;
    MaterialAssetEditor m_materialAssetEditor;
    bool m_requestMaterialExitPopup = false;
    std::string m_openMaterialAsset;
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
    // 每个标签页保存场景快照和独立的撤销/重做历史；切换前回写，切换后恢复。
    struct SceneDocument
    {
        std::filesystem::path path;
        std::string snapshot;
        bool dirty = false;
        std::vector<std::string> undo;
        std::vector<std::string> redo;
    };
    std::vector<SceneDocument> m_sceneDocuments;
    std::size_t m_activeSceneDocument = 0;
    static constexpr std::size_t kInvalidSceneDocument = static_cast<std::size_t>(-1);
    // 程序化创建/打开标签后，下一帧显式要求 ImGui 选中它，避免 UI 又切回旧文档。
    std::size_t m_pendingSelectedSceneDocument = kInvalidSceneDocument;
    // Save As 是异步 UI 流程；记录发起保存的文档，禁止对话框期间把路径写给其他标签。
    std::size_t m_sceneSaveDocument = kInvalidSceneDocument;
    bool m_requestSceneSavePopup = false;
    bool m_packageAfterSceneSave = false;
    std::array<char, 1024> m_sceneSavePath{};
    std::string m_sceneSaveError;
    std::filesystem::path m_sceneOverwriteTarget;
    // 关闭脏标签是独立事务；Save As 成功后可继续完成最初的关闭请求。
    std::size_t m_sceneCloseDocument = kInvalidSceneDocument;
    bool m_requestSceneClosePopup = false;
    bool m_closeAfterSceneSave = false;
    // 菜单与系统关闭共用退出事务；所有脏文档逐一确认，另存为后继续检查剩余文档。
    bool m_requestExitPopup = false;
    bool m_exitAfterSceneSave = false;
    bool m_exitInProgress = false;
    std::vector<std::size_t> m_exitDiscardedDocuments;
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
} // namespace tinyengine
