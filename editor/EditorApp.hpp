#pragma once

#include "MiniEngine/Runtime/Engine.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
    class SdlGlRenderDevice;

    class EditorApp
    {
    public:
        bool Initialize(const std::filesystem::path& projectFile);
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
        void RecordUndo();
        void Undo();
        void Redo();
        bool SaveCurrentScene();
        Entity PickEntity(float normalizedX, float normalizedY) const;
        bool DrawGizmo(float minimumX, float minimumY, float width, float height);

        enum class GizmoMode { Translate, Rotate, Scale };

        Engine m_engine;
        SdlGlRenderDevice* m_device = nullptr;
        Entity m_selected = InvalidEntity;
        std::vector<std::string> m_undo;
        std::vector<std::string> m_redo;
        std::vector<std::string> m_messages;
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
    };
}
