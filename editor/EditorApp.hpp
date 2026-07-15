#pragma once

#include "MiniEngine/Runtime/Engine.hpp"
#ifdef TINYENGINE_HAS_ASSIMP
#include "MiniEngine/Import/AssimpImporter.hpp"
#endif

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
class SdlGlRenderDevice;

class EditorApp
{
  public:
    bool Initialize(const std::filesystem::path &projectFile);
    void Run();
    void Shutdown();

  private:
    void Draw();
    void DrawMenuBar();
    void DrawHierarchy();
    void DrawInspector();
    void DrawSceneView();
    void DrawAssets();
    void DrawConsole();
    void DrawSceneTabs();
    void RecordUndo();
    void Undo();
    void Redo();
    bool SaveCurrentScene();
    bool Autosave();
    bool RestoreAutosave();
    std::filesystem::path GetAutosavePath();
    void UpdateWindowTitle();
    bool OpenSceneDocument(const std::filesystem::path &path);
    void NewSceneDocument();
    bool SwitchSceneDocument(std::size_t index);
    Entity PickEntity(float normalizedX, float normalizedY) const;
    bool DrawGizmo(float minimumX, float minimumY, float width, float height);

    enum class GizmoMode
    {
        Translate,
        Rotate,
        Scale
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
    Vec3 m_editorCameraPosition{0.0f, 2.5f, 7.0f};
    Vec3 m_editorCameraRotation{-10.0f, 0.0f, 0.0f};
    float m_editorCameraFov = 60.0f;
    GizmoMode m_gizmoMode = GizmoMode::Translate;
    int m_activeGizmoAxis = -1;
    bool m_gizmoSnap = false;
    float m_translateSnap = 0.5f;
    float m_rotateSnap = 15.0f;
    float m_scaleSnap = 0.1f;
    float m_gizmoAccumulatedMotion = 0.0f;
    float m_gizmoAppliedMotion = 0.0f;
    std::string m_selectedAsset;
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
#endif
};
} // namespace MiniEngine
