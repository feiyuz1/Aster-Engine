// 不启动窗口，直接用 ImGui 输入驱动真实 World，检查编辑器命令；不依赖 SDL、DPI 或输入法。
#include "EditorApp.hpp"
#include "MaterialAssetEditor.hpp"
#include "MaterialGraphEditor.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#include "MiniEngine/Project/ProjectPackager.hpp"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace MiniEngine
{
struct EditorInteractionTests
{
    World world;
    EditorApp app;
    int checks = 0;

    EditorInteractionTests()
    {
        app.m_engine.GetSceneManager().SetWorld(&world);
        // 测试 Engine 持有这个上下文；无窗口测试只接入实际会用到的服务。
        const_cast<EngineContext &>(app.m_engine.GetContext()).world = &world;
        app.m_sceneDocuments.push_back({{}, app.m_engine.GetSceneManager().CreateSnapshot(), false});
        app.m_interfaceLanguage = EditorApp::InterfaceLanguage::English;
        ImGui::CreateContext();
        auto &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 900};
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char *pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~EditorInteractionTests()
    {
        ImGui::DestroyContext();
    }

    void Check(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
        ++checks;
        std::cout << "PASS: " << message << '\n';
    }

    void Frame(const std::function<void()> &draw = {})
    {
        ImGui::NewFrame();
        if (draw)
            draw();
        if (!ImGui::IsAnyItemActive())
            app.m_inspectorEditId = 0;
        ImGui::Render();
    }

    void Key(ImGuiKey key, bool ctrl = false, bool shift = false, bool textInput = false, bool popup = false)
    {
        auto &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        io.AddKeyEvent(ImGuiMod_Shift, shift);
        io.AddKeyEvent(key, true);
        Frame([&]() {
            ImGui::Begin("TestHost");
            if (popup)
                ImGui::OpenPopup("BlockCommands");
            if (ImGui::BeginPopup("BlockCommands"))
            {
                app.HandleShortcuts();
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            else
            {
                io.WantTextInput = textInput;
                app.HandleShortcuts();
            }
            ImGui::End();
        });
        io.AddKeyEvent(key, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        Frame();
    }

    void Shortcuts()
    {
        const Entity entity = world.CreateEntity("Selected");
        app.m_selected = entity;
        app.m_selection = {entity};
        app.m_sceneCommandsFocused = false;
        Key(ImGuiKey_Delete);
        Check(world.IsAlive(entity) && app.m_undo.empty(), "Delete in another panel does not delete scene objects");
        app.m_sceneCommandsFocused = true;
        Key(ImGuiKey_Delete, false, false, true);
        Check(world.IsAlive(entity), "text editing owns Delete");
        Key(ImGuiKey_Delete, false, false, false, true);
        Check(world.IsAlive(entity), "popup at any stack level blocks scene Delete");
        app.m_sceneOrbitActive = true;
        Key(ImGuiKey_Delete);
        Check(world.IsAlive(entity), "camera gesture blocks scene commands");
        app.m_sceneOrbitActive = false;
        app.m_marqueeActive = true;
        Key(ImGuiKey_Escape);
        Check(!app.m_marqueeActive && app.m_selection.empty() && world.IsAlive(entity),
              "Escape ends selection gestures without deleting objects");
        app.m_selected = entity;
        app.m_selection = {entity};
        Key(ImGuiKey_Delete);
        Check(world.GetEntityManager().GetAliveEntities().empty() && app.m_undo.size() == 1, "focused Delete removes objects once");
        Key(ImGuiKey_Z, true);
        Check(world.GetEntityManager().GetAliveEntities().size() == 1 && app.m_selection.empty(),
              "Ctrl+Z restores scene and clears stale selection");
        Key(ImGuiKey_Z, true, true);
        Check(world.GetEntityManager().GetAliveEntities().empty(), "Ctrl+Shift+Z redoes instead of undoing");
        app.m_sceneCommandsFocused = false;
        Key(ImGuiKey_Z, true);
        Check(world.GetEntityManager().GetAliveEntities().empty(), "Ctrl+Z in another editor cannot undo scene changes");
        app.m_inspectorFocused = true;
        Key(ImGuiKey_Z, true);
        Check(world.GetEntityManager().GetAliveEntities().size() == 1, "Inspector retains scene Undo after field editing ends");
        app.m_inspectorFocused = false;
    }

    void Documents()
    {
        app.m_undo.clear();
        app.m_redo.clear();
        app.RecordUndo();
        world.CreateEntity("A edit");
        const auto sceneA = app.m_engine.GetSceneManager().CreateSnapshot();
        app.m_selection = {*world.GetEntityManager().GetAliveEntities().begin()};
        app.m_lastPickValid = true;
        app.m_activeGizmoAxis = 1;
        app.NewSceneDocument();
        Check(app.m_selection.empty() && !app.m_lastPickValid && app.m_activeGizmoAxis == -1,
              "new scene clears selection, picking cache and active drag");
        app.RecordUndo();
        world.CreateEntity("B edit");
        Check(app.SwitchSceneDocument(0) && app.m_undo.size() == 1, "switch back restores A Undo history");
        Check(app.m_engine.GetSceneManager().CreateSnapshot() == sceneA, "switch back restores A contents");
        app.Undo();
        const auto undoneA = app.m_engine.GetSceneManager().CreateSnapshot();
        Check(undoneA != sceneA, "A Undo restores its own earlier state");
        Check(app.SwitchSceneDocument(1) && app.m_undo.size() == 1 && app.m_redo.empty(), "B history stays independent");
        Check(app.SwitchSceneDocument(0) && app.m_redo.size() == 1, "A Redo survives switching tabs");
        app.Redo();
        Check(app.m_engine.GetSceneManager().CreateSnapshot() == sceneA, "A Redo restores exact contents");
        app.m_undo.push_back("TINYSCENE 11\ninvalid\n");
        const auto undoSize = app.m_undo.size();
        app.Undo();
        Check(app.m_undo.size() == undoSize && app.m_engine.GetSceneManager().CreateSnapshot() == sceneA,
              "malformed Undo snapshot preserves both scene and history");
        app.m_undo.pop_back();
        const auto selected = *world.GetEntityManager().GetAliveEntities().begin();
        app.m_selected = selected;
        app.m_selection = {selected};
        Check(app.CloseSceneDocument(1) && app.m_undo.size() == 1 && app.m_selected == selected,
              "closing a background tab preserves active selection and history");
        app.NewSceneDocument();
        app.m_sceneDirty = false;
        app.RequestEditorExit();
        Check(app.m_requestExitPopup && app.m_activeSceneDocument == 0, "exit finds unsaved background scenes");
        app.m_requestExitPopup = false;
        app.m_exitDiscardedDocuments.push_back(0);
        app.m_sceneDocuments[1].dirty = true;
        app.RequestEditorExit(true);
        Check(app.m_activeSceneDocument == 1 && app.m_sceneDocuments[0].dirty,
              "discard during exit keeps earlier dirty state until all prompts complete");
        app.m_requestExitPopup = false;
        app.m_exitInProgress = false;
        app.m_exitDiscardedDocuments.clear();
        app.RequestEditorExit();
        Check(app.m_activeSceneDocument == 0, "canceling exit allows earlier unsaved scenes to be prompted again");
        app.m_requestExitPopup = false;
        app.m_exitInProgress = false;
    }

    void Inspector()
    {
        world.Clear();
        const auto entity = world.CreateEntity("Inspector");
        // 保留一个区块，命中点按当前 ImGui 样式计算，不依赖固定屏幕坐标或 DPI。
        world.RemoveComponent<NameComponent>(entity);
        world.RemoveComponent<ActiveComponent>(entity);
        world.RemoveComponent<TagComponent>(entity);
        world.RemoveComponent<LayerComponent>(entity);
        app.m_selected = entity;
        app.m_selection = {entity};
        app.m_undo.clear();
        app.m_redo.clear();
        app.m_sceneDirty = false;
        const auto draw = [&]() { app.DrawInspector(); };
        Frame(draw);
        Frame(draw);
        const auto *window = ImGui::FindWindowByName("Inspector");
        const auto &style = ImGui::GetStyle();
        const float x = window->DC.CursorStartPos.x + 15.0f;
        const float y = window->DC.CursorStartPos.y + ImGui::GetFontSize() + style.SeparatorTextPadding.y * 2.0f + style.ItemSpacing.y +
                        ImGui::GetFrameHeight() * 0.5f;
        auto &io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        Frame(draw);
        io.AddMouseButtonEvent(0, true);
        Frame(draw);
        Check(ImGui::IsAnyItemActive(), "test pointer activates the real Inspector position field");
        Check(app.m_undo.empty() && !app.m_sceneDirty, "clicking a numeric field without editing leaves scene clean");
        io.AddMousePosEvent(x + 35.0f, y);
        Frame(draw);
        io.AddMousePosEvent(x + 65.0f, y);
        Frame(draw);
        io.AddMouseButtonEvent(0, false);
        Frame(draw);
        Check(world.GetComponent<TransformComponent>(entity)->position.x != 0.0f && app.m_undo.size() == 1 && app.m_sceneDirty,
              "multi-frame property drag changes the value with exactly one Undo entry");
        const float editedPosition = world.GetComponent<TransformComponent>(entity)->position.x;
        app.Undo();
        const auto restored = *world.GetEntityManager().GetAliveEntities().begin();
        Check(world.GetComponent<TransformComponent>(restored)->position.x == 0.0f, "property Undo restores pre-drag value");
        app.Redo();
        Check(world.GetComponent<TransformComponent>(*world.GetEntityManager().GetAliveEntities().begin())->position.x == editedPosition,
              "property Redo restores final drag value");

        const auto renamed = *world.GetEntityManager().GetAliveEntities().begin();
        world.RemoveComponent<TransformComponent>(renamed);
        world.RemoveComponent<ActiveComponent>(renamed);
        world.RemoveComponent<TagComponent>(renamed);
        world.RemoveComponent<LayerComponent>(renamed);
        world.GetComponent<NameComponent>(renamed)->name = "Before";
        app.m_selected = renamed;
        app.m_selection = {renamed};
        app.m_undo.clear();
        app.m_sceneDirty = false;
        Frame(draw);
        io.AddMousePosEvent(x, y);
        Frame(draw);
        io.AddMouseButtonEvent(0, true);
        Frame(draw);
        io.AddMouseButtonEvent(0, false);
        Frame(draw);
        io.AddInputCharactersUTF8("X");
        Frame(draw);
        io.AddInputCharactersUTF8("Y");
        Frame(draw);
        Check(world.GetComponent<NameComponent>(renamed)->name != "Before" && app.m_undo.size() == 1,
              "text edits persist without Enter and coalesce into one Undo entry");
        io.AddMousePosEvent(300, 850);
        io.AddMouseButtonEvent(0, true);
        Frame(draw);
        io.AddMouseButtonEvent(0, false);
        Frame(draw);
        app.Undo();
        Check(world.GetComponent<NameComponent>(*world.GetEntityManager().GetAliveEntities().begin())->name == "Before",
              "text field Undo restores the complete original name");
    }

    void Gizmo()
    {
        Renderer renderer;
        const_cast<EngineContext &>(app.m_engine.GetContext()).renderer = &renderer;
        world.Clear();
        const auto entity = world.CreateEntity("Gizmo");
        app.ResetSceneInteraction();
        app.m_selected = entity;
        app.m_selection = {entity};
        app.m_undo.clear();
        app.m_sceneDirty = false;
        app.m_gizmoSnap = true;
        app.m_translateSnap = 1.0f;
        const auto draw = [&]() {
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({600, 600});
            ImGui::Begin("GizmoTest");
            app.DrawGizmo(50, 50, 400, 400, true);
            ImGui::End();
        };
        auto &io = ImGui::GetIO();
        io.AddMousePosEvent(50, 50);
        Frame(draw);
        io.AddMousePosEvent(300, 250);
        io.AddMouseButtonEvent(0, true);
        Frame(draw);
        Check(app.m_activeGizmoAxis == 0 && app.m_undo.empty() && !app.m_sceneDirty &&
                  world.GetComponent<TransformComponent>(entity)->position.x == 0,
              "clicking Gizmo ignores pointer arrival delta and leaves scene clean");
        io.AddMousePosEvent(302, 250);
        Frame(draw);
        Check(app.m_undo.empty() && !app.m_sceneDirty, "sub-threshold snapped Gizmo drag creates no undo record");
        io.AddMousePosEvent(390, 250);
        Frame(draw);
        io.AddMousePosEvent(490, 250);
        Frame(draw);
        io.AddMouseButtonEvent(0, false);
        Frame(draw);
        Check(app.m_undo.size() == 1 && world.GetComponent<TransformComponent>(entity)->position.x != 0,
              "snapped Gizmo drag commits one Undo for actual movement");
        app.Undo();
        Check(world.GetComponent<TransformComponent>(*world.GetEntityManager().GetAliveEntities().begin())->position.x == 0,
              "Gizmo Undo restores the position before the gesture");
        const_cast<EngineContext &>(app.m_engine.GetContext()).renderer = nullptr;
    }

    void HierarchyCameraFocus()
    {
        world.Clear();
        app.ResetSceneInteraction();
        app.m_selected = InvalidEntity;
        app.m_selection.clear();
        app.m_hierarchySearch[0] = '\0';
        app.m_editorCameraPosition = {0.0f, 2.5f, 7.0f};
        app.m_editorCameraRotation = {0.0f, 0.0f, 0.0f};
        const Entity target = world.CreateEntity("Focus target");
        world.GetComponent<TransformComponent>(target)->position = {14.0f, 2.0f, -6.0f};
        ColliderComponent collider;
        collider.shape = ColliderShape::Box;
        collider.halfExtents = {2.0f, 1.0f, 3.0f};
        world.AddComponent(target, collider);
        ImVec2 rowMinimum{}, rowMaximum{};
        const auto draw = [&]() {
            app.DrawHierarchy();
            rowMinimum = ImGui::GetItemRectMin();
            rowMaximum = ImGui::GetItemRectMax();
        };
        auto &io = ImGui::GetIO();
        Frame(draw);
        Check(rowMaximum.y > rowMinimum.y, "hierarchy exposes a clickable entity row");
        io.AddMousePosEvent(rowMinimum.x + 38.0f, rowMaximum.y + 5.5f);
        Frame(draw);
        io.AddMouseButtonEvent(0, true);
        Frame(draw);
        Check(app.m_selected == target && app.m_editorCameraPosition.x == 0.0f,
              "single click selects without moving the editor camera");
        io.AddMouseButtonEvent(0, false);
        Frame(draw);
        io.AddMouseButtonEvent(0, true);
        Frame(draw);
        io.AddMouseButtonEvent(0, false);
        Frame(draw);
        Check(app.m_selected == target && std::abs(app.m_editorCameraPosition.x - 14.0f) < 0.01f &&
                  std::abs(app.m_editorCameraPosition.y - 2.0f) < 0.01f && app.m_editorCameraPosition.z > -1.0f &&
                  app.m_editorCameraRotation.x == 0.0f && app.m_editorCameraRotation.y == 0.0f,
              "double click frames the entity while preserving camera direction");

        const Entity parent = world.CreateEntity("Parent");
        world.GetComponent<TransformComponent>(parent)->position = {100.0f, 4.0f, 0.0f};
        const Entity child = world.CreateEntity("Child");
        world.GetComponent<TransformComponent>(child)->position = {3.0f, 0.0f, 0.0f};
        Check(world.SetParent(child, parent), "focus fixture parents a child entity");
        app.FocusSceneEntity(child);
        Check(std::abs(app.m_editorCameraPosition.x - 103.0f) < 0.01f &&
                  std::abs(app.m_editorCameraPosition.y - 4.0f) < 0.01f,
              "focus uses world position for a child without geometry");
        app.m_editorCameraPosition = {0.0f, 0.0f, 0.0f};
        app.FocusSceneEntity(InvalidEntity);
        Check(Length(app.m_editorCameraPosition) == 0.0f, "focus ignores an invalid entity");
        world.Clear();
        app.ResetSceneInteraction();
    }

    void ViewportRegression()
    {
        Renderer renderer;
        const_cast<EngineContext &>(app.m_engine.GetContext()).renderer = &renderer;
        auto &view = const_cast<FrameView &>(renderer.GetFrameView());
        view.cameraPosition = {4, 3, 6};
        view.view = LookAt(view.cameraPosition, {}, {0, 1, 0});
        view.projection = Perspective(60, 1, 0.1f, 100);
        app.m_editorCameraPosition = view.cameraPosition;
        app.m_gizmoSnap = false;
        app.m_gizmoSpace = EditorApp::GizmoSpace::World;
        app.m_pivotMode = EditorApp::PivotMode::Individual;
        auto project = [&](Vec3 point) {
            const auto m = view.projection * view.view;
            const float w = m.At(3, 0) * point.x + m.At(3, 1) * point.y + m.At(3, 2) * point.z + m.At(3, 3);
            return ImVec2{50 + 200 * (1 + (m.At(0, 0) * point.x + m.At(0, 1) * point.y + m.At(0, 2) * point.z + m.At(0, 3)) / w),
                          50 + 200 * (1 - (m.At(1, 0) * point.x + m.At(1, 1) * point.y + m.At(1, 2) * point.z + m.At(1, 3)) / w)};
        };
        auto draw = [&]() {
            ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({600, 600});
            ImGui::Begin("GizmoGeometry"); app.DrawGizmo(50, 50, 400, 400, true); ImGui::End();
        };
        auto &io = ImGui::GetIO();
        const float length = Length(view.cameraPosition) * 0.13f;
        auto drag = [&](ImVec2 from, ImVec2 to, int axis) {
            io.AddMousePosEvent(from.x, from.y); Frame(draw);
            io.AddMouseButtonEvent(0, true); Frame(draw);
            Check(app.m_activeGizmoAxis == axis, "projected handle selects the intended axis");
            io.AddMousePosEvent(to.x, to.y); Frame(draw);
            io.AddMouseButtonEvent(0, false); Frame(draw);
        };
        for (int axis = 0; axis < 3; ++axis)
        {
            world.Clear(); const auto entity = world.CreateEntity("Axis");
            app.ResetSceneInteraction(); app.m_selected = entity; app.m_selection = {entity}; app.m_undo.clear();
            app.m_gizmoMode = EditorApp::GizmoMode::Translate;
            const Vec3 direction = axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
            const auto from = project(direction * (length * 0.8f)), end = project(direction * length), origin = project({});
            drag(from, {from.x + (end.x - origin.x) * 0.3f, from.y + (end.y - origin.y) * 0.3f}, axis);
            Check(Dot(world.GetComponent<TransformComponent>(entity)->position, direction) > 0 && app.m_undo.size() == 1,
                  "dragging along each visible arrow moves positively on that axis with one Undo");
        }
        world.Clear(); const auto scaled = world.CreateEntity("Scale");
        app.ResetSceneInteraction(); app.m_selected = scaled; app.m_selection = {scaled};
        app.m_gizmoMode = EditorApp::GizmoMode::Scale;
        const auto blue = project({0, 0, length}), origin = project({});
        drag(blue, {blue.x + (blue.x - origin.x) * 0.25f, blue.y + (blue.y - origin.y) * 0.25f}, 2);
        Check(std::abs(world.GetComponent<TransformComponent>(scaled)->scale.z - 1.25f) < 0.02f,
              "blue scale handle grows in its displayed direction by the drag ratio");

        for (int axis = 0; axis < 3; ++axis)
        {
            world.Clear(); const auto rotated = world.CreateEntity("Rotate");
            app.ResetSceneInteraction(); app.m_selected = rotated; app.m_selection = {rotated};
            app.m_gizmoMode = EditorApp::GizmoMode::Rotate;
            const Vec3 u = axis == 0 ? Vec3{0, 1, 0} : axis == 1 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
            const Vec3 v = axis == 0 ? Vec3{0, 0, 1} : axis == 1 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            const float firstAngle = DegreesToRadians(30), lastAngle = DegreesToRadians(65);
            drag(project((u * std::cos(firstAngle) + v * std::sin(firstAngle)) * length),
                 project((u * std::cos(lastAngle) + v * std::sin(lastAngle)) * length), axis);
            const auto rotation = world.GetComponent<TransformComponent>(rotated)->rotation;
            const float angle = axis == 0 ? rotation.x : axis == 1 ? rotation.y : rotation.z;
            // ImGui 会把注入的指针位置取整到像素，角度允许相应的微小误差。
            Check(std::abs(angle - 35) < 2.0f, "each rotation ring follows a 35 degree projected gesture");
        }

        world.Clear(); const auto parent = world.CreateEntity("Parent"), child = world.CreateEntity("Child");
        world.SetParent(child, parent);
        world.GetComponent<TransformComponent>(parent)->rotation.y = 90;
        world.GetComponent<TransformComponent>(parent)->scale = {2, 2, 2};
        app.ResetSceneInteraction(); app.m_selected = child; app.m_selection = {child};
        app.m_gizmoMode = EditorApp::GizmoMode::Translate;
        auto red = project({length * 0.8f, 0, 0}), redEnd = project({length, 0, 0});
        drag(red, {red.x + (redEnd.x - origin.x) * 0.3f, red.y + (redEnd.y - origin.y) * 0.3f}, 0);
        const auto childWorld = world.GetWorldMatrix(child);
        Check(childWorld.At(0, 3) > 0 && std::abs(childWorld.At(2, 3)) < 0.001f,
              "world translation under a rotated scaled parent stays on the displayed world axis");
        world.GetComponent<TransformComponent>(child)->position = {};
        app.m_selected = parent; app.m_selection = {parent, child};
        drag(red, {red.x + (redEnd.x - origin.x) * 0.3f, red.y + (redEnd.y - origin.y) * 0.3f}, 0);
        Check(Length(world.GetComponent<TransformComponent>(child)->position) == 0 &&
                  std::abs(world.GetWorldMatrix(child).At(0, 3) - world.GetWorldMatrix(parent).At(0, 3)) < 0.001f,
              "selected parent and child receive world translation only once");
        const_cast<EngineContext &>(app.m_engine.GetContext()).renderer = nullptr;

        app.ResetSceneInteraction(); app.m_editorCameraPosition = {0, 0, 5}; app.m_editorCameraRotation = {};
        const auto camera = [&]() { app.UpdateSceneCamera(true, true); };
        io.AddKeyEvent(ImGuiKey_W, true); Frame(camera);
        io.AddKeyEvent(ImGuiKey_W, false); Frame(camera);
        Check(app.m_editorCameraPosition.z < 5, "focused WASD moves the scene camera without holding RMB");
        const Vec3 before = app.m_editorCameraPosition;
        io.AddKeyEvent(ImGuiKey_A, true); Frame([&]() { app.UpdateSceneCamera(true, false); });
        io.AddKeyEvent(ImGuiKey_A, false); Frame(camera);
        Check(Length(app.m_editorCameraPosition - before) == 0, "hover alone cannot steal WASD from another panel");
        io.AddKeyEvent(ImGuiKey_W, true);
        Frame([&]() { io.WantTextInput = true; app.UpdateSceneCamera(true, true); });
        io.AddKeyEvent(ImGuiKey_W, false); Frame(camera);
        Check(Length(app.m_editorCameraPosition - before) == 0, "text input prevents camera movement");
        const auto rotationBefore = app.m_editorCameraRotation;
        io.AddMousePosEvent(100, 100); Frame(camera);
        io.AddMousePosEvent(200, 200); io.AddMouseButtonEvent(1, true); Frame(camera);
        Check(Length(app.m_editorCameraRotation - rotationBefore) == 0, "starting RMB look ignores pointer arrival delta");
        io.AddMouseButtonEvent(1, false); Frame(camera);

        Check(renderer.Initialize(std::make_unique<NullRenderDevice>()), "stable projection renderer initializes");
        renderer.SetCameraOverride(true, {0, 0, 5}, {});
        renderer.RenderFrame(world); const auto projection = renderer.GetFrameView().projection;
        renderer.RenderFrame(world);
        Check(projection.data == renderer.GetFrameView().projection.data && renderer.GetFrameView().temporalJitter.x == 0 &&
                  renderer.GetFrameView().temporalJitter.y == 0, "stationary editor projection is identical across frames");
        renderer.SetCameraOverride(false); renderer.RenderFrame(world);
        Check(renderer.GetFrameView().temporalJitter.x == 0 && renderer.GetFrameView().temporalJitter.y == 0,
              "render device without temporal AA receives no projection jitter");
        renderer.Shutdown();
    }

    void SceneOpenRegression()
    {
        const auto root = std::filesystem::absolute("Saved/SceneOpenRegression") /
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root / "assets");
        const auto oldSettings = app.m_engine.GetProjectSettings();
        auto &settings = app.m_engine.GetProjectSettings();
        settings.projectFile = root / "Test.tinyproject";
        settings.assetDirectory = "assets";
        ResourceManager resources;
        resources.Initialize(nullptr, root / "assets");
        const_cast<EngineContext &>(app.m_engine.GetContext()).resourceManager = &resources;
        app.NewSceneDocument();
        world.CreateEntity("Saved scene object");
        const auto saved = root / "assets/Level.scene";
        Check(app.SaveSceneDocumentTo(app.m_activeSceneDocument, saved), "scene is saved through the editor save transaction");
        const auto snapshot = app.m_engine.GetSceneManager().CreateSnapshot();
        app.NewSceneDocument();
        world.CreateEntity("Keep unsaved work");
        app.m_sceneDirty = true;
        const auto work = app.m_engine.GetSceneManager().CreateSnapshot();
        // 先移除已保存的标签，确保打开操作确实从磁盘读取，而非只切换标签。
        for (std::size_t index = 0; index < app.m_sceneDocuments.size(); ++index)
            if (app.m_sceneDocuments[index].path == saved) { app.CloseSceneDocument(index); break; }
        const auto unsavedIndex = app.m_activeSceneDocument;
        app.m_shaderFocused = false;
        Key(ImGuiKey_O, true);
        Check(app.m_requestSceneOpenPopup && std::find(app.m_sceneOpenFiles.begin(), app.m_sceneOpenFiles.end(), saved) != app.m_sceneOpenFiles.end(),
              "Ctrl+O opens a refreshed list containing the newly saved scene");
        const auto draw = [&]() { app.DrawSceneOpenDialog(); };
        Frame(draw); Frame(draw);
        auto *dialog = ImGui::FindWindowByName("Open Scene###SceneOpenDialog");
        Check(dialog && dialog->Active && !dialog->Hidden, "Open Scene modal is visible");
        const auto path = saved.generic_u8string();
        std::copy(path.begin(), path.end(), app.m_sceneOpenPath.begin()); app.m_sceneOpenPath[path.size()] = '\0';
        ImGui::ActivateItemByID(dialog->GetID("Open")); Frame(draw);
        Check(app.m_engine.GetSceneManager().CreateSnapshot() == snapshot,
              "Open button loads the saved scene contents from disk");
        Check(app.SwitchSceneDocument(unsavedIndex) && app.m_sceneDirty && app.m_engine.GetSceneManager().CreateSnapshot() == work,
              "opening a scene preserves the previous tab's unsaved changes");
        for (float width : {800.0f, 1280.0f, 1920.0f})
        {
            const auto layout = app.GetWorkspaceLayout(width, 720);
            Check(width - layout.left - layout.right - 6 > 300 && layout.upper > 300 && 720 - layout.upper > 180,
                  "workspace leaves usable viewport and asset space across window widths");
        }
        auto &io = ImGui::GetIO();
        const auto splitters = [&]() { app.DrawWorkspaceSplitters(); };
        Frame(splitters); Frame(splitters);
        const auto *grip = ImGui::FindWindowByName("##LeftSplit");
        const float originalRatio = app.m_leftPanelRatio;
        io.AddMousePosEvent(grip->Pos.x + 3, grip->Pos.y + 120); Frame(splitters);
        io.AddMouseButtonEvent(0, true); Frame(splitters);
        io.AddMousePosEvent(grip->Pos.x + 63, grip->Pos.y + 120); Frame(splitters);
        io.AddMouseButtonEvent(0, false); Frame(splitters);
        Check(app.m_leftPanelRatio > originalRatio, "dragging the real splitter resizes the hierarchy panel");
        const float storedRatio = app.m_leftPanelRatio;
        app.m_leftPanelRatio = originalRatio; app.LoadEditorSettings();
        Check(std::abs(app.m_leftPanelRatio - storedRatio) < 0.0001f, "workspace ratios survive saving and reloading settings");
        app.m_leftPanelRatio = originalRatio;
        Key(ImGuiKey_F11);
        Check(app.m_sceneMaximized, "F11 expands the scene viewport");
        Key(ImGuiKey_F11);
        Check(!app.m_sceneMaximized, "F11 restores the complete workspace");
        const auto selected = world.CreateEntity("Keep selected object on failed open");
        app.m_selected = selected;
        app.m_selection = {selected};
        const auto beforeFailedOpen = app.m_engine.GetSceneManager().CreateSnapshot();
        Check(!app.OpenSceneDocument(root / "missing.scene") && world.IsAlive(selected) &&
                  app.m_selected == selected && app.m_engine.GetSceneManager().CreateSnapshot() == beforeFailedOpen,
              "failed scene open preserves the live world and selected entity handles");
        Check(PlatformFile::WriteAllText(app.GetAutosavePath(), "TINYSCENE 11\nentity 1 0 \"broken\"\ntransform invalid\n"),
              "malformed recovery fixture is written");
        Check(!app.RestoreAutosave() && world.IsAlive(selected) && app.m_selected == selected &&
                  app.m_engine.GetSceneManager().CreateSnapshot() == beforeFailedOpen,
              "failed recovery preserves the live world and selected entity handles");
        app.m_sceneDirty = true;
        Check(app.Autosave(), "dirty scene can create a recovery file");
        const auto recovery = PlatformFile::ReadAllText(app.GetAutosavePath());
        app.NewSceneDocument();
        Check(app.SaveSceneDocumentTo(app.m_activeSceneDocument, root / "assets/Other.scene") &&
                  PlatformFile::ReadAllText(app.GetAutosavePath()) == recovery,
              "saving another tab preserves recovery while unsaved scenes remain open");
        settings = oldSettings;
        const_cast<EngineContext &>(app.m_engine.GetContext()).resourceManager = nullptr;
    }

    void CompactWorkspace()
    {
        auto &io = ImGui::GetIO();
        const auto oldSize = io.DisplaySize;
        io.DisplaySize = {800, 600};
        const auto layout = app.GetWorkspaceLayout(800, 600);
        Check(layout.left >= 160 && layout.right >= 240 && 800 - layout.left - layout.right - 6 >= 300,
              "compact workspace reserves readable sidebars and a usable scene viewport");
        world.Clear();
        const auto entity = world.CreateEntity("Compact inspector");
        world.AddComponent<CameraComponent>(entity, {});
        app.m_selected = entity;
        app.m_selection = {entity};
        Frame([&]() { app.DrawInspector(); });
        Frame([&]() { app.DrawInspector(); });
        const auto *inspector = ImGui::FindWindowByName("Inspector");
        Check(inspector->ContentSize.x <= inspector->InnerRect.GetWidth() + 1,
              "compact inspector keeps property labels and controls inside the panel");

        app.m_messages.clear();
        for (int index = 0; index < 100; ++index)
            app.m_messages.push_back("Log entry " + std::to_string(index));
        Frame([&]() { app.DrawConsole(); });
        Frame([&]() { app.DrawConsole(); });
        auto *console = ImGui::FindWindowByName("Console / Stats");
        Check(console->ScrollMax.y == 0,
              "long console output cannot scroll the command input out of view");
        ImGuiWindow *log = nullptr;
        for (auto *candidate : ImGui::GetCurrentContext()->Windows)
            if (candidate->ParentWindow == console && candidate->ScrollMax.y > 0)
                log = candidate;
        Check(log != nullptr, "console output has its own scrollable history");
        ImGui::SetScrollY(log, 0);
        Frame([&]() { app.DrawConsole(); });
        app.m_messages.emplace_back("New message while reading old output");
        Frame([&]() { app.DrawConsole(); });
        Frame([&]() { app.DrawConsole(); });
        Check(log->Scroll.y == 0, "new output preserves the position when reading earlier console messages");
        ImGui::SetScrollY(log, log->ScrollMax.y);
        Frame([&]() { app.DrawConsole(); });
        app.m_messages.emplace_back("New message while following output");
        Frame([&]() { app.DrawConsole(); });
        Frame([&]() { app.DrawConsole(); });
        Check(log->Scroll.y >= log->ScrollMax.y - 1,
              "console follows new messages only when already at the bottom");
        app.m_messages.clear();
        io.DisplaySize = oldSize;
        app.ResetSceneInteraction();
    }

    void ShaderDocuments()
    {
        const auto root = std::filesystem::absolute("Saved/EditorShaderRegression") /
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        const auto first = (root / "first.frag").generic_u8string();
        const auto second = (root / "second.vert").generic_u8string();
        const std::string original = "// original shader\nvoid main() {}\n";
        const std::string other = "// second shader\nvoid main() {}\n";
        Check(PlatformFile::WriteAllText(first, original) && PlatformFile::WriteAllText(second, other), "shader fixtures created");
        app.m_exitInProgress = false;
        app.m_requestExitPopup = false;
        app.RequestShaderAction(EditorApp::ShaderAction::Open, first);
        Check(app.m_openShaderAsset == first && !app.IsShaderDirty(), "opening Shader reads a clean independent document");
        const auto draw = [&]() { app.DrawShaderEditor(); };
        auto &io = ImGui::GetIO();
        io.AddMousePosEvent(1100, 800);
        Frame(draw);
        Frame(draw);
        const auto *window = ImGui::FindWindowByName("Shader Source Editor");
        const auto start = window->DC.CursorStartPos;
        io.AddMousePosEvent(start.x + 30, start.y + ImGui::GetTextLineHeightWithSpacing() + 12);
        Frame(draw);
        io.AddMouseButtonEvent(0, true);
        Frame(draw);
        io.AddMouseButtonEvent(0, false);
        Frame(draw);
        Check(ImGui::IsAnyItemActive(), "real Shader multiline field receives input focus");
        io.AddInputCharactersUTF8("edited");
        Frame(draw);
        const std::string edited = app.m_shaderSource.data();
        Check(app.IsShaderDirty() && PlatformFile::ReadAllText(first) == original,
              "typing Shader source changes memory without touching its file");
        app.m_selectedAsset = "some-other-material.mat";
        Frame(draw);
        Check(!app.m_shaderEditorHidden && app.m_openShaderAsset == first && std::string(app.m_shaderSource.data()) == edited,
              "selecting a different asset preserves the open Shader and its text");
        app.RequestShaderAction(EditorApp::ShaderAction::Close);
        Check(app.m_shaderAction == EditorApp::ShaderAction::Close && !app.m_shaderEditorHidden,
              "closing dirty Shader waits for a decision");
        const auto drawDialog = [&]() { app.DrawShaderEditor(); app.DrawShaderCloseDialog(); };
        Frame(drawDialog);
        Frame(drawDialog);
        auto *dialog = ImGui::FindWindowByName("Unsaved shader###ShaderCloseDialog");
        Check(dialog && dialog->Active && !dialog->Hidden, "dirty Shader displays its actual confirmation modal");
        ImGui::ActivateItemByID(dialog->GetID("Cancel"));
        Frame(drawDialog);
        Check(app.m_shaderAction == EditorApp::ShaderAction::None && app.IsShaderDirty() && !app.m_shaderEditorHidden &&
                  PlatformFile::ReadAllText(first) == original,
              "confirmation Cancel button keeps edits and leaves disk unchanged");
        app.RequestShaderAction(EditorApp::ShaderAction::Open, second);
        Check(app.m_openShaderAsset == first && app.m_pendingShaderPath == second,
              "switching Shader cannot replace a dirty buffer before confirmation");
        app.ResolveShaderAction(EditorApp::ShaderDecision::Cancel);
        // 文本框占用键盘焦点时，Ctrl+S 仍应保存文档。
        io.AddMousePosEvent(start.x + 30, start.y + ImGui::GetTextLineHeightWithSpacing() + 12);
        Frame(draw);
        io.AddMouseButtonEvent(0, true);
        Frame(draw);
        io.AddMouseButtonEvent(0, false);
        Frame(draw);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_S, true);
        Frame([&]() {
            app.DrawShaderEditor();
            app.HandleShortcuts();
        });
        io.AddKeyEvent(ImGuiKey_S, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        Frame(draw);
        Check(!app.IsShaderDirty() && PlatformFile::ReadAllText(first) == edited &&
                  PlatformFile::ReadAllText(first + ".bak") == original &&
                  app.m_sceneSaveDocument == EditorApp::InvalidSceneDocument,
              "Shader Ctrl+S atomically saves with backup and does not open scene Save As");
        io.AddInputCharactersUTF8("discard-me");
        Frame(draw);
        Check(app.IsShaderDirty(), "further text editing marks Shader dirty again");
        app.RequestShaderAction(EditorApp::ShaderAction::Open, second);
        Check(app.ResolveShaderAction(EditorApp::ShaderDecision::Discard) && app.m_openShaderAsset == second &&
                  !app.IsShaderDirty() && PlatformFile::ReadAllText(first) == edited,
              "Discard switches Shader without writing discarded text");
        const std::string changed = other + "// pending\n";
        app.m_shaderSource.assign(changed.begin(), changed.end());
        app.m_shaderSource.push_back('\0');
        app.RequestShaderAction(EditorApp::ShaderAction::Close);
        // 使用非空的 .bak 目录阻止原子写入，无需依赖平台权限设置。
        std::filesystem::create_directories(second + ".bak");
        PlatformFile::WriteAllText(std::filesystem::path(second + ".bak") / "blocker", "keep");
        Check(!app.ResolveShaderAction(EditorApp::ShaderDecision::Save) && app.IsShaderDirty() && !app.m_shaderEditorHidden &&
                  app.m_shaderAction == EditorApp::ShaderAction::Close && PlatformFile::ReadAllText(second) == other &&
                  !std::filesystem::exists(second + ".tmp"),
              "failed atomic save preserves original file, dirty buffer and pending close");
        app.ResolveShaderAction(EditorApp::ShaderDecision::Cancel);
        PlatformFile::WriteAllText(second, "// external edit\n");
        Check(!app.SaveShaderDocument() && app.IsShaderDirty() && PlatformFile::ReadAllText(second) == "// external edit\n",
              "external edits are not overwritten by a stale Shader buffer");
        for (auto &document : app.m_sceneDocuments)
            document.dirty = false;
        app.m_sceneDirty = false;
        app.m_selectedAsset.clear();
        app.RequestEditorExit();
        Check(app.m_shaderAction == EditorApp::ShaderAction::Exit && app.m_exitInProgress,
              "application exit checks Shader changes even with no dirty scenes");
        app.ResolveShaderAction(EditorApp::ShaderDecision::Cancel);
        Check(!app.m_exitInProgress && app.IsShaderDirty(), "canceling Shader exit preserves the document and cancels exit");
        app.RequestShaderAction(EditorApp::ShaderAction::Close);
        Check(app.ResolveShaderAction(EditorApp::ShaderDecision::Discard) && app.m_shaderEditorHidden && !app.IsShaderDirty(),
              "discarding close hides Shader without saving");
        app.RequestShaderAction(EditorApp::ShaderAction::Open, second);
        Check(std::string(app.m_shaderSource.data()) == "// external edit\n", "reopening a closed Shader reloads current disk content");
        Check(!app.OpenShaderDocument((root / "missing.frag").generic_string()) && app.m_openShaderAsset == second,
              "failed Shader open preserves the current document");
        app.RequestShaderAction(EditorApp::ShaderAction::Open, first);
        const std::string savedOnClose = edited + "// explicit save on close\n";
        app.m_shaderSource.assign(savedOnClose.begin(), savedOnClose.end());
        app.m_shaderSource.push_back('\0');
        app.RequestShaderAction(EditorApp::ShaderAction::Close);
        Frame(drawDialog);
        Frame(drawDialog);
        dialog = ImGui::FindWindowByName("Unsaved shader###ShaderCloseDialog");
        ImGui::ActivateItemByID(dialog->GetID("Save"));
        Frame(drawDialog);
        if (app.m_shaderAction != EditorApp::ShaderAction::None || !app.m_shaderEditorHidden ||
            PlatformFile::ReadAllText(first) != savedOnClose)
            std::cerr << "Shader save diagnostic: action=" << static_cast<int>(app.m_shaderAction)
                      << " hidden=" << app.m_shaderEditorHidden << " error=" << app.m_shaderError
                      << " dirty=" << app.IsShaderDirty() << " modal=" << dialog->Active
                      << " disk=" << PlatformFile::ReadAllText(first) << " expected=" << savedOnClose << '\n';
        Check(app.m_shaderAction == EditorApp::ShaderAction::None && app.m_shaderEditorHidden &&
                  PlatformFile::ReadAllText(first) == savedOnClose,
              "confirmation Save button persists edits before hiding the window");
    }

    void WindowClose()
    {
        Check(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL event queue initializes without a graphics window");
        SdlGlRenderDevice device(1, 1, "Headless close request");
        int closeRequests = 0;
        device.SetCloseRequestCallback([&]() { ++closeRequests; });
        SDL_Event event{};
        event.type = SDL_QUIT;
        Check(SDL_PushEvent(&event) == 1 && device.PumpEvents() && closeRequests == 1,
              "system window close is routed to confirmation without ending the event loop");
        device.SetCloseRequestCallback({});
        Check(SDL_PushEvent(&event) == 1 && !device.PumpEvents(), "runtime without a close handler still exits on SDL_QUIT");
    }
    void MaterialDocuments()
    {
        MaterialAssetData data; data.formatVersion=2; data.definitionPath="builtin:PBR";
        MaterialAssetEditor asset; const auto definition=MakeBuiltinPbrDefinition();
        Check(asset.Open("Saved/EditorMaterials/instance.mat",data,definition.parameters),"open typed material draft");
        Check(asset.SetOverride("BaseColor",Vec4{1,0,0,1}) && asset.IsDirty(),"typed override marks real draft dirty");
        Check(!asset.SetOverride("BaseColor",1.f) && asset.GetDraft().overrides.size()==1,"wrong override type rejected");
        Check(asset.ResetOverride("BaseColor") && asset.GetDraft().overrides.empty(),"reset removes override for inheritance");
        asset.SetOverride("Metallic",.8f);
        asset.SetSaveRequest([](const auto &,const auto &,auto &diagnostics) { diagnostics.push_back({MaterialDiagnosticSeverity::Error,{},0,-1,"test validator rejection"}); return false; });
        Check(!asset.Save() && asset.IsDirty() && !asset.GetDiagnostics().empty(),"failed candidate preserves dirty draft and diagnostics");
        asset.SetSaveRequest([](const auto &path,const auto &data,auto &) { return PlatformFile::WriteAllText(std::filesystem::u8path(path),SerializeMaterialAsset(data)); });
        Check(asset.Save() && !asset.IsDirty(),"successful save clears dirty state");
        MaterialResource saved; Check(saved.Load("Saved/EditorMaterials/instance.mat") && std::abs(std::get<float>(saved.GetMaterialData().overrides.at("Metallic"))-.8f)<.00001f,"saved typed material reopens without loss");
        std::string error; Check(MaterialAssetEditor::CreateInstance("Saved/EditorMaterials/child.mat","Saved/EditorMaterials/instance.mat",error),"create child instance asset");
        Check(saved.Load("Saved/EditorMaterials/child.mat") && saved.GetMaterialData().parentPath=="instance.mat","child has v2 relative parent reference");
        MaterialDefinitionData graph; graph.graph.SetFormatVersion(2); graph.sourcePath="Saved/EditorMaterials/parent.matgraph";
        graph.graph.AddNode(MaterialNodeType::SurfaceOutput,"Surface");
        MaterialGraphEditor editor; Check(editor.Open(graph.sourcePath,graph),"open graph draft");
        auto color=editor.AddNode(MaterialNodeType::Constant,"Color");
        Check(!editor.ConnectPorts(color,0,1,0),"Vec4 to Vec3 connection requires explicit mask");
        auto mask=editor.AddNode(MaterialNodeType::ComponentMask,"RGB");
        Check(editor.ConnectPorts(color,0,mask,0) && editor.ConnectPorts(mask,0,1,0),"real typed output and input ports connect");
        Check(editor.DeleteNode(mask) && editor.GetDraft().graph.GetLinks().empty(),"deleting a node removes attached links");
        Frame([&] { asset.Draw(); editor.Draw(); });
        Check(editor.IsDirty(),"actual ImGui material modules retain draft state");
    }
    void MaterialWorkflowGpu()
    {
        const auto root=std::filesystem::absolute("Saved/MaterialWorkflow"); const auto assets=root/"assets"; std::filesystem::create_directories(assets);
        PlatformFile::WriteAllText(assets/"quad.mesh","v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0.5 0.5 0\nv -0.5 0.5 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
        PlatformFile::WriteAllText(assets/"default.input",PlatformFile::ReadAllText("assets/default.input"));
        ResourceManager resources; resources.Initialize(nullptr,assets); Renderer renderer; renderer.SetResourceManager(&resources);
        auto gpu=std::make_unique<SdlGlRenderDevice>(128,64,"Material editor workflow"); auto *device=gpu.get();
        Check(renderer.Initialize(std::move(gpu)),"material editor workflow initializes real GPU"); SDL_HideWindow(static_cast<SDL_Window *>(device->GetNativeWindow()));
        device->EnableSceneRenderTarget(true,128,64); device->SetTaaEnabled(false); device->SetGtaoEnabled(false); device->SetBloomEnabled(false); device->SetFxaaEnabled(false);
        auto &system=renderer.GetMaterialSystem(); const auto validator=[&](const auto &compiled,auto &diagnostics) { return device->ValidateMaterial(compiled,diagnostics); };
        MaterialDefinitionData definition; definition.sourcePath=(assets/"Shared.matgraph").generic_u8string(); definition.renderState.shadingModel=MaterialShadingModel::Unlit;
        definition.graph.SetFormatVersion(2); definition.graph.AddNode(MaterialNodeType::SurfaceOutput,"Surface");
        MaterialParameterDecl color; color.name="Tint"; color.type=MaterialParameterType::Vec3; color.defaultValue=Vec3{.3f,.3f,.3f}; definition.parameters={color};
        auto id=definition.graph.AddNode(MaterialNodeType::VectorParameter,"Tint"); definition.graph.GetNodes().back().parameterName="Tint"; definition.graph.Connect(id,1,0);
        MaterialGraphEditor graph; graph.Open(definition.sourcePath,definition); graph.SetSaveRequest([&](const auto &path,const auto &data,auto &diagnostics) { const auto r=system.ApplyDefinitionDraft(path,data,validator); diagnostics=r.diagnostics; return r.success; });
        Check(graph.Save(),"real graph editor saves a validated parent definition");
        std::string error;
        for (const auto &name:{"Red","Blue"}) Check(MaterialAssetEditor::CreateInstance((assets/(std::string(name)+".mat")).generic_u8string(),definition.sourcePath,error),"create instance through material editor command");
        MaterialAssetEditor red,blue; MaterialResource data; data.Load((assets/"Red.mat").generic_u8string()); red.Open((assets/"Red.mat").generic_u8string(),data.GetMaterialData(),definition.parameters);
        data.Load((assets/"Blue.mat").generic_u8string()); blue.Open((assets/"Blue.mat").generic_u8string(),data.GetMaterialData(),definition.parameters);
        const auto save=[&](const auto &path,const auto &draft,auto &diagnostics) { const auto r=system.ApplyInstanceDraft(path,draft,validator); diagnostics=r.diagnostics; return r.success; }; red.SetSaveRequest(save); blue.SetSaveRequest(save);
        red.SetOverride("Tint",Vec3{1,0,0}); blue.SetOverride("Tint",Vec3{0,0,1}); Check(red.Save() && blue.Save(),"save independent red and blue asset overrides");
        Frame([&] { graph.Draw(); red.Draw(); });
        World scene; const auto a=scene.CreateEntity("Red"),b=scene.CreateEntity("Blue");
        for (auto e:{a,b}) { scene.AddComponent(e,MeshComponent{"assets/quad.mesh",e==a ? "assets/Red.mat" : "assets/Blue.mat"}); scene.GetComponent<TransformComponent>(e)->position={e==a ? -.65f : .65f,0,0}; }
        renderer.SetCameraOverride(true,{0,0,3},{}); renderer.RenderFrame(scene); auto hdr=device->ReadHdrAttachmentForTests(0);
        Check(hdr[32*128+52].x>.9f && hdr[32*128+76].z>.9f,"assigned instances render red and blue in the actual scene");
        Check(graph.SetParameterDefault("Tint",Vec3{0,1,0}) && graph.Save(),"parent default changes through the graph editor command");
        auto dynamic=system.CreateDynamicInstance("assets/Blue.mat"); dynamic->SetParameter("Tint",Vec3{1,1,0}); renderer.SetDynamicMaterial(b,dynamic); renderer.RenderFrame(scene); hdr=device->ReadHdrAttachmentForTests(0);
        Check(hdr[32*128+52].x>.9f && hdr[32*128+76].x>.9f && hdr[32*128+76].y>.9f,"dynamic color affects only its assigned scene entity");
        auto broken=graph.GetDraft(); broken.graph.GetLinks()[0].fromNode=999; graph.Open(definition.sourcePath,broken); graph.AddNode(MaterialNodeType::Constant,"Unused");
        Check(!graph.Save() && graph.IsDirty() && !graph.GetDiagnostics().empty(),"broken editor graph cannot replace the live scene material");
        renderer.RenderFrame(scene); hdr=device->ReadHdrAttachmentForTests(0); Check(hdr[32*128+76].y>.9f,"failed editor save keeps the current rendered material");
        graph.DisconnectPort(1,0); Check(graph.ConnectPorts(id,0,1,0) && graph.Save(),"repair graph link and publish from the same editor draft");
        SceneManager scenes(&scene); Check(scenes.SaveScene(assets/"MaterialDemo.scene"),"save scene referencing the new material assets");
        ProjectSettings project; project.name="MaterialWorkflow"; project.projectFile=root/"MaterialWorkflow.tinyproject"; project.assetDirectory="assets"; project.startupScene="assets/MaterialDemo.scene"; Check(project.Save(project.projectFile),"save isolated material example project");
        const auto package=ProjectPackager::Package({project.projectFile,root/"Package",TINYENGINE_TEST_GAME_PATH});
        Check(package.success,"package complete edited material scene with offline validation");
        renderer.Shutdown(); resources.Shutdown();
    }
};
} // 结束 MiniEngine 命名空间

int main()
{
    try
    {
        MiniEngine::EditorInteractionTests tests;
        tests.Shortcuts();
        tests.Documents();
        tests.Inspector();
        tests.Gizmo();
        tests.HierarchyCameraFocus();
        tests.ViewportRegression();
        tests.CompactWorkspace();
        tests.SceneOpenRegression();
        tests.ShaderDocuments();
        tests.WindowClose();
        tests.MaterialDocuments();
        tests.MaterialWorkflowGpu();
        std::cout << tests.checks << " interaction checks passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
