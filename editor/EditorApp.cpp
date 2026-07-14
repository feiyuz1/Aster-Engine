#include "EditorApp.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/Reflection.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/Project/ProjectPackager.hpp"
#include "MiniEngine/Platform/Platform.hpp"
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
#include <cmath>
#include <limits>
#include <sstream>

namespace MiniEngine
{
    namespace
    {
        Vec3 TransformPoint(const Mat4& matrix, const Vec3& point)
        {
            return {
                matrix.At(0, 0) * point.x + matrix.At(0, 1) * point.y + matrix.At(0, 2) * point.z + matrix.At(0, 3),
                matrix.At(1, 0) * point.x + matrix.At(1, 1) * point.y + matrix.At(1, 2) * point.z + matrix.At(1, 3),
                matrix.At(2, 0) * point.x + matrix.At(2, 1) * point.y + matrix.At(2, 2) * point.z + matrix.At(2, 3)
            };
        }

        bool ProjectToScreen(const FrameView& view, const Vec3& point, const float minimumX, const float minimumY, const float width, const float height, ImVec2& screen)
        {
            const Mat4 matrix = view.projection * view.view;
            const float x = matrix.At(0, 0) * point.x + matrix.At(0, 1) * point.y + matrix.At(0, 2) * point.z + matrix.At(0, 3);
            const float y = matrix.At(1, 0) * point.x + matrix.At(1, 1) * point.y + matrix.At(1, 2) * point.z + matrix.At(1, 3);
            const float w = matrix.At(3, 0) * point.x + matrix.At(3, 1) * point.y + matrix.At(3, 2) * point.z + matrix.At(3, 3);
            if (w <= 0.0001f) return false;
            screen = {minimumX + (x / w * 0.5f + 0.5f) * width, minimumY + (1.0f - (y / w * 0.5f + 0.5f)) * height}; return true;
        }

        bool IntersectRayAabb(const Ray& ray, const Aabb& bounds, float& distance)
        {
            float minimumDistance = 0.0f, maximumDistance = std::numeric_limits<float>::max();
            for (int axis = 0; axis < 3; ++axis)
            {
                const float origin = axis == 0 ? ray.origin.x : axis == 1 ? ray.origin.y : ray.origin.z;
                const float direction = axis == 0 ? ray.direction.x : axis == 1 ? ray.direction.y : ray.direction.z;
                const float minimum = axis == 0 ? bounds.minimum.x : axis == 1 ? bounds.minimum.y : bounds.minimum.z;
                const float maximum = axis == 0 ? bounds.maximum.x : axis == 1 ? bounds.maximum.y : bounds.maximum.z;
                if (std::abs(direction) < 1e-6f) { if (origin < minimum || origin > maximum) return false; continue; }
                float nearValue = (minimum - origin) / direction, farValue = (maximum - origin) / direction;
                if (nearValue > farValue) std::swap(nearValue, farValue);
                minimumDistance = std::max(minimumDistance, nearValue); maximumDistance = std::min(maximumDistance, farValue);
                if (minimumDistance > maximumDistance) return false;
            }
            distance = minimumDistance; return true;
        }

        Aabb TransformBounds(const Aabb& bounds, const Mat4& matrix)
        {
            const Vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
            const Vec3 extent = (bounds.maximum - bounds.minimum) * 0.5f;
            const Vec3 worldCenter = TransformPoint(matrix, center);
            const Vec3 worldExtent{
                std::abs(matrix.At(0, 0)) * extent.x + std::abs(matrix.At(0, 1)) * extent.y + std::abs(matrix.At(0, 2)) * extent.z,
                std::abs(matrix.At(1, 0)) * extent.x + std::abs(matrix.At(1, 1)) * extent.y + std::abs(matrix.At(1, 2)) * extent.z,
                std::abs(matrix.At(2, 0)) * extent.x + std::abs(matrix.At(2, 1)) * extent.y + std::abs(matrix.At(2, 2)) * extent.z
            };
            return {worldCenter - worldExtent, worldCenter + worldExtent};
        }

        float DistanceToSegment(const ImVec2 point, const ImVec2 start, const ImVec2 end)
        {
            const ImVec2 segment{end.x - start.x, end.y - start.y}, offset{point.x - start.x, point.y - start.y};
            const float lengthSquared = segment.x * segment.x + segment.y * segment.y;
            const float t = lengthSquared > 0.0f ? std::clamp((offset.x * segment.x + offset.y * segment.y) / lengthSquared, 0.0f, 1.0f) : 0.0f;
            const float dx = point.x - (start.x + segment.x * t), dy = point.y - (start.y + segment.y * t); return std::sqrt(dx * dx + dy * dy);
        }
    }

    bool EditorApp::Initialize(const std::filesystem::path& projectFile)
    {
        if (!m_engine.Initialize(projectFile, false)) return false;
        m_device = dynamic_cast<SdlGlRenderDevice*>(m_engine.GetContext().renderer->GetDevice());
        if (!m_device) { m_engine.Shutdown(); return false; }
        if (!m_device->EnableSceneRenderTarget(true, 960, 540)) { m_engine.Shutdown(); return false; }
        m_engine.GetContext().renderer->SetCameraOverride(true, m_editorCameraPosition, m_editorCameraRotation, m_editorCameraFov);
        SDL_SetWindowTitle(static_cast<SDL_Window*>(m_device->GetNativeWindow()), "TinyEngine Editor");
        IMGUI_CHECKVERSION(); ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        if (!ImGui_ImplSDL2_InitForOpenGL(static_cast<SDL_Window*>(m_device->GetNativeWindow()), m_device->GetGlContext()) || !ImGui_ImplOpenGL3_Init("#version 330"))
        { ImGui::DestroyContext(); m_engine.Shutdown(); return false; }
        m_device->SetEventCallback([](const void* event) { ImGui_ImplSDL2_ProcessEvent(static_cast<const SDL_Event*>(event)); });
        m_engine.GetContext().renderer->SetOverlayCallback([this]() { Draw(); });
        m_messages.emplace_back("Editor initialized. Scene changes are isolated from Play mode.");
        m_sceneDocuments.push_back({m_engine.GetSceneManager().GetCurrentScene(), m_engine.GetSceneManager().CreateSnapshot(), false});
#ifdef TINYENGINE_HAS_ASSIMP
        m_importQueue.Initialize(m_engine.GetContext().jobSystem);
#endif
        if (std::filesystem::is_regular_file(GetAutosavePath())) m_messages.emplace_back("Recovery autosave available from File > Restore Autosave.");
        UpdateWindowTitle();
        m_initialized = true; return true;
    }

    void EditorApp::Run() { if (m_initialized) m_engine.Run(); }

    void EditorApp::Shutdown()
    {
        if (!m_initialized) return;
        if (m_sceneDirty && !m_engine.IsPlaying()) Autosave();
        m_engine.GetContext().renderer->SetOverlayCallback({}); m_device->SetEventCallback({}); m_device->EnableSceneRenderTarget(false);
        ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
        m_engine.Shutdown(); m_initialized = false;
    }

    void EditorApp::Draw()
    {
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        DrawMenuBar(); DrawHierarchy(); DrawInspector(); DrawSceneView(); DrawAssets(); DrawConsole();
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && io.KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
            if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
            if (ImGui::IsKeyPressed(ImGuiKey_S)) SaveCurrentScene();
            if (ImGui::IsKeyPressed(ImGuiKey_C) && m_selected != InvalidEntity) Prefab::Save(*m_engine.GetContext().world, m_selected, "Saved/EditorClipboard.prefab");
            if (ImGui::IsKeyPressed(ImGuiKey_V)) { RecordUndo(); const auto roots = Prefab::Instantiate(*m_engine.GetContext().world, "Saved/EditorClipboard.prefab"); if (!roots.empty()) m_selected = roots.front(); }
        }
        if (m_sceneDirty && !m_engine.IsPlaying() && ImGui::GetTime() - m_lastAutosaveTime >= 30.0) Autosave();
        UpdateWindowTitle();
        ImGui::Render(); ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void EditorApp::DrawMenuBar()
    {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveCurrentScene();
            if (ImGui::MenuItem("New Scene")) NewSceneDocument();
            const auto autosavePath = GetAutosavePath();
            ImGui::BeginDisabled(!std::filesystem::is_regular_file(autosavePath) || m_engine.IsPlaying());
            if (ImGui::MenuItem("Restore Autosave")) RestoreAutosave();
            ImGui::EndDisabled();
            const bool canPackage = !m_engine.IsPlaying() && !m_engine.GetProjectSettings().projectFile.empty();
            ImGui::BeginDisabled(!canPackage);
            if (ImGui::MenuItem("Package Project"))
            {
                SaveCurrentScene();
                char* basePath = SDL_GetBasePath();
                const std::filesystem::path executableDirectory = basePath ? std::filesystem::path(basePath) : std::filesystem::current_path();
                if (basePath) SDL_free(basePath);
#ifdef _WIN32
                const auto gameExecutable = executableDirectory / "tinyengine_game.exe";
#else
                const auto gameExecutable = executableDirectory / "tinyengine_game";
#endif
                const auto output = m_engine.GetProjectSettings().Resolve(std::filesystem::path("Dist") / m_engine.GetProjectSettings().name);
                const auto result = ProjectPackager::Package({m_engine.GetProjectSettings().projectFile, output, gameExecutable});
                if (result.success) m_messages.emplace_back("Packaged project: " + result.outputDirectory.generic_string());
                else for (const auto& error : result.errors) m_messages.emplace_back("Package error: " + error);
            }
            ImGui::EndDisabled();
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
        if (!world.IsAlive(m_selected))
        {
            const auto asset = m_selectedAsset.empty() ? std::optional<AssetInfo>{} : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
            if (asset && asset->type == "Material")
            {
                if (auto material = m_engine.GetContext().resourceManager->Load<MaterialResource>(asset->path))
                {
                    ImGui::SeparatorText("Material Asset"); ImGui::TextWrapped("%s", asset->path.c_str());
                    Vec4 color = material->GetBaseColor(); float metallic = material->GetMetallic(), roughness = material->GetRoughness(); Vec3 emissive = material->GetEmissive(); bool transparent = material->IsTransparent(); bool changed = false;
                    changed = ImGui::ColorEdit4("Base Color", &color.x) || changed; changed = ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f) || changed;
                    changed = ImGui::SliderFloat("Roughness", &roughness, 0.04f, 1.0f) || changed; changed = ImGui::ColorEdit3("Emissive", &emissive.x) || changed; changed = ImGui::Checkbox("Transparent", &transparent) || changed;
                    std::string shader = material->GetShaderPath(), albedo = material->GetTexturePath(), normal = material->GetNormalTexturePath(), metallicMap = material->GetMetallicTexturePath();
                    std::string roughnessMap = material->GetRoughnessTexturePath(), ao = material->GetAoTexturePath(), emissiveMap = material->GetEmissiveTexturePath();
                    auto editPath = [&](const char* label, std::string& path)
                    {
                        char buffer[512]{}; std::strncpy(buffer, path.c_str(), sizeof(buffer) - 1);
                        if (ImGui::InputText(label, buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) { path = buffer; changed = true; }
                    };
                    editPath("Shader", shader); editPath("Albedo", albedo); editPath("Normal", normal); editPath("Metallic Map", metallicMap); editPath("Roughness Map", roughnessMap); editPath("AO Map", ao); editPath("Emissive Map", emissiveMap);
                    if (changed)
                    {
                        std::ostringstream output; output << "color " << color.x << ' ' << color.y << ' ' << color.z << ' ' << color.w << "\nmetallic " << metallic << "\nroughness " << roughness << "\nemissive " << emissive.x << ' ' << emissive.y << ' ' << emissive.z << '\n';
                        if (!shader.empty()) output << "shader " << shader << '\n'; if (!albedo.empty()) output << "texture " << albedo << '\n'; if (!normal.empty()) output << "normalTexture " << normal << '\n';
                        if (!metallicMap.empty()) output << "metallicTexture " << metallicMap << '\n'; if (!roughnessMap.empty()) output << "roughnessTexture " << roughnessMap << '\n'; if (!ao.empty()) output << "aoTexture " << ao << '\n'; if (!emissiveMap.empty()) output << "emissiveTexture " << emissiveMap << '\n';
                        output << "blend " << (transparent ? "alpha" : "opaque") << '\n';
                        if (PlatformFile::WriteAllText(asset->path, output.str()) && m_engine.GetContext().resourceManager->Reload(asset->path)) m_messages.emplace_back("Material saved: " + asset->path);
                        else m_messages.emplace_back("Material save failed: " + asset->path);
                    }
                }
            }
            else ImGui::TextDisabled("Select an entity or material asset");
            ImGui::End(); return;
        }
        const bool editable = !m_engine.IsPlaying(); ImGui::BeginDisabled(!editable);
        const auto& registry = ReflectionRegistry::Get();
        for (const auto& component : registry.GetComponents())
        {
            if (!component.has(world, m_selected)) continue;
            ImGui::PushID(component.name.c_str());
            ImGui::SeparatorText(component.name.c_str());
            for (const auto& property : component.properties)
            {
                ImGui::PushID(property.name.c_str());
                void* value = property.access ? property.access(world, m_selected) : nullptr;
                bool changed = false;
                switch (property.type)
                {
                case ReflectedPropertyType::Bool:
                {
                    bool edited = value ? *static_cast<bool*>(value) : false;
                    changed = value && ImGui::Checkbox(property.name.c_str(), &edited);
                    if (changed) { RecordUndo(); *static_cast<bool*>(value) = edited; }
                    break;
                }
                case ReflectedPropertyType::UInt:
                {
                    std::uint32_t edited = value ? *static_cast<std::uint32_t*>(value) : 0;
                    changed = value && ImGui::InputScalar(property.name.c_str(), ImGuiDataType_U32, &edited);
                    if (changed) { RecordUndo(); *static_cast<std::uint32_t*>(value) = edited; }
                    break;
                }
                case ReflectedPropertyType::Entity:
                {
                    Entity edited = value ? *static_cast<Entity*>(value) : InvalidEntity;
                    changed = value && ImGui::InputScalar(property.name.c_str(), ImGuiDataType_U64, &edited);
                    if (changed && (edited == InvalidEntity || world.IsAlive(edited))) { RecordUndo(); *static_cast<Entity*>(value) = edited; }
                    break;
                }
                case ReflectedPropertyType::Float:
                {
                    float edited = value ? *static_cast<float*>(value) : 0.0f;
                    const float minimum = property.hasRange ? property.minimum : 0.0f;
                    const float maximum = property.hasRange ? property.maximum : 0.0f;
                    changed = value && ImGui::DragFloat(property.name.c_str(), &edited, property.speed, minimum, maximum);
                    if (ImGui::IsItemActivated()) RecordUndo();
                    if (changed) *static_cast<float*>(value) = edited;
                    break;
                }
                case ReflectedPropertyType::Vec3:
                {
                    Vec3 edited = value ? *static_cast<Vec3*>(value) : Vec3{};
                    if (property.color) changed = value && ImGui::ColorEdit3(property.name.c_str(), &edited.x);
                    else
                    {
                        const float minimum = property.hasRange ? property.minimum : 0.0f;
                        const float maximum = property.hasRange ? property.maximum : 0.0f;
                        changed = value && ImGui::DragFloat3(property.name.c_str(), &edited.x, property.speed, minimum, maximum);
                    }
                    if (ImGui::IsItemActivated()) RecordUndo();
                    if (changed) *static_cast<Vec3*>(value) = edited;
                    break;
                }
                case ReflectedPropertyType::String:
                {
                    auto* text = static_cast<std::string*>(value);
                    char buffer[512]{};
                    if (text) std::strncpy(buffer, text->c_str(), sizeof(buffer) - 1);
                    if (text && ImGui::InputText(property.name.c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
                    { RecordUndo(); *text = buffer; changed = true; }
                    if (text && !property.assetType.empty() && ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TINY_ASSET_PATH"))
                        {
                            const std::string path = static_cast<const char*>(payload->Data);
                            const auto asset = m_engine.GetContext().resourceManager->GetAssetRegistry().Find(path);
                            const bool accepted = asset && (asset->type == property.assetType || (property.assetType == "Animation" && asset->extension == ".anim"));
                            if (accepted) { RecordUndo(); *text = path; changed = true; }
                            else m_messages.emplace_back("Inspector rejected " + path + "; expected " + property.assetType);
                        }
                        ImGui::EndDragDropTarget();
                    }
                    break;
                }
                case ReflectedPropertyType::Enum:
                {
                    int edited = property.getEnum ? property.getEnum(world, m_selected) : 0;
                    const char* preview = edited >= 0 && static_cast<std::size_t>(edited) < property.enumNames.size() ? property.enumNames[edited].c_str() : "Unknown";
                    if (ImGui::BeginCombo(property.name.c_str(), preview))
                    {
                        for (std::size_t index = 0; index < property.enumNames.size(); ++index)
                            if (ImGui::Selectable(property.enumNames[index].c_str(), edited == static_cast<int>(index)))
                            { RecordUndo(); property.setEnum(world, m_selected, static_cast<int>(index)); changed = true; }
                        ImGui::EndCombo();
                    }
                    break;
                }
                }
                (void)changed;
                ImGui::PopID();
            }
            if (component.removable)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
                if (ImGui::SmallButton("Remove Component")) { RecordUndo(); component.remove(world, m_selected); }
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Add Component")) ImGui::OpenPopup("AddComponentPopup");
        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            for (const auto& component : registry.GetComponents())
                if (component.removable && !component.has(world, m_selected) && ImGui::MenuItem(component.name.c_str()))
                { RecordUndo(); component.add(world, m_selected); }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled(); ImGui::End();
    }

    void EditorApp::DrawSceneView()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float left = viewport->WorkPos.x + 260.0f, top = viewport->WorkPos.y;
        const float width = std::max(160.0f, viewport->WorkSize.x - 610.0f), height = viewport->WorkSize.y * 0.67f;
        ImGui::SetNextWindowPos({left, top}, ImGuiCond_Always); ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
        ImGui::Begin("Scene View", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        DrawSceneTabs();
        if (ImGui::Button("W Translate")) m_gizmoMode = GizmoMode::Translate; ImGui::SameLine();
        if (ImGui::Button("E Rotate")) m_gizmoMode = GizmoMode::Rotate; ImGui::SameLine();
        if (ImGui::Button("R Scale")) m_gizmoMode = GizmoMode::Scale; ImGui::SameLine();
        ImGui::Checkbox("Snap", &m_gizmoSnap); ImGui::SameLine();
        float* snapValue = m_gizmoMode == GizmoMode::Translate ? &m_translateSnap : m_gizmoMode == GizmoMode::Rotate ? &m_rotateSnap : &m_scaleSnap;
        ImGui::SetNextItemWidth(70.0f); ImGui::DragFloat("##SnapValue", snapValue, 0.05f, 0.001f, 100.0f, "%.2f");
        float exposure = m_device->GetExposure(); ImGui::SameLine(); ImGui::SetNextItemWidth(70.0f);
        if (ImGui::DragFloat("Exposure", &exposure, 0.02f, 0.05f, 16.0f, "%.2f")) m_device->SetExposure(exposure);
        bool fxaa = m_device->IsFxaaEnabled(); ImGui::SameLine(); if (ImGui::Checkbox("FXAA", &fxaa)) m_device->SetFxaaEnabled(fxaa);
        ImGui::SameLine(); if (ImGui::SmallButton("Reload Shaders"))
        {
            if (m_device->ReloadShaders()) m_messages.emplace_back("Shaders reloaded");
            else m_messages.emplace_back("Shader reload failed; previous program retained: " + m_device->GetLastShaderError());
        }
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const int targetWidth = std::max(1, static_cast<int>(available.x)), targetHeight = std::max(1, static_cast<int>(available.y));
        m_device->ResizeSceneRenderTarget(targetWidth, targetHeight);
        const ImVec2 imageMinimum = ImGui::GetCursorScreenPos();
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(m_device->GetSceneColorTexture())), available, {0, 1}, {1, 0});
        const bool hovered = ImGui::IsItemHovered();
        if (!m_engine.IsPlaying() && hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) m_gizmoMode = GizmoMode::Translate;
            if (ImGui::IsKeyPressed(ImGuiKey_E)) m_gizmoMode = GizmoMode::Rotate;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_gizmoMode = GizmoMode::Scale;
        }
        ImGuiIO& io = ImGui::GetIO();
        const float pitch = DegreesToRadians(m_editorCameraRotation.x), yaw = DegreesToRadians(m_editorCameraRotation.y);
        const Vec3 forward = Normalize(Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
        const Vec3 right = Normalize(Cross(forward, {0.0f, 1.0f, 0.0f})); const Vec3 up = Normalize(Cross(right, forward));
        if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            m_editorCameraRotation.y += io.MouseDelta.x * 0.18f; m_editorCameraRotation.x = std::clamp(m_editorCameraRotation.x - io.MouseDelta.y * 0.18f, -89.0f, 89.0f);
            const float speed = (io.KeyShift ? 12.0f : 4.0f) * io.DeltaTime;
            if (ImGui::IsKeyDown(ImGuiKey_W)) m_editorCameraPosition += forward * speed;
            if (ImGui::IsKeyDown(ImGuiKey_S)) m_editorCameraPosition -= forward * speed;
            if (ImGui::IsKeyDown(ImGuiKey_D)) m_editorCameraPosition += right * speed;
            if (ImGui::IsKeyDown(ImGuiKey_A)) m_editorCameraPosition -= right * speed;
            if (ImGui::IsKeyDown(ImGuiKey_E)) m_editorCameraPosition += up * speed;
            if (ImGui::IsKeyDown(ImGuiKey_Q)) m_editorCameraPosition -= up * speed;
        }
        if (hovered && std::abs(io.MouseWheel) > 0.0f) m_editorCameraPosition += forward * (io.MouseWheel * 0.8f);
        m_engine.GetContext().renderer->SetCameraOverride(!m_engine.IsPlaying(), m_editorCameraPosition, m_editorCameraRotation, m_editorCameraFov);
        const bool gizmoConsumed = !m_engine.IsPlaying() && DrawGizmo(imageMinimum.x, imageMinimum.y, available.x, available.y);
        if (!m_engine.IsPlaying() && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoConsumed)
        {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float normalizedX = (mouse.x - imageMinimum.x) / std::max(1.0f, available.x);
            const float normalizedY = (mouse.y - imageMinimum.y) / std::max(1.0f, available.y);
            m_selected = PickEntity(normalizedX, normalizedY);
        }
        if (!m_engine.IsPlaying() && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TINY_ASSET_PATH"))
            {
                const std::string path(static_cast<const char*>(payload->Data));
                const auto asset = m_engine.GetContext().resourceManager->GetAssetRegistry().Find(path);
                if (asset)
                {
                    World& world = *m_engine.GetContext().world; RecordUndo();
                    if (asset->type == "Prefab")
                    {
                        const auto roots = Prefab::Instantiate(world, path); if (!roots.empty()) m_selected = roots.front();
                    }
                    else if (asset->type == "Mesh")
                    {
                        if (!world.IsAlive(m_selected)) m_selected = world.CreateEntity(std::filesystem::path(path).stem().string());
                        if (auto* mesh = world.GetComponent<MeshComponent>(m_selected)) mesh->meshPath = path;
                        else world.AddComponent(m_selected, MeshComponent{path, "assets/default.mat"});
                    }
                    else if (asset->type == "Material" && world.IsAlive(m_selected))
                    {
                        if (auto* mesh = world.GetComponent<MeshComponent>(m_selected)) mesh->materialPath = path;
                    }
                    m_messages.emplace_back("Dropped asset: " + path);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::End();
    }

    Entity EditorApp::PickEntity(const float normalizedX, const float normalizedY) const
    {
        const float pitch = DegreesToRadians(m_editorCameraRotation.x), yaw = DegreesToRadians(m_editorCameraRotation.y);
        const Vec3 forward = Normalize(Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
        const Vec3 right = Normalize(Cross(forward, {0.0f, 1.0f, 0.0f})), up = Normalize(Cross(right, forward));
        const float tangent = std::tan(DegreesToRadians(m_editorCameraFov) * 0.5f), aspect = std::max(0.01f, m_device->GetAspectRatio());
        const float x = normalizedX * 2.0f - 1.0f, y = 1.0f - normalizedY * 2.0f;
        const Ray ray{m_editorCameraPosition, Normalize(forward + right * (x * tangent * aspect) + up * (y * tangent))};
        World& world = *m_engine.GetContext().world; Entity selected = InvalidEntity; float closest = std::numeric_limits<float>::max();
        for (const Entity entity : world.GetEntityManager().GetAliveEntities())
        {
            if (const auto* active = world.GetComponent<ActiveComponent>(entity); active && !active->active) continue;
            Aabb bounds{}; bool hasBounds = false;
            if (const auto* meshComponent = world.GetComponent<MeshComponent>(entity))
            {
                if (const auto mesh = m_engine.GetContext().resourceManager->Load<MeshResource>(meshComponent->meshPath)) { bounds = TransformBounds(mesh->GetBounds(), world.GetWorldMatrix(entity)); hasBounds = true; }
            }
            else if (const auto* collider = world.GetComponent<ColliderComponent>(entity))
            {
                Vec3 extent = collider->shape == ColliderShape::Box ? collider->halfExtents : Vec3{collider->radius, collider->shape == ColliderShape::Capsule ? collider->height * 0.5f + collider->radius : collider->radius, collider->radius};
                bounds = TransformBounds({extent * -1.0f, extent}, world.GetWorldMatrix(entity)); hasBounds = true;
            }
            float distance = 0.0f;
            if (hasBounds && IntersectRayAabb(ray, bounds, distance) && distance < closest) { closest = distance; selected = entity; }
        }
        return selected;
    }

    bool EditorApp::DrawGizmo(const float minimumX, const float minimumY, const float width, const float height)
    {
        World& world = *m_engine.GetContext().world;
        if (!world.IsAlive(m_selected) || !world.HasComponent<TransformComponent>(m_selected)) { m_activeGizmoAxis = -1; return false; }
        const Mat4 worldMatrix = world.GetWorldMatrix(m_selected); const Vec3 origin{worldMatrix.At(0, 3), worldMatrix.At(1, 3), worldMatrix.At(2, 3)};
        const float handleLength = std::max(0.5f, Length(origin - m_editorCameraPosition) * 0.13f);
        const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; const ImU32 colors[3] = {IM_COL32(235, 70, 70, 255), IM_COL32(70, 220, 100, 255), IM_COL32(75, 135, 245, 255)};
        ImVec2 start; if (!ProjectToScreen(m_engine.GetContext().renderer->GetFrameView(), origin, minimumX, minimumY, width, height, start)) return false;
        ImDrawList* drawList = ImGui::GetWindowDrawList(); const ImVec2 mouse = ImGui::GetMousePos(); bool hovered = false; int hoveredAxis = -1;
        for (int axis = 0; axis < 3; ++axis)
        {
            ImVec2 end; if (!ProjectToScreen(m_engine.GetContext().renderer->GetFrameView(), origin + axes[axis] * handleLength, minimumX, minimumY, width, height, end)) continue;
            const bool axisHovered = DistanceToSegment(mouse, start, end) <= 7.0f; if (axisHovered) { hovered = true; hoveredAxis = axis; }
            drawList->AddLine(start, end, axisHovered || m_activeGizmoAxis == axis ? IM_COL32(255, 225, 80, 255) : colors[axis], axisHovered ? 4.0f : 2.5f);
            drawList->AddCircleFilled(end, 5.0f, colors[axis]);
        }
        if (hoveredAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            RecordUndo(); m_activeGizmoAxis = hoveredAxis; m_gizmoAccumulatedMotion = 0.0f; m_gizmoAppliedMotion = 0.0f;
        }
        if (m_activeGizmoAxis >= 0)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_activeGizmoAxis = -1;
            else if (auto* transform = world.GetComponent<TransformComponent>(m_selected))
            {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                const float rawMotion = m_gizmoMode == GizmoMode::Translate ? (delta.x - delta.y) * 0.01f * handleLength :
                    m_gizmoMode == GizmoMode::Rotate ? (delta.x - delta.y) * 0.5f : (delta.x - delta.y) * 0.01f;
                m_gizmoAccumulatedMotion += rawMotion;
                const float step = m_gizmoMode == GizmoMode::Translate ? m_translateSnap : m_gizmoMode == GizmoMode::Rotate ? m_rotateSnap : m_scaleSnap;
                const float targetMotion = m_gizmoSnap ? std::round(m_gizmoAccumulatedMotion / std::max(0.001f, step)) * step : m_gizmoAccumulatedMotion;
                const float motion = targetMotion - m_gizmoAppliedMotion; m_gizmoAppliedMotion = targetMotion;
                if (m_gizmoMode == GizmoMode::Translate) { if (m_activeGizmoAxis == 0) transform->position.x += motion; else if (m_activeGizmoAxis == 1) transform->position.y += motion; else transform->position.z += motion; }
                else if (m_gizmoMode == GizmoMode::Rotate) { if (m_activeGizmoAxis == 0) transform->rotation.x += motion; else if (m_activeGizmoAxis == 1) transform->rotation.y += motion; else transform->rotation.z += motion; }
                else { if (m_activeGizmoAxis == 0) transform->scale.x = std::max(0.001f, transform->scale.x + motion); else if (m_activeGizmoAxis == 1) transform->scale.y = std::max(0.001f, transform->scale.y + motion); else transform->scale.z = std::max(0.001f, transform->scale.z + motion); }
            }
        }
        return hovered || m_activeGizmoAxis >= 0;
    }

    void EditorApp::DrawAssets()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float top = viewport->WorkPos.y + viewport->WorkSize.y * 0.67f;
        const float halfWidth = viewport->WorkSize.x * 0.58f;
        ImGui::SetNextWindowPos({viewport->WorkPos.x, top}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({halfWidth, viewport->WorkPos.y + viewport->WorkSize.y - top}, ImGuiCond_Always);
        ImGui::Begin("Assets");
#ifdef TINYENGINE_HAS_ASSIMP
        if (m_activeImportTask != 0)
        {
            const auto task = m_importQueue.Get(m_activeImportTask);
            ImGui::ProgressBar(task.progress, ImVec2(220.0f, 0.0f), task.status == AssetImportStatus::Queued ? "Queued" : task.status == AssetImportStatus::Running ? "Importing" : "Finishing");
            if ((task.status == AssetImportStatus::Queued || task.status == AssetImportStatus::Running) && ImGui::Button("Cancel Import")) m_importQueue.Cancel(m_activeImportTask);
            if (task.status == AssetImportStatus::Completed || task.status == AssetImportStatus::Failed || task.status == AssetImportStatus::Cancelled)
            {
                if (task.status == AssetImportStatus::Completed)
                {
                    m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory));
                    m_messages.emplace_back("Background model import completed");
                }
                else if (task.status == AssetImportStatus::Cancelled) m_messages.emplace_back("Background model import cancelled");
                else m_messages.emplace_back("Background model import failed: " + task.result.error);
                for (const auto& warning : task.result.warnings) m_messages.emplace_back("Import warning: " + warning);
                m_importQueue.ClearFinished(); m_activeImportTask = 0;
            }
            ImGui::Separator();
        }
        ImGui::BeginDisabled(m_activeImportTask != 0);
        if (ImGui::Button("Import Model...")) ImGui::OpenPopup("ImportModelPopup");
        ImGui::SameLine();
        const auto selectedInfo = m_selectedAsset.empty() ? std::optional<AssetInfo>{} : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
        ImGui::BeginDisabled(!selectedInfo || selectedInfo->type != "Prefab" || selectedInfo->importSettings.count("source") == 0 || m_activeImportTask != 0);
        if (ImGui::Button("Reimport"))
        {
            const std::filesystem::path source = selectedInfo->importSettings.at("source");
            ModelImportOptions options;
            if (const auto scale = selectedInfo->importSettings.find("scale"); scale != selectedInfo->importSettings.end()) { try { options.scale = std::stof(scale->second); } catch (...) {} }
            if (const auto flip = selectedInfo->importSettings.find("flipUvs"); flip != selectedInfo->importSettings.end()) options.flipUvs = flip->second == "true";
            const std::filesystem::path output = std::filesystem::path(selectedInfo->path).parent_path();
            const auto projectRoot = m_engine.GetProjectSettings().projectFile.empty() ? std::filesystem::current_path() : m_engine.GetProjectSettings().projectFile.parent_path();
            options.resourcePrefix = std::filesystem::relative(output, projectRoot);
            m_activeImportTask = m_importQueue.Submit(source, output, options);
            m_messages.emplace_back("Queued model reimport: " + source.generic_string());
        }
        ImGui::EndDisabled();
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
                m_activeImportTask = m_importQueue.Submit(source, output, options);
                m_messages.emplace_back("Queued model import: " + source.generic_string()); ImGui::CloseCurrentPopup();
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
            if (ImGui::Selectable(asset.path.c_str(), m_selectedAsset == asset.path)) m_selectedAsset = asset.path;
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("TINY_ASSET_PATH", asset.path.c_str(), asset.path.size() + 1);
                ImGui::TextUnformatted(asset.path.c_str()); ImGui::EndDragDropSource();
            }
            if (!m_engine.IsPlaying() && asset.extension == ".prefab" && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                RecordUndo(); const auto roots = Prefab::Instantiate(*m_engine.GetContext().world, asset.path);
                if (!roots.empty()) { m_selected = roots.front(); m_messages.emplace_back("Instantiated prefab: " + asset.path); }
            }
            if (!m_engine.IsPlaying() && asset.extension == ".scene" && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) OpenSceneDocument(asset.path);
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
        ImGui::Text("Draw calls: %zu | Culled: %zu | LOD: %zu | GPU: %.2f ms", renderer->GetLastDrawCallCount(), renderer->GetLastCulledCount(), renderer->GetLastLodSwitchCount(), m_device->GetLastGpuFrameMilliseconds());
        const auto& profile = renderer->GetLastFrameProfile();
        ImGui::Text("CPU: %.2f ms (queue %.2f | shadow %.2f | scene/UI %.2f)", profile.totalMilliseconds, profile.queueMilliseconds, profile.shadowMilliseconds, profile.sceneMilliseconds);
        for (const auto& message : m_messages) ImGui::TextUnformatted(message.c_str());
        ImGui::End();
    }

    void EditorApp::DrawSceneTabs()
    {
        if (!ImGui::BeginTabBar("SceneDocuments")) return;
        for (std::size_t index = 0; index < m_sceneDocuments.size(); ++index)
        {
            const auto& document = m_sceneDocuments[index];
            const std::string label = (document.path.empty() ? "Untitled" : document.path.filename().string()) + "##Scene" + std::to_string(index);
            ImGuiTabItemFlags flags = (index == m_activeSceneDocument ? m_sceneDirty : document.dirty) ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem(label.c_str(), nullptr, flags))
            {
                if (index != m_activeSceneDocument) SwitchSceneDocument(index);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    bool EditorApp::SwitchSceneDocument(const std::size_t index)
    {
        if (m_engine.IsPlaying() || index >= m_sceneDocuments.size() || index == m_activeSceneDocument) return false;
        auto& current = m_sceneDocuments[m_activeSceneDocument]; current.snapshot = m_engine.GetSceneManager().CreateSnapshot(); current.dirty = m_sceneDirty;
        if (!m_engine.GetSceneManager().RestoreSnapshot(m_sceneDocuments[index].snapshot)) return false;
        m_activeSceneDocument = index; m_sceneDirty = m_sceneDocuments[index].dirty; m_selected = InvalidEntity; m_undo.clear(); m_redo.clear();
        return true;
    }

    bool EditorApp::OpenSceneDocument(const std::filesystem::path& path)
    {
        const auto normalized = std::filesystem::path(path).lexically_normal();
        for (std::size_t index = 0; index < m_sceneDocuments.size(); ++index) if (m_sceneDocuments[index].path.lexically_normal() == normalized) return index == m_activeSceneDocument || SwitchSceneDocument(index);
        auto& current = m_sceneDocuments[m_activeSceneDocument]; current.snapshot = m_engine.GetSceneManager().CreateSnapshot(); current.dirty = m_sceneDirty;
        if (!SceneSerializer::Load(*m_engine.GetContext().world, normalized)) { m_engine.GetSceneManager().RestoreSnapshot(current.snapshot); m_messages.emplace_back("Scene open failed: " + normalized.generic_string()); return false; }
        m_sceneDocuments.push_back({normalized, m_engine.GetSceneManager().CreateSnapshot(), false}); m_activeSceneDocument = m_sceneDocuments.size() - 1;
        m_sceneDirty = false; m_selected = InvalidEntity; m_undo.clear(); m_redo.clear(); m_messages.emplace_back("Opened scene tab: " + normalized.generic_string()); return true;
    }

    void EditorApp::NewSceneDocument()
    {
        if (m_engine.IsPlaying()) return;
        auto& current = m_sceneDocuments[m_activeSceneDocument]; current.snapshot = m_engine.GetSceneManager().CreateSnapshot(); current.dirty = m_sceneDirty;
        m_engine.GetSceneManager().NewScene(); m_sceneDocuments.push_back({{}, m_engine.GetSceneManager().CreateSnapshot(), true}); m_activeSceneDocument = m_sceneDocuments.size() - 1;
        m_sceneDirty = true; m_selected = InvalidEntity; m_undo.clear(); m_redo.clear();
    }

    void EditorApp::RecordUndo() { if (m_engine.IsPlaying()) return; m_undo.push_back(m_engine.GetSceneManager().CreateSnapshot()); if (m_undo.size() > 64) m_undo.erase(m_undo.begin()); m_redo.clear(); m_sceneDirty = true; }
    void EditorApp::Undo() { if (m_engine.IsPlaying() || m_undo.empty()) return; m_redo.push_back(m_engine.GetSceneManager().CreateSnapshot()); m_engine.GetSceneManager().RestoreSnapshot(m_undo.back()); m_undo.pop_back(); m_selected = InvalidEntity; m_sceneDirty = true; }
    void EditorApp::Redo() { if (m_engine.IsPlaying() || m_redo.empty()) return; m_undo.push_back(m_engine.GetSceneManager().CreateSnapshot()); m_engine.GetSceneManager().RestoreSnapshot(m_redo.back()); m_redo.pop_back(); m_selected = InvalidEntity; m_sceneDirty = true; }
    bool EditorApp::SaveCurrentScene()
    {
        auto path = m_activeSceneDocument < m_sceneDocuments.size() ? m_sceneDocuments[m_activeSceneDocument].path : std::filesystem::path{};
        if (path.empty()) path = m_engine.GetSceneManager().GetCurrentScene();
        if (path.empty()) path = m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().startupScene);
        const bool saved = m_engine.SaveScene(path);
        if (saved)
        {
            m_sceneDirty = false;
            if (m_activeSceneDocument < m_sceneDocuments.size()) { auto& document = m_sceneDocuments[m_activeSceneDocument]; document.path = path; document.snapshot = m_engine.GetSceneManager().CreateSnapshot(); document.dirty = false; }
            std::error_code error; std::filesystem::remove(GetAutosavePath(), error);
        }
        m_messages.emplace_back(saved ? "Scene saved: " + path.generic_string() : "Scene save failed"); return saved;
    }

    std::filesystem::path EditorApp::GetAutosavePath()
    {
        return m_engine.GetProjectSettings().Resolve("Saved/EditorAutosave.scene");
    }

    bool EditorApp::Autosave()
    {
        m_lastAutosaveTime = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
        const auto path = GetAutosavePath();
        const bool saved = SceneSerializer::Save(*m_engine.GetContext().world, path);
        if (saved) m_messages.emplace_back("Autosaved recovery scene: " + path.generic_string());
        else m_messages.emplace_back("Autosave failed: " + path.generic_string());
        return saved;
    }

    bool EditorApp::RestoreAutosave()
    {
        if (m_engine.IsPlaying() || !std::filesystem::is_regular_file(GetAutosavePath())) return false;
        RecordUndo();
        const bool loaded = SceneSerializer::Load(*m_engine.GetContext().world, GetAutosavePath());
        if (loaded) { m_selected = InvalidEntity; m_sceneDirty = true; }
        m_messages.emplace_back(loaded ? "Restored editor autosave" : "Failed to restore editor autosave");
        return loaded;
    }

    void EditorApp::UpdateWindowTitle()
    {
        if (!m_device || (m_titleInitialized && m_lastTitleDirty == m_sceneDirty)) return;
        const std::string title = "TinyEngine Editor - " + m_engine.GetProjectSettings().name + (m_sceneDirty ? " *" : "");
        SDL_SetWindowTitle(static_cast<SDL_Window*>(m_device->GetNativeWindow()), title.c_str());
        m_lastTitleDirty = m_sceneDirty;
        m_titleInitialized = true;
    }
}
