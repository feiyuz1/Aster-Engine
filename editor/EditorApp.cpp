#include "EditorApp.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#ifdef TINYENGINE_HAS_ASSIMP
#include "MiniEngine/Import/AssimpImporter.hpp"
#endif

#include <SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>

#include <algorithm>
#include <cstring>
#include <optional>

namespace MiniEngine
{
    bool EditorApp::Initialize(const std::filesystem::path& projectFile)
    {
        if (!m_engine.Initialize(projectFile, false)) return false;
        m_device = dynamic_cast<SdlGlRenderDevice*>(m_engine.GetContext().renderer->GetDevice());
        if (!m_device) { m_engine.Shutdown(); return false; }
        SDL_SetWindowTitle(static_cast<SDL_Window*>(m_device->GetNativeWindow()), "TinyEngine Editor");
        IMGUI_CHECKVERSION(); ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        if (!ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(m_device->GetNativeWindow()), m_device->GetGlContext()) || !ImGui_ImplOpenGL3_Init("#version 330"))
        { ImGui::DestroyContext(); m_engine.Shutdown(); return false; }
        m_device->SetEventCallback([](const void* event) { ImGui_ImplSDL2_ProcessEvent(static_cast<const SDL_Event*>(event)); });
        m_engine.GetContext().renderer->SetOverlayCallback([this]() { Draw(); });
        m_messages.emplace_back("Editor initialized. Scene changes are isolated from Play mode.");
        m_initialized = true; return true;
    }

    void EditorApp::Run() { if (m_initialized) m_engine.Run(); }

    void EditorApp::Shutdown()
    {
        if (!m_initialized) return;
        m_engine.GetContext().renderer->SetOverlayCallback({}); m_device->SetEventCallback({});
        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
        m_engine.Shutdown(); m_initialized = false;
    }

    void EditorApp::Draw()
    {
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        DrawMenuBar(); DrawHierarchy(); DrawInspector(); DrawAssets(); DrawConsole();
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && io.KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
            if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
            if (ImGui::IsKeyPressed(ImGuiKey_C) && m_selected != InvalidEntity) Prefab::Save(*m_engine.GetContext().world, m_selected, "Saved/EditorClipboard.prefab");
            if (ImGui::IsKeyPressed(ImGuiKey_V)) { RecordUndo(); const auto roots = Prefab::Instantiate(*m_engine.GetContext().world, "Saved/EditorClipboard.prefab"); if (!roots.empty()) m_selected = roots.front(); }
        }
        ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void EditorApp::DrawMenuBar()
    {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveCurrentScene();
            if (ImGui::MenuItem("New Scene")) { RecordUndo(); m_engine.GetSceneManager().NewScene(); m_selected = InvalidEntity; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_undo.empty())) Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !m_redo.empty())) Redo();
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (!m_engine.IsPlaying()) { if (ImGui::Button("Play")) { m_engine.EnterPlayMode(); m_messages.emplace_back("Play mode started"); } }
        else { if (ImGui::Button("Stop")) { m_engine.ExitPlayMode(); m_selected = InvalidEntity; m_messages.emplace_back("Play mode stopped; edit scene restored"); } }
        ImGui::SameLine(); ImGui::TextUnformatted(m_engine.IsPlaying() ? "Runtime" : "Editing");
        ImGui::EndMainMenuBar();
    }

    void EditorApp::DrawHierarchy()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float top = viewport->WorkPos.y;
        const float upperHeight = viewport->WorkSize.y * 0.67f;
        ImGui::SetNextWindowPos({viewport->WorkPos.x, top}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({260.0f, upperHeight}, ImGuiCond_Always);
        ImGui::Begin("Hierarchy");
        World& world = *m_engine.GetContext().world;
        if (!m_engine.IsPlaying() && ImGui::Button("+ Entity")) { RecordUndo(); m_selected = world.CreateEntity(); }
        std::vector<Entity> entities(world.GetEntityManager().GetAliveEntities().begin(), world.GetEntityManager().GetAliveEntities().end()); std::sort(entities.begin(), entities.end());
        std::function<void(Entity)> drawNode = [&](const Entity entity)
        {
            std::vector<Entity> children;
            for (const Entity candidate : entities) if (const auto* relation = world.GetComponent<RelationshipComponent>(candidate); relation && relation->parent == entity) children.push_back(candidate);
            const auto* name = world.GetComponent<NameComponent>(entity);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf; if (m_selected == entity) flags |= ImGuiTreeNodeFlags_Selected;
            const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<std::uintptr_t>(entity)), flags, "%s", name ? name->name.c_str() : "Entity");
            if (ImGui::IsItemClicked()) m_selected = entity;
            if (open) { for (const Entity child : children) drawNode(child); ImGui::TreePop(); }
        };
        for (const Entity entity : entities) { const auto* relation = world.GetComponent<RelationshipComponent>(entity); if (!relation || relation->parent == InvalidEntity || !world.IsAlive(relation->parent)) drawNode(entity); }
        if (!m_engine.IsPlaying() && m_selected != InvalidEntity && ImGui::IsKeyPressed(ImGuiKey_Delete)) { RecordUndo(); world.DestroyEntity(m_selected); m_selected = InvalidEntity; }
        ImGui::End();
    }

    void EditorApp::DrawInspector()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float width = 350.0f;
        const float upperHeight = viewport->WorkSize.y * 0.67f;
        ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - width, viewport->WorkPos.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({width, upperHeight}, ImGuiCond_Always);
        ImGui::Begin("Inspector"); World& world = *m_engine.GetContext().world;
        if (!world.IsAlive(m_selected)) { ImGui::TextDisabled("Select an entity"); ImGui::End(); return; }
        const bool editable = !m_engine.IsPlaying(); ImGui::BeginDisabled(!editable);
        if (auto* name = world.GetComponent<NameComponent>(m_selected))
        {
            char buffer[256]{}; std::strncpy(buffer, name->name.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Name", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) { RecordUndo(); name->name = buffer; }
        }
        if (auto* active = world.GetComponent<ActiveComponent>(m_selected)) { bool value = active->active; if (ImGui::Checkbox("Active", &value)) { RecordUndo(); active->active = value; } }
        if (auto* transform = world.GetComponent<TransformComponent>(m_selected))
        {
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat3("Position", &transform->position.x, 0.05f); if (ImGui::IsItemActivated()) RecordUndo();
            ImGui::DragFloat3("Rotation", &transform->rotation.x, 0.25f); if (ImGui::IsItemActivated()) RecordUndo();
            ImGui::DragFloat3("Scale", &transform->scale.x, 0.02f, 0.001f, 1000.0f); if (ImGui::IsItemActivated()) RecordUndo();
        }
        if (auto* tag = world.GetComponent<TagComponent>(m_selected))
        {
            char buffer[128]{}; std::strncpy(buffer, tag->tag.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Tag", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) { RecordUndo(); tag->tag = buffer; }
        }
        if (auto* layer = world.GetComponent<LayerComponent>(m_selected)) { int value = static_cast<int>(layer->layer); if (ImGui::InputInt("Layer", &value)) { RecordUndo(); layer->layer = static_cast<std::uint32_t>(std::max(0, value)); } }
        if (auto* mesh = world.GetComponent<MeshComponent>(m_selected))
        {
            ImGui::SeparatorText("Mesh Renderer");
            char meshPath[512]{}; char materialPath[512]{};
            std::strncpy(meshPath, mesh->meshPath.c_str(), sizeof(meshPath) - 1); std::strncpy(materialPath, mesh->materialPath.c_str(), sizeof(materialPath) - 1);
            if (ImGui::InputText("Mesh", meshPath, sizeof(meshPath), ImGuiInputTextFlags_EnterReturnsTrue)) { RecordUndo(); mesh->meshPath = meshPath; }
            if (ImGui::InputText("Material", materialPath, sizeof(materialPath), ImGuiInputTextFlags_EnterReturnsTrue)) { RecordUndo(); mesh->materialPath = materialPath; }
        }
        if (auto* camera = world.GetComponent<CameraComponent>(m_selected)) { ImGui::SeparatorText("Camera"); ImGui::DragFloat("FOV", &camera->fov, 0.25f, 1.0f, 179.0f); if (ImGui::IsItemActivated()) RecordUndo(); ImGui::Checkbox("Primary", &camera->primary); }
        if (auto* light = world.GetComponent<LightComponent>(m_selected)) { ImGui::SeparatorText("Light"); ImGui::ColorEdit3("Color", &light->color.x); if (ImGui::IsItemActivated()) RecordUndo(); ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 1000.0f); if (ImGui::IsItemActivated()) RecordUndo(); }
        if (ImGui::Button("Add Component")) ImGui::OpenPopup("AddComponentPopup");
        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            if (!world.HasComponent<MeshComponent>(m_selected) && ImGui::MenuItem("Mesh")) { RecordUndo(); world.AddComponent(m_selected, MeshComponent{}); }
            if (!world.HasComponent<CameraComponent>(m_selected) && ImGui::MenuItem("Camera")) { RecordUndo(); world.AddComponent(m_selected, CameraComponent{}); }
            if (!world.HasComponent<LightComponent>(m_selected) && ImGui::MenuItem("Light")) { RecordUndo(); world.AddComponent(m_selected, LightComponent{}); }
            if (!world.HasComponent<RigidBodyComponent>(m_selected) && ImGui::MenuItem("Rigid Body")) { RecordUndo(); world.AddComponent(m_selected, RigidBodyComponent{}); }
            if (!world.HasComponent<ColliderComponent>(m_selected) && ImGui::MenuItem("Collider")) { RecordUndo(); world.AddComponent(m_selected, ColliderComponent{}); }
            if (!world.HasComponent<AudioSourceComponent>(m_selected) && ImGui::MenuItem("Audio Source")) { RecordUndo(); world.AddComponent(m_selected, AudioSourceComponent{}); }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled(); ImGui::End();
    }

    void EditorApp::DrawAssets()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float top = viewport->WorkPos.y + viewport->WorkSize.y * 0.67f;
        const float halfWidth = viewport->WorkSize.x * 0.58f;
        ImGui::SetNextWindowPos({viewport->WorkPos.x, top}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({halfWidth, viewport->WorkPos.y + viewport->WorkSize.y - top}, ImGuiCond_Always);
        ImGui::Begin("Assets");
        static std::string selectedAsset;
#ifdef TINYENGINE_HAS_ASSIMP
        if (ImGui::Button("Import Model...")) ImGui::OpenPopup("ImportModelPopup");
        ImGui::SameLine();
        const auto selectedInfo = selectedAsset.empty() ? std::optional<AssetInfo>{} : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(selectedAsset);
        ImGui::BeginDisabled(!selectedInfo || selectedInfo->type != "Prefab" || selectedInfo->importSettings.count("source") == 0);
        if (ImGui::Button("Reimport"))
        {
            const std::filesystem::path source = selectedInfo->importSettings.at("source");
            ModelImportOptions options;
            if (const auto scale = selectedInfo->importSettings.find("scale"); scale != selectedInfo->importSettings.end()) { try { options.scale = std::stof(scale->second); } catch (...) {} }
            if (const auto flip = selectedInfo->importSettings.find("flipUvs"); flip != selectedInfo->importSettings.end()) options.flipUvs = flip->second == "true";
            const std::filesystem::path output = std::filesystem::path(selectedInfo->path).parent_path();
            const auto projectRoot = m_engine.GetProjectSettings().projectFile.empty() ? std::filesystem::current_path() : m_engine.GetProjectSettings().projectFile.parent_path();
            options.resourcePrefix = std::filesystem::relative(output, projectRoot);
            const auto result = AssimpImporter::ImportModel(source, output, options);
            m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory));
            m_messages.emplace_back(result.success ? "Reimported model: " + source.generic_string() : "Reimport failed: " + result.error);
        }
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("ImportModelPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            static char sourcePath[1024]{};
            static float scale = 1.0f;
            static bool flipUvs = false;
            ImGui::InputText("Source", sourcePath, sizeof(sourcePath));
            ImGui::DragFloat("Scale", &scale, 0.01f, 0.0001f, 1000.0f);
            ImGui::Checkbox("Flip UVs", &flipUvs);
            if (ImGui::Button("Import") && sourcePath[0] != '\0')
            {
                const std::filesystem::path source(sourcePath);
                const std::string folderName = source.stem().empty() ? "Model" : source.stem().string();
                const auto relativeOutput = m_engine.GetProjectSettings().assetDirectory / "Imported" / folderName;
                const auto output = m_engine.GetProjectSettings().Resolve(relativeOutput);
                ModelImportOptions options; options.scale = scale; options.flipUvs = flipUvs; options.resourcePrefix = relativeOutput;
                const auto result = AssimpImporter::ImportModel(source, output, options);
                if (result.success)
                {
                    m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory));
                    m_messages.emplace_back("Imported model: " + source.generic_string()); ImGui::CloseCurrentPopup();
                }
                else m_messages.emplace_back("Import failed: " + result.error);
                for (const auto& warning : result.warnings) m_messages.emplace_back("Import warning: " + warning);
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Separator();
#endif
        auto assets = m_engine.GetContext().resourceManager->GetAssetRegistry().GetAssets();
        std::sort(assets.begin(), assets.end(), [](const AssetInfo& a, const AssetInfo& b) { return a.path < b.path; });
        for (const auto& asset : assets)
        {
            ImGui::PushID(asset.id.c_str()); ImGui::TextDisabled("%s", asset.type.c_str()); ImGui::SameLine(90.0f);
            if (ImGui::Selectable(asset.path.c_str(), selectedAsset == asset.path)) selectedAsset = asset.path;
            if (!m_engine.IsPlaying() && asset.extension == ".prefab" && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                RecordUndo(); const auto roots = Prefab::Instantiate(*m_engine.GetContext().world, asset.path);
                if (!roots.empty()) { m_selected = roots.front(); m_messages.emplace_back("Instantiated prefab: " + asset.path); }
            }
            ImGui::PopID();
        }
        ImGui::End();
    }

    void EditorApp::DrawConsole()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float top = viewport->WorkPos.y + viewport->WorkSize.y * 0.67f;
        const float left = viewport->WorkPos.x + viewport->WorkSize.x * 0.58f;
        ImGui::SetNextWindowPos({left, top}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({viewport->WorkPos.x + viewport->WorkSize.x - left, viewport->WorkPos.y + viewport->WorkSize.y - top}, ImGuiCond_Always);
        ImGui::Begin("Console / Stats");
        const Renderer* renderer = m_engine.GetContext().renderer;
        ImGui::Text("Draw calls: %zu | Culled: %zu", renderer->GetLastDrawCallCount(), renderer->GetLastCulledCount());
        for (const auto& message : m_messages) ImGui::TextUnformatted(message.c_str());
        ImGui::End();
    }

    void EditorApp::RecordUndo() { if (m_engine.IsPlaying()) return; m_undo.push_back(m_engine.GetSceneManager().CreateSnapshot()); if (m_undo.size() > 64) m_undo.erase(m_undo.begin()); m_redo.clear(); }
    void EditorApp::Undo() { if (m_engine.IsPlaying() || m_undo.empty()) return; m_redo.push_back(m_engine.GetSceneManager().CreateSnapshot()); m_engine.GetSceneManager().RestoreSnapshot(m_undo.back()); m_undo.pop_back(); m_selected = InvalidEntity; }
    void EditorApp::Redo() { if (m_engine.IsPlaying() || m_redo.empty()) return; m_undo.push_back(m_engine.GetSceneManager().CreateSnapshot()); m_engine.GetSceneManager().RestoreSnapshot(m_redo.back()); m_redo.pop_back(); m_selected = InvalidEntity; }
    bool EditorApp::SaveCurrentScene()
    {
        auto path = m_engine.GetSceneManager().GetCurrentScene();
        if (path.empty()) path = m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().startupScene);
        const bool saved = m_engine.SaveScene(path); m_messages.emplace_back(saved ? "Scene saved: " + path.generic_string() : "Scene save failed"); return saved;
    }
}
