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

class EditorApp
{
  public:
    bool Initialize(const std::filesystem::path &projectFile);
    void Run(std::uint32_t maximumFrames = 0);
    void Shutdown();

  private:
    void Draw();
    void DrawMenuBar();
    void DrawSettings();
    void DrawHierarchy();
    void DrawInspector();
    void DrawSceneView();
    void DrawAssets();
    void DrawConsole();
    void DrawAnimationEditor();
    void DrawShaderEditor();
    void DrawMaterialGraphEditor();
    void DrawEffectEditor();
    void DrawBehaviorTreeEditor();
    void DrawSceneTabs();
    void RecordUndo();
    void Undo();
    void Redo();
    bool SaveCurrentScene();
    bool Autosave();
    bool RestoreAutosave();
    std::filesystem::path GetAutosavePath();
    std::filesystem::path GetEditorSettingsPath();
    void LoadEditorSettings();
    bool SaveEditorSettings();
    void UpdateWindowTitle();
    const char *Localize(const char *english, const char *simplifiedChinese) const;
    std::string LocalizeReflected(std::string_view text) const;
    bool OpenSceneDocument(const std::filesystem::path &path);
    void NewSceneDocument();
    bool SwitchSceneDocument(std::size_t index);
    bool PlaceAssetInScene(const AssetInfo &asset);
    std::filesystem::path GetThumbnailPath(const AssetInfo &asset) const;
    bool IsThumbnailReady(const AssetInfo &asset, const std::filesystem::path &path) const;
    void RequestThumbnail(const AssetInfo &asset, bool force = false);
    Entity PickEntity(float normalizedX, float normalizedY) const;
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
    struct SceneDocument
    {
        std::filesystem::path path;
        std::string snapshot;
        bool dirty = false;
    };
    std::vector<SceneDocument> m_sceneDocuments;
    std::size_t m_activeSceneDocument = 0;
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
