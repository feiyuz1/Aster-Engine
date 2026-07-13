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
        void DrawAssets();
        void DrawConsole();
        void RecordUndo();
        void Undo();
        void Redo();
        bool SaveCurrentScene();

        Engine m_engine;
        SdlGlRenderDevice* m_device = nullptr;
        Entity m_selected = InvalidEntity;
        std::vector<std::string> m_undo;
        std::vector<std::string> m_redo;
        std::vector<std::string> m_messages;
        bool m_initialized = false;
    };
}
