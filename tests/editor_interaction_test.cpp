// 不启动窗口，直接用 ImGui 输入驱动真实 World，检查编辑器命令；不依赖 SDL、DPI 或输入法。
#include "editor_app.h"
#include "material_asset_editor.h"
#include "material_graph_editor.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/project/project_packager.h"
#include "tinyengine/render/sdl_gl_render_device.h"
#include "tinyengine/resource/pak_archive.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SDL_syswm.h>
#endif
#include "tinyengine/platform/platform.h"
#include <chrono>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace tinyengine
{
struct EditorInteractionTests
{
    World world;
    EditorApp app;
    int checks = 0;

    EditorInteractionTests()
    {
        app.m_engine.GetSceneManager().setWorld(&world);
        // 测试 Engine 持有这个上下文；无窗口测试只接入实际会用到的服务。
        const_cast<EngineContext&>(app.m_engine.GetContext()).world = &world;
        app.m_sceneDocuments.push_back({{}, app.m_engine.GetSceneManager().createSnapshot(), false});
        app.m_interfaceLanguage = EditorApp::InterfaceLanguage::English;
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 900};
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~EditorInteractionTests()
    {
        ImGui::DestroyContext();
    }

    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
        ++checks;
        std::cout << "PASS: " << message << '\n';
    }

    void frame(const std::function<void()>& draw = {})
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
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        io.AddKeyEvent(ImGuiMod_Shift, shift);
        io.AddKeyEvent(key, true);
        frame(
            [&]()
            {
                ImGui::Begin("TestHost");
                if (popup)
                    ImGui::OpenPopup("BlockCommands");
                if (ImGui::BeginPopup("BlockCommands"))
                {
                    app.handleShortcuts();
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                else
                {
                    io.WantTextInput = textInput;
                    app.handleShortcuts();
                }
                ImGui::End();
            });
        io.AddKeyEvent(key, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        frame();
    }

    void logicalUiMetrics()
    {
        const auto dpi = app.m_dpiScale, ui = app.m_uiScale;
        app.m_uiScale = 1;
        app.m_dpiScale = 1;
        check(std::abs(app.scaled(96) - 96) < .001f,
              "logical UI dimensions use the configured point size at 100 percent DPI");
        app.m_dpiScale = 2;
        check(std::abs(app.scaled(96) - 96) < .001f,
              "higher framebuffer DPI does not double asset and inspector UI dimensions");
        app.m_uiScale = 1.25f;
        check(std::abs(app.scaled(96) - 120) < .001f,
              "UI size preference scales logical dimensions independently from monitor DPI");
        app.m_dpiScale = dpi;
        app.m_uiScale = ui;
    }

    void projectCreation()
    {
        namespace fs = std::filesystem;
        const auto root =
            fs::temp_directory_path() /
            ("TinyCreateUI-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) /
            fromUtf8Path("中文 with spaces");
        fs::create_directories(root);
        std::snprintf(app.m_newProjectName.data(), app.m_newProjectName.size(), "%s", "UIFirstGame");
        std::snprintf(app.m_newProjectParent.data(), app.m_newProjectParent.size(), "%s", toUtf8Path(root).c_str());
        app.startProjectCreation();
        check(app.m_projectCreationFuture.valid() && app.m_requestedProject.empty(),
              "create and open starts a background build before requesting a project switch");
        const auto wait = [&]
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(70);
            int frames = 0;
            while (app.m_projectCreationFuture.valid() && std::chrono::steady_clock::now() < until)
            {
                frame([&] { app.drawProjectLauncher(); });
                ++frames;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            check(!app.m_projectCreationFuture.valid() && frames > 1, "launcher keeps drawing while C++ code builds");
        };
        wait();
        const auto project = root / "UIFirstGame/UIFirstGame.tinyproject";
        if (app.m_requestedProject != project || !app.m_projectLauncherError.empty())
            std::cerr << app.m_projectLauncherError << '\n' << app.m_projectCreationOutput << '\n';
        check(app.m_requestedProject == project && app.m_projectLauncherError.empty(),
              "successful first compilation automatically opens the new C++ project");
        // 模拟清理忽略的 build/ 后，通过现有菜单重新生成 VS 工程。
        fs::rename(project.parent_path() / "build", project.parent_path() / "saved/InitialBuild");
        const_cast<ProjectSettings&>(app.m_engine.getProjectSettings()).projectFile = project;
        app.startSolutionGeneration();
        app.m_solutionFuture.wait();
        const auto regenerated = app.m_solutionFuture.get();
        if (!regenerated.success)
            std::cerr << regenerated.output << '\n';
        check(regenerated.success && fs::is_regular_file(regenerated.solutionFile),
              "Generate VS rediscovers the installed SDK after clearing the build cache");
        app.m_requestedProject.clear();
        const auto sourceFile = project.parent_path() / "source/game.cpp";
        const auto read = [](const fs::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(stream), {});
        };
        const auto source = read(sourceFile);
        std::ofstream(sourceFile, std::ios::app) << "\n#error deliberate_ui_build_failure\n";
        app.startCreatedProjectBuild();
        wait();
        check(app.m_requestedProject.empty() && !app.m_projectLauncherError.empty() &&
                  !app.m_projectCreationOutput.empty() && fs::exists(project),
              "compile failure shows diagnostics, retains files and does not open an unbuilt project");
        std::ofstream(sourceFile, std::ios::trunc) << source;
        app.startCreatedProjectBuild();
        wait();
        check(app.m_requestedProject == project && app.m_projectLauncherError.empty(),
              "retry after fixing the error opens the retained project");
    }

    void nativeDialogDpi()
    {
#ifdef _WIN32
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/EditorDpi")) /
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root / "assets");
        ProjectSettings project;
        project.name = "DPI regression";
        project.projectFile = root / "Dpi.tinyproject";
        project.assetDirectory = "assets";
        project.startupScene = "assets/Startup.scene";
        World startupWorld;
        startupWorld.createEntity("Player");
        check(SceneSerializer::Save(startupWorld, root / project.startupScene),
              "save valid isolated DPI startup scene");
        check(project.Save(project.projectFile), "save isolated DPI startup fixture");
        auto* previousContext = ImGui::GetCurrentContext();
        EditorApp editor;
        struct Restore
        {
            EditorApp& editor;
            ImGuiContext* context;
            ~Restore()
            {
                editor.shutdown();
                ImGui::SetCurrentContext(context);
            }
        } restore{editor, previousContext};
        ImGui::SetCurrentContext(nullptr);
        check(editor.initialize(project.projectFile), "initialize actual editor before any native picker");
        auto* window = static_cast<SDL_Window*>(editor.m_device->getNativeWindow());
        SDL_HideWindow(window);
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        check(SDL_GetWindowWMInfo(window, &info) == SDL_TRUE, "DPI fixture exposes the real editor HWND");
        const auto context = GetWindowDpiAwarenessContext(info.info.win.window);
        const auto awareness = GetAwarenessFromDpiAwarenessContext(context);
        std::cout << "Editor startup DPI awareness: " << awareness << '\n';
        check(awareness == DPI_AWARENESS_PER_MONITOR_AWARE,
              "editor locks per-monitor DPI awareness before creating windows");
        int width = 0, height = 0, pixelsWide = 0, pixelsHigh = 0;
        SDL_GetWindowSize(window, &width, &height);
        SDL_GL_GetDrawableSize(window, &pixelsWide, &pixelsHigh);
        const float scale = GetDpiForWindow(info.info.win.window) / 96.0f;
        check(std::abs(pixelsWide - width * scale) <= 2 && std::abs(pixelsHigh - height * scale) <= 2,
              "editor SDL coordinates remain logical points on scaled displays");
        // A legacy Shell context-menu extension must not change the process from virtualized to aware mid-session.
        SetProcessDPIAware();
        SDL_PumpEvents();
        int nextWidth = 0, nextHeight = 0, nextPixelsWide = 0, nextPixelsHigh = 0;
        SDL_GetWindowSize(window, &nextWidth, &nextHeight);
        SDL_GL_GetDrawableSize(window, &nextPixelsWide, &nextPixelsHigh);
        check(AreDpiAwarenessContextsEqual(context, GetWindowDpiAwarenessContext(info.info.win.window)),
              "late Shell DPI initialization cannot change editor window awareness");
        check(width == nextWidth && height == nextHeight && pixelsWide == nextPixelsWide &&
                  pixelsHigh == nextPixelsHigh,
              "late Shell DPI initialization preserves logical and physical editor sizes");
        editor.m_dpiScale = scale > 1.2f ? 1.0f : 2.0f;
        editor.Run(1);
        check(std::abs(editor.m_dpiScale - static_cast<float>(pixelsWide) / width) < .01f,
              "editor refreshes scene framebuffer scale from the current SDL frame after a DPI change");
#endif
    }

    void p1Settings()
    {
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        const auto old = settings;
        settings.projectFile = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/P1Settings/Settings.tinyproject"));
        app.m_uiScale = 1.25f;
        app.m_cameraSpeed = 2.5f;
        app.m_showSceneGrid = false;
        app.m_showHierarchy = false;
        app.m_showAssets = false;
        app.m_showPerformance = false;
        check(app.saveEditorSettings(), "P1 preferences save to project-local settings");
        app.m_uiScale = 1;
        app.m_cameraSpeed = 1;
        app.m_showSceneGrid = true;
        app.m_showHierarchy = true;
        app.m_showAssets = true;
        app.m_showPerformance = true;
        app.loadEditorSettings();
        check(std::abs(app.m_uiScale - 1.25f) < .01f && std::abs(app.m_cameraSpeed - 2.5f) < .01f &&
                  !app.m_showSceneGrid,
              "UI scale, camera speed and grid preference survive restart");
        check(!app.m_showHierarchy && !app.m_showAssets && !app.m_showPerformance, "closed panels survive restart");
        settings = old;
        app.m_uiScale = 1;
        app.m_cameraSpeed = 1;
        app.m_showSceneGrid = true;
        app.m_showHierarchy = app.m_showAssets = app.m_showPerformance = true;
    }

    void p1Docking()
    {
        auto* previousContext = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(ImGui::CreateContext());
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 900};
        io.DeltaTime = 1.0f / 60;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        const auto old = settings;
        settings.projectFile = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/P1Docking/Workspace.tinyproject"));
        app.m_resetWorkspace = true;
        const auto draw = [&]
        {
            app.drawWorkspace();
            const auto panel = [&](const char* name, bool& open)
            {
                if (open)
                {
                    app.beginEditorPanel(name, std::string(name) == "Inspector" ? "属性检查器" : name, open, {0, 0},
                                         {300, 300}, std::string(name) == "Scene View" && app.m_sceneMaximized);
                    ImGui::TextUnformatted(name);
                    ImGui::End();
                }
            };
            if (!app.m_sceneMaximized)
            {
                panel("Hierarchy", app.m_showHierarchy);
                panel("Inspector", app.m_showInspector);
                panel("Assets", app.m_showAssets);
                panel("Console / Stats", app.m_showConsole);
                panel("Performance", app.m_showPerformance);
            }
            bool scene = true;
            panel("Scene View", scene);
        };
        frame(draw);
        frame(draw);
        frame(draw);
        const auto assetDock = ImGui::FindWindowByName("Assets")->DockId;
        const auto sceneDock = ImGui::FindWindowByName("Scene View")->DockId;
        check(assetDock && sceneDock && assetDock != sceneDock, "default workspace docks panels around the scene");
        check(ImGui::FindWindowByName("Console / Stats")->DockId == assetDock &&
                  ImGui::FindWindowByName("Performance")->DockId == assetDock,
              "assets console and performance share bottom tabs");
        const auto inspectorId = ImGui::FindWindowByName("Inspector")->ID;
        app.m_interfaceLanguage = EditorApp::InterfaceLanguage::SimplifiedChinese;
        frame(draw);
        check(ImGui::FindWindowByName("Inspector")->ID == inspectorId,
              "language changes preserve panel identity and layout");
        app.m_interfaceLanguage = EditorApp::InterfaceLanguage::English;
        app.m_showInspector = false;
        frame(draw);
        app.m_showInspector = true;
        frame(draw);
        check(ImGui::FindWindowByName("Inspector")->DockId != 0, "closed panel reopens in its dock");
        app.m_sceneMaximized = true;
        frame(draw);
        frame(draw);
        check(ImGui::FindWindowByName("Scene View Focus")->DockId == 0 &&
                  ImGui::FindWindowByName("Scene View Focus")->Size.x >= io.DisplaySize.x - 1,
              "F11 expands scene over the workspace");
        app.m_sceneMaximized = false;
        frame(draw);
        frame(draw);
        check(ImGui::FindWindowByName("Scene View")->DockId == sceneDock, "F11 restores the previous scene dock");
        ImGui::DockBuilderDockWindow("Console / Stats", sceneDock);
        frame(draw);
        frame(draw);
        app.m_showPerformance = false;
        check(app.saveWorkspace(), "custom docking layout saves to project");
        app.resetWorkspace();
        frame(draw);
        frame(draw);
        check(app.m_showPerformance &&
                  ImGui::FindWindowByName("Console / Stats")->DockId != ImGui::FindWindowByName("Scene View")->DockId,
              "reset layout restores default panels and bottom tabs");
        check(app.restoreWorkspace(), "saved layout reloads");
        frame(draw);
        frame(draw);
        check(!app.m_showPerformance &&
                  ImGui::FindWindowByName("Console / Stats")->DockId == ImGui::FindWindowByName("Scene View")->DockId,
              "saved layout restores custom docking and closed panels");
        settings = old;
        io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
        app.m_showHierarchy = app.m_showInspector = app.m_showAssets = app.m_showConsole = app.m_showPerformance = true;
        app.m_sceneMaximized = false;
        ImGui::DestroyContext();
        ImGui::SetCurrentContext(previousContext);
    }

    void p1HiddenImport()
    {
#ifdef TINYENGINE_HAS_ASSIMP
        app.m_importQueue.initialize(nullptr);
        app.m_activeImportTask = app.m_importQueue.submit("saved/NoSuchP1Model.fbx", "saved/P1ImportOutput");
        check(app.m_importQueue.Get(app.m_activeImportTask).status == AssetImportStatus::Failed,
              "missing-model import reaches real terminal status");
        app.m_showAssets = false;
        app.m_sceneMaximized = true;
        const auto messages = app.m_messages.size();
        app.pollAssetImport();
        check(app.m_activeImportTask == 0 && app.m_messages.size() == messages + 1,
              "hidden asset panel still publishes import result and releases packaging lock");
        app.pollAssetImport();
        check(app.m_messages.size() == messages + 1, "import completion publishes once");
        app.m_showAssets = true;
        app.m_sceneMaximized = false;
#endif
    }

    void p1AssetWorkflow()
    {
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/P1AssetWorkflow"));
        std::filesystem::create_directories(root / "assets/Models");
        PlatformFile::writeAllText(root / "assets/Models/a.mesh", "mesh\n");
        ResourceManager resources;
        resources.initialize(nullptr, root / "assets");
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.resourceManager = &resources;
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        const auto old = settings;
        settings.projectFile = root / "Workflow.tinyproject";
        settings.assetDirectory = "assets";
        world.clear();
        const auto e = world.createEntity("Keep inspector");
        app.m_selected = e;
        app.m_selection = {e};
        app.m_selectedAsset.clear();
        app.m_selectedAssets.clear();
        app.m_showAssets = false;
        app.m_assetTypeFilters = {"Material"};
        std::strcpy(app.m_assetSearch.data(), "excluded");
        app.locateAsset("assets/Models/a.mesh");
        check(app.m_showAssets && std::filesystem::equivalent(app.m_assetFolder, root / "assets/Models"),
              "Locate reference opens the asset browser at the containing folder");
        check(app.m_selectedAsset.empty() && app.m_selected == e && app.m_selection == std::vector<Entity>{e},
              "Locate reference preserves entity inspector ownership");
        check(app.m_assetSearch[0] == 0 && app.m_assetTypeFilters.empty() && app.m_selectedAssets.size() == 1,
              "Locate reference clears filters and highlights the target asset");
        app.m_assetRenameRequested = false;
        app.m_assetsFocused = false;
        Key(ImGuiKey_F2);
        check(!app.m_assetRenameRequested, "F2 outside asset browser cannot rename");
        app.m_assetsFocused = true;
        Key(ImGuiKey_F2, false, false, true);
        check(!app.m_assetRenameRequested, "text input owns F2 while editing");
        Key(ImGuiKey_F2);
        check(app.m_assetRenameRequested, "focused asset browser receives F2 rename");
        app.m_assetRenameRequested = false;
        app.m_assetsFocused = false;
        app.m_focusAssets = false;
        app.m_selectedAsset = "keep-open-asset.mat";
        app.locateAsset("assets/Models/a.mesh");
        check(app.m_selectedAsset == "keep-open-asset.mat", "Locate preserves an existing asset inspector owner too");
        const auto locatedFolder = app.m_assetFolder;
        const auto locatedSelection = app.m_selectedAssets;
        app.locateAsset("builtin:PBR");
        app.locateAsset("assets/missing.mesh");
        check(app.m_assetFolder == locatedFolder && app.m_selectedAssets == locatedSelection &&
                  app.m_selectedAsset == "keep-open-asset.mat",
              "invalid and builtin Locate keep the current browser and inspector state");
        app.m_selectedAsset.clear();
        app.m_focusAssets = false;
        app.m_assetRevealPending = false;
        settings = old;
        context.resourceManager = nullptr;
        resources.shutdown();
    }

    void p1SceneNavigation()
    {
        world.clear();
        const auto e = world.createEntity("Orbit target");
        app.m_selected = e;
        app.m_selection = {e};
        app.m_editorCameraPosition = {0, 2, 7};
        app.m_editorCameraRotation = {-15, 0, 0};
        app.m_cameraSpeed = 1;
        const auto camera = [&] { app.updateSceneCamera(true, true); };
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(300, 300);
        frame(camera);
        io.AddKeyEvent(ImGuiMod_Alt, true);
        io.AddMouseButtonEvent(0, true);
        frame(camera);
        const auto before = app.m_editorCameraPosition;
        const float distance = length(before);
        io.AddMousePosEvent(345, 315);
        frame(camera);
        check(length(app.m_editorCameraPosition - before) > .1f &&
                  std::abs(length(app.m_editorCameraPosition) - distance) < .01f,
              "Alt-left orbit changes camera angle while preserving target distance");
        io.AddMouseButtonEvent(0, false);
        io.AddKeyEvent(ImGuiMod_Alt, false);
        frame(camera);
        io.AddMouseButtonEvent(2, true);
        frame(camera);
        const auto panBefore = app.m_editorCameraPosition;
        io.AddMousePosEvent(365, 325);
        frame(camera);
        check(length(app.m_editorCameraPosition - panBefore) > .01f, "middle mouse pans the scene camera");
        io.AddMouseButtonEvent(2, false);
        frame(camera);
        const auto baseline = app.m_editorCameraPosition;
        app.m_cameraSpeed = 1;
        io.AddKeyEvent(ImGuiKey_W, true);
        frame(camera);
        const float slow = length(app.m_editorCameraPosition - baseline);
        io.AddKeyEvent(ImGuiKey_W, false);
        frame(camera);
        app.m_editorCameraPosition = baseline;
        app.m_cameraSpeed = 2;
        io.AddKeyEvent(ImGuiKey_W, true);
        frame(camera);
        check(std::abs(length(app.m_editorCameraPosition - baseline) - slow * 2) < .001f,
              "camera speed changes actual navigation distance");
        io.AddKeyEvent(ImGuiKey_W, false);
        frame(camera);
        app.m_cameraSpeed = 1;
        AssetInfo mesh;
        mesh.path = "assets/player.mesh";
        mesh.type = "Mesh";
        const Vec3 placement{4, 1, -2};
        app.m_undo.clear();
        const auto original = app.m_engine.GetSceneManager().createSnapshot();
        check(app.placeAssetInScene(mesh, &placement), "pointer placement creates a mesh");
        const auto* transform = world.GetComponent<TransformComponent>(app.m_selected);
        check(transform && length(transform->position - placement) < .001f && app.m_undo.size() == 1,
              "mesh placement and position form one undo transaction");
        app.undo();
        check(app.m_engine.GetSceneManager().createSnapshot() == original, "undo removes the entire pointer placement");
        world.clear();
        const auto surface = world.createEntity("Surface");
        world.AddComponent(surface, ColliderComponent{});
        app.m_editorCameraPosition = {0, 0, 5};
        app.m_editorCameraRotation = {};
        Vec3 hit;
        const auto querySnapshot = app.m_engine.GetSceneManager().createSnapshot();
        check(app.getScenePlacement(.5f, .5f, hit) && std::abs(hit.z - .5f) < .001f,
              "pointer placement hits nearest collider with no device");
        check(app.m_engine.GetSceneManager().createSnapshot() == querySnapshot,
              "placement preview query never modifies World");
        world.clear();
        app.m_editorCameraPosition = {0, 2, 5};
        app.m_editorCameraRotation = {-30, 0, 0};
        check(app.getScenePlacement(.5f, .5f, hit) && std::abs(hit.y) < .001f,
              "empty-space placement falls back to ground plane");
        app.m_editorCameraRotation = {30, 0, 0};
        check(!app.getScenePlacement(.5f, .5f, hit), "placement above horizon has no valid drop target");
        World prefab;
        const auto first = prefab.createEntity("First"), second = prefab.createEntity("Second");
        prefab.GetComponent<TransformComponent>(first)->position = {-2, 0, 0};
        prefab.GetComponent<TransformComponent>(second)->position = {2, 0, 0};
        const auto prefabPath = std::filesystem::absolute(fromUtf8Path("saved/P1Scene/中文预制体.prefab"));
        std::filesystem::create_directories(prefabPath.parent_path());
        check(SceneSerializer::Save(prefab, prefabPath), "multi-root prefab fixture saves under a Unicode path");
        AssetInfo prefabAsset;
        prefabAsset.type = "Prefab";
        prefabAsset.path = toUtf8GenericPath(prefabPath);
        app.m_undo.clear();
        check(app.placeAssetInScene(prefabAsset, &placement) && app.m_selection.size() == 2,
              "pointer placement loads Unicode multi-root prefab");
        const auto a = world.GetComponent<TransformComponent>(app.m_selection[0])->position;
        const auto b = world.GetComponent<TransformComponent>(app.m_selection[1])->position;
        check(length((a + b) * .5f - placement) < .001f && std::abs(length(a - b) - 4) < .001f &&
                  app.m_undo.size() == 1,
              "prefab roots preserve offsets around pointer in one undo transaction");
        app.undo();
        check(world.GetEntityManager().GetAliveEntities().empty(), "one undo removes all placed prefab roots");
        app.m_sceneOrbitActive = app.m_scenePanActive = app.m_sceneAltOrbitActive = true;
        Key(ImGuiKey_F11);
        check(app.m_sceneMaximized && !app.m_sceneOrbitActive && !app.m_scenePanActive && !app.m_sceneAltOrbitActive,
              "released viewport gestures cannot block F11 while scene tab is hidden");
        app.m_sceneMaximized = false;
        app.resetSceneInteraction();
        app.m_editorCameraRotation = {-10, 0, 0};
        const auto focus = world.createEntity("Dock aspect framing");
        world.AddComponent(focus, ColliderComponent{});
        Renderer projectionRenderer;
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.renderer = &projectionRenderer;
        auto& frame = const_cast<FrameView&>(projectionRenderer.GetFrameView());
        frame.projection = perspective(60, 1.6f, .1f, 100);
        app.focusSceneEntity(focus);
        const float wideDistance = length(app.m_editorCameraPosition);
        frame.projection = perspective(60, .25f, .1f, 100);
        app.focusSceneEntity(focus);
        check(length(app.m_editorCameraPosition) > wideDistance * 2,
              "focus frames selected bounds using the actual narrow dock projection");
        context.renderer = nullptr;
        world.clear();
    }

    void groundGridContinuity()
    {
        world.clear();
        app.m_selected = kInvalidEntity;
        app.m_selection.clear();
        Renderer renderer;
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.renderer = &renderer;
        app.m_showSceneGrid = true;
        app.m_showSelectionBounds = false;
        struct Restore
        {
            EditorApp& app;
            EngineContext& context;
            ~Restore()
            {
                context.renderer = nullptr;
                app.m_showSelectionBounds = true;
            }
        } restore{app, context};
        auto& view = const_cast<FrameView&>(renderer.GetFrameView());
        // Rasterize the real overlay triangles, so coverage/opacity jumps are observable without a GPU.
        int gridVertices = 0;
        const auto capture = [&](Vec3 camera, Vec3 direction = Vec3{-.35f, -.55f, -1})
        {
            constexpr int kW = 240, kH = 180;
            std::vector<float> pixels(kW * kH);
            view.cameraPosition = camera;
            view.view = lookAt(camera, camera + normalize(direction), {0, 1, 0});
            view.projection = perspective(60, 4.0f / 3.0f, .1f, 1000);
            frame(
                [&]
                {
                    ImGui::SetNextWindowPos({0, 0});
                    ImGui::SetNextWindowSize({1000, 800});
                    ImGui::Begin("Grid continuity");
                    auto* draw = ImGui::GetWindowDrawList();
                    const int first = draw->IdxBuffer.Size, firstVertex = draw->VtxBuffer.Size;
                    // Use geometry AA so coverage is represented by vertices rather than an atlas texture.
                    const auto flags = draw->Flags;
                    draw->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
                    app.drawSceneOverlays(20, 40, 800, 600, false);
                    draw->Flags = flags;
                    gridVertices = draw->VtxBuffer.Size - firstVertex;
                    for (int index = first; index + 2 < draw->IdxBuffer.Size; index += 3)
                    {
                        const auto &a = draw->VtxBuffer[draw->IdxBuffer[index]],
                                   &b = draw->VtxBuffer[draw->IdxBuffer[index + 1]],
                                   &c = draw->VtxBuffer[draw->IdxBuffer[index + 2]];
                        const ImU32 rgbMask = ~(255u << IM_COL32_A_SHIFT), gridRgb = IM_COL32(165, 174, 184, 0);
                        if ((a.col & rgbMask) != gridRgb || (b.col & rgbMask) != gridRgb ||
                            (c.col & rgbMask) != gridRgb)
                            continue;
                        const auto cross = [](ImVec2 p, ImVec2 q, ImVec2 r)
                        { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
                        const float area = cross(a.pos, b.pos, c.pos);
                        if (std::abs(area) < 1e-8f)
                            continue;
                        const int minX = std::clamp(
                            static_cast<int>(std::floor((std::min({a.pos.x, b.pos.x, c.pos.x}) - 20) * kW / 800)), 0,
                            kW - 1);
                        const int maxX = std::clamp(
                            static_cast<int>(std::ceil((std::max({a.pos.x, b.pos.x, c.pos.x}) - 20) * kW / 800)), 0,
                            kW - 1);
                        const int minY = std::clamp(
                            static_cast<int>(std::floor((std::min({a.pos.y, b.pos.y, c.pos.y}) - 40) * kH / 600)), 0,
                            kH - 1);
                        const int maxY = std::clamp(
                            static_cast<int>(std::ceil((std::max({a.pos.y, b.pos.y, c.pos.y}) - 40) * kH / 600)), 0,
                            kH - 1);
                        const auto alpha = [](ImU32 col)
                        { return static_cast<float>((col >> IM_COL32_A_SHIFT) & 255) / 255; };
                        for (int row = minY; row <= maxY; ++row)
                            for (int column = minX; column <= maxX; ++column)
                            {
                                // Ignore the orientation widget in the upper right.
                                if (column > kW * 3 / 4 && row < kH / 3)
                                    continue;
                                const ImVec2 point{20 + (column + .5f) * 800 / kW, 40 + (row + .5f) * 600 / kH};
                                const float u = cross(b.pos, c.pos, point) / area,
                                            v = cross(c.pos, a.pos, point) / area, t = 1 - u - v;
                                if (u < 0 || v < 0 || t < 0)
                                    continue;
                                const float opacity = u * alpha(a.col) + v * alpha(b.col) + t * alpha(c.col);
                                auto& pixel = pixels[row * kW + column];
                                pixel += opacity * (1 - pixel);
                            }
                    }
                    ImGui::End();
                });
            return pixels;
        };
        capture({.3f, 2, 5}); // Establish ImGui window state before comparing frames.
        for (float height : {2.5f, 25.0f})
        {
            const auto before = capture({.3f, height - .0001f, 5}), after = capture({.3f, height + .0001f, 5});
            float difference = 0;
            for (std::size_t i = 0; i < before.size(); ++i)
                difference += std::abs(before[i] - after[i]);
            difference /= static_cast<float>(before.size());
            std::cout << "Grid height boundary image delta: " << difference << '\n';
            check(difference < .002f,
                  "tiny vertical camera movement preserves grid coverage and density across old decade boundaries");
        }
        const auto before = capture({.0999f, 2, 5}), after = capture({.1001f, 2, 5});
        float difference = 0;
        for (std::size_t i = 0; i < before.size(); ++i)
            difference += std::abs(before[i] - after[i]);
        check(difference / before.size() < .002f,
              "grid recentering does not expose moving rectangular edges during camera translation");
        const auto initial = capture({0, 2.5f, 7}, {0, -.173648f, -.984808f});
        float coverage = 0, horizon = 0;
        for (float pixel : initial)
            coverage += pixel;
        check(coverage / initial.size() > .002f, "initial editor ground grid remains visibly rendered");
        for (int row = 59; row < 76; ++row)
        {
            float sum = 0;
            for (int column = 0; column < 180; ++column)
                sum += initial[row * 240 + column];
            horizon = std::max(horizon, sum / 180);
        }
        std::cout << "Grid horizon peak row coverage: " << horizon << '\n';
        check(horizon < .2f, "distant subpixel grid lines fade before accumulating into a bright horizon band");
        for (Vec3 direction : {Vec3{0, 0, -1}, Vec3{1, -.001f, 0}, Vec3{0, -1, -.001f}})
        {
            // A top view must keep the ground beyond the fixture's .1 near plane.
            const auto image = capture({.3f, direction.y < -.5f ? .5f : .05f, 5}, direction);
            float visible = 0;
            for (float pixel : image)
                visible += pixel;
            std::cout << "Grid low camera coverage/vertices: " << visible / image.size() << '/' << gridVertices << '\n';
            check(visible / image.size() > .0001f && gridVertices < 60000,
                  "horizontal and near-ground navigation keeps visible grid geometry within the UI mesh budget");
        }
    }

    void viewCubeNavigation()
    {
        world.clear();
        const auto entity = world.createEntity("ViewCube target");
        world.GetComponent<TransformComponent>(entity)->position = {3, 1, 0};
        app.resetSceneInteraction();
        app.m_selected = entity;
        app.m_selection = {entity};
        app.m_undo.clear();
        app.m_showSceneGrid = false;
        app.m_showSelectionBounds = false;
        app.m_editorCameraPosition = {3, 1, 7};
        app.m_editorCameraRotation = {};
        Renderer renderer;
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.renderer = &renderer;
        auto& io = ImGui::GetIO();
        bool hovered = true;
        const auto draw = [&]
        {
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({1100, 850});
            ImGui::Begin("ViewCube test");
            app.drawSceneOverlays(50, 50, 800, 700, hovered);
            ImGui::End();
        };
        io.AddMousePosEvent(772, 128);
        frame(draw);
        frame(draw);
        const auto before = app.m_editorCameraPosition;
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        check(length(app.m_editorCameraPosition - before) < .001f,
              "ViewCube press waits for click or drag without jumping");
        io.AddMousePosEvent(808, 148);
        frame(draw);
        check(length(app.m_editorCameraPosition - before) > .1f, "dragging ViewCube orbits the camera");
        check(std::abs(length(app.m_editorCameraPosition - Vec3{3, 1, 0}) - 7) < .001f,
              "ViewCube orbit preserves selected target and distance");
        const auto during = app.m_editorCameraPosition;
        hovered = false;
        io.AddMousePosEvent(910, 220);
        frame(draw);
        check(length(app.m_editorCameraPosition - during) > .1f,
              "ViewCube drag stays captured beyond the widget and viewport");
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        const auto released = app.m_editorCameraPosition;
        io.AddMousePosEvent(920, 230);
        frame(draw);
        check(length(app.m_editorCameraPosition - released) < .001f,
              "releasing ViewCube stops orbit without snapping to a face");
        check(app.m_selected == entity && app.m_undo.empty(), "ViewCube navigation preserves selection and scene undo");
        const auto click = [&](float x, float y)
        {
            io.AddMousePosEvent(x, y);
            frame(draw);
            io.AddMouseButtonEvent(0, true);
            frame(draw);
            io.AddMouseButtonEvent(0, false);
            frame(draw);
        };
        hovered = true;
        app.m_editorCameraPosition = {3, 1, 7};
        app.m_editorCameraRotation = {};
        io.AddMousePosEvent(772, 128);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        check(std::abs(app.m_editorCameraRotation.y) < .01f && std::abs(app.m_editorCameraRotation.x) < .01f,
              "clicking the front face selects the front direction");
        click(795, 128);
        check(std::abs(app.m_editorCameraRotation.y + 45) < .01f && std::abs(app.m_editorCameraRotation.x) < .01f,
              "clicking a cube edge aligns to its diagonal direction");
        app.m_editorCameraPosition = {3, 1, 7};
        app.m_editorCameraRotation = {};
        click(795, 105);
        check(std::abs(app.m_editorCameraRotation.y + 45) < .01f &&
                  std::abs(app.m_editorCameraRotation.x + 35.264f) < .02f,
              "clicking a cube corner aligns to its isometric direction");
        app.m_editorCameraPosition = {3, 1, 7};
        app.m_editorCameraRotation = {};
        io.AddMousePosEvent(772, 128);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddFocusEvent(false);
        frame(draw);
        io.AddFocusEvent(true);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        const auto restored = app.m_editorCameraPosition;
        io.AddMousePosEvent(800, 140);
        frame(draw);
        check(length(app.m_editorCameraPosition - restored) < .001f, "focus loss cancels ViewCube capture");
        app.m_editorCameraPosition = {3, 1, 7};
        app.m_editorCameraRotation = {};
        io.AddMousePosEvent(772, 128);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        ImGui::SetNextWindowPos({0, 0});
        frame(
            [&]
            {
                ImGui::Begin("ViewCube test");
                ImGui::OpenPopup("Block cube");
                if (ImGui::BeginPopup("Block cube"))
                {
                    app.drawSceneOverlays(50, 50, 800, 700, true);
                    ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::End();
            });
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        check(!app.m_viewCubeActive && std::abs(app.m_editorCameraRotation.y) < .01f,
              "popup cancels ViewCube capture without moving the camera");
        context.renderer = nullptr;
        app.m_showSceneGrid = app.m_showSelectionBounds = true;
        app.resetSceneInteraction();
        world.clear();
    }

    void p1OrbitStability()
    {
        world.clear();
        const auto e = world.createEntity("Off-center orbit");
        world.GetComponent<TransformComponent>(e)->position = {10, 2, 0};
        app.m_selected = e;
        app.m_selection = {e};
        app.m_editorCameraPosition = {0, 2, 7};
        app.m_editorCameraRotation = {0, 0, 0};
        const auto initialPosition = app.m_editorCameraPosition;
        const auto initialRotation = app.m_editorCameraRotation;
        const auto draw = [&] { app.updateSceneCamera(true, true); };
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(300, 300);
        frame(draw);
        io.AddKeyEvent(ImGuiMod_Alt, true);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        frame(draw);
        check(length(app.m_editorCameraPosition - initialPosition) < .0001f &&
                  length(app.m_editorCameraRotation - initialRotation) < .0001f,
              "holding Alt-orbit stationary does not snap to an off-center selection");
        io.AddMousePosEvent(320, 300);
        frame(draw);
        check(std::abs(app.m_editorCameraRotation.y - 3.6f) < .01f,
              "off-center orbit applies mouse angle without an orientation jump");
        io.AddMouseButtonEvent(0, false);
        io.AddKeyEvent(ImGuiMod_Alt, false);
        frame(draw);
        app.resetSceneInteraction();
        app.m_editorCameraRotation = {-10, 0, 0};
        world.clear();
    }

    void p1SceneDrag()
    {
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/P1SceneDrag")) /
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto assets = root / "assets";
        std::filesystem::create_directories(assets);
        PlatformFile::writeAllText(assets / "a.mesh", PlatformFile::readAllText("assets/player.mesh"));
        ResourceManager resources;
        resources.initialize(nullptr, assets);
        Renderer renderer;
        renderer.setResourceManager(&resources);
        auto gpu = std::make_unique<SdlGlRenderDevice>(256, 128, "Viewport drag transaction");
        auto* device = gpu.get();
        check(renderer.initialize(std::move(gpu)), "viewport drop fixture initializes real render target");
        SDL_HideWindow(static_cast<SDL_Window*>(device->getNativeWindow()));
        device->enableSceneRenderTarget(true, 256, 128);
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.resourceManager = &resources;
        context.renderer = &renderer;
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        const auto oldSettings = settings;
        settings.projectFile = root / "Drag.tinyproject";
        settings.assetDirectory = "assets";
        app.m_device = device;
        world.clear();
        app.resetSceneInteraction();
        app.m_undo.clear();
        app.m_redo.clear();
        app.m_sceneDirty = false;
        app.m_assetFolder = assets;
        app.m_assetViewMode = EditorApp::AssetViewMode::List;
        app.m_assetSearch.fill(0);
        app.m_assetTypeFilters.clear();
        app.m_showAssets = true;
        app.m_sceneMaximized = false;
        app.m_editorCameraPosition = {0, 2, 7};
        app.m_editorCameraRotation = {-20, 0, 0};
        const auto draw = [&]
        {
            renderer.setCameraOverride(true, app.m_editorCameraPosition, app.m_editorCameraRotation,
                                       app.m_editorCameraFov);
            renderer.renderFrame(world);
            app.drawSceneView();
            app.drawAssets();
        };
        frame(draw);
        frame(draw);
        ImGuiWindow* browser = nullptr;
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
            if (candidate->ParentWindow == ImGui::FindWindowByName("Assets") &&
                std::string(candidate->Name).find("AssetBrowserItems") != std::string::npos)
                browser = candidate;
        check(browser != nullptr, "viewport drag fixture exposes asset rows");
        auto* table = ImGui::GetCurrentContext()->Tables.GetByKey(browser->GetID("AssetList"));
        check(table != nullptr, "viewport drag fixture exposes actual list table");
        const float rowHeight = ImGui::GetFontSize() + ImGui::GetStyle().CellPadding.y * 2;
        const ImVec2 source{table->Columns[0].MinX + 24, table->OuterRect.Min.y + rowHeight * 1.5f};
        const auto* scene = ImGui::FindWindowByName("Scene View");
        auto& io = ImGui::GetIO();
        const ImVec2 cubeHome{scene->WorkRect.Min.x + device->getSceneTargetWidth() / app.m_dpiScale - 130,
                              scene->DC.CursorMaxPos.y - device->getSceneTargetHeight() / app.m_dpiScale + 25};
        io.AddMousePosEvent(cubeHome.x, cubeHome.y);
        frame(draw);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        check(app.m_viewCubeActive, "ViewCube captures input above the actual viewport image");
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        frame(draw);
        check(std::abs(app.m_editorCameraRotation.y + 35) < .01f,
              "ViewCube home click completes above the actual viewport image");
        const ImVec2 cubeCenter{cubeHome.x + 52, cubeHome.y + 53};
        io.AddMousePosEvent(cubeCenter.x, cubeCenter.y);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        const auto orbitBefore = app.m_editorCameraPosition;
        io.AddMousePosEvent(cubeCenter.x + 20, cubeCenter.y + 10);
        frame(draw);
        check(length(app.m_editorCameraPosition - orbitBefore) > .1f,
              "ViewCube drag works with real viewport input ownership");
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        app.m_editorCameraPosition = {0, 2, 7};
        app.m_editorCameraRotation = {-20, 0, 0};
        frame(draw);
        frame(draw);
        check(scene && scene->ScrollMax.y < 1, "viewport overlay does not introduce a vertical scrollbar");
        app.m_viewportRenderMode = ViewportRenderMode::Unlit;
        frame(draw);
        frame(draw);
        check(renderer.GetFrameView().renderMode == ViewportRenderMode::Unlit,
              "viewport selection reaches renderer frame state");
        renderer.setCameraOverride(false);
        renderer.renderFrame(world);
        check(renderer.GetFrameView().renderMode == ViewportRenderMode::Lit,
              "runtime camera always restores normal Lit rendering");
        app.m_viewportRenderMode = ViewportRenderMode::Lit;
        frame(draw);
        frame(draw);
        const ImVec2 target{scene->InnerRect.Min.x + scene->InnerRect.GetWidth() * .7f, scene->InnerRect.Max.y - 30};
        io.AddMousePosEvent(source.x, source.y);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMousePosEvent(source.x + 20, source.y);
        frame(draw);
        check(ImGui::GetDragDropPayload() != nullptr, "real asset mouse gesture starts a payload");
        io.AddMousePosEvent(target.x, target.y);
        frame(draw);
        frame(draw);
        check(world.GetEntityManager().GetAliveEntities().empty() && app.m_undo.empty() && !app.m_sceneDirty,
              "hovering placement preview never creates an entity or undo record");
        check(!app.m_scenePreviewAsset.empty() && app.m_scenePreviewBoundsValid,
              "hovering viewport resolves ghost bounds for the dragged mesh");
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        frame(draw);
        check(world.GetEntityManager().GetAliveEntities().size() == 1 && app.m_undo.size() == 1,
              "drop delivery creates exactly one mesh and one undo record");
        const auto* transform = world.GetComponent<TransformComponent>(app.m_selected);
        check(transform && std::abs(transform->position.y) < .001f && std::abs(transform->position.x) > .1f,
              "delivered mesh uses the preview pointer position instead of the origin");
        check(PlatformFile::writeAllText(assets / "z.mat", makeBuiltinPbrDefinition().graph.serialize()),
              "create node material for viewport drag");
        resources.getAssetRegistry().scan(assets);
        frame(draw);
        frame(draw);
        table = ImGui::GetCurrentContext()->Tables.GetByKey(browser->GetID("AssetList"));
        const ImVec2 materialSource{table->Columns[0].MinX + 24, table->OuterRect.Min.y + rowHeight * 2.5f};
        io.AddMousePosEvent(materialSource.x, materialSource.y);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMousePosEvent(materialSource.x + 20, materialSource.y);
        frame(draw);
        check(ImGui::GetDragDropPayload() != nullptr, "real node material drag creates an asset payload");
        const auto* materialPayload = ImGui::GetDragDropPayload();
        check(fromUtf8Path(static_cast<const char*>(materialPayload->Data)).filename() == "z.mat",
              "viewport drag carries the actual node material path");
        // Drop inside the triangle, away from the ground-placement pivot on its edge.
        const auto point = transform->position + Vec3{.25f, .25f, 0};
        const auto matrix = renderer.GetFrameView().projection * renderer.GetFrameView().view;
        const float clipX =
            matrix.at(0, 0) * point.x + matrix.at(0, 1) * point.y + matrix.at(0, 2) * point.z + matrix.at(0, 3);
        const float clipY =
            matrix.at(1, 0) * point.x + matrix.at(1, 1) * point.y + matrix.at(1, 2) * point.z + matrix.at(1, 3);
        const float clipW =
            matrix.at(3, 0) * point.x + matrix.at(3, 1) * point.y + matrix.at(3, 2) * point.z + matrix.at(3, 3);
        const ImVec2 meshTarget{
            scene->WorkRect.Min.x + (clipX / clipW * .5f + .5f) * device->getSceneTargetWidth() / app.m_dpiScale,
            scene->WorkRect.Max.y - (.5f + clipY / clipW * .5f) * device->getSceneTargetHeight() / app.m_dpiScale};
        io.AddMousePosEvent(meshTarget.x, meshTarget.y);
        frame(draw);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        frame(draw);
        check(world.GetEntityManager().GetAliveEntities().size() == 1 && app.m_undo.size() == 2 &&
                  world.GetComponent<MeshComponent>(app.m_selected)->materialPath == "assets/z.mat",
              "dropping node material on scene mesh assigns it without spawning an entity");
        app.undo();
        check(world.GetComponent<MeshComponent>(*world.GetEntityManager().GetAliveEntities().begin())->materialPath ==
                  "assets/default.mat",
              "one undo reverses viewport node material assignment");
        app.undo();
        check(world.GetEntityManager().GetAliveEntities().empty(), "one undo reverses the delivered viewport drop");
        app.resetSceneInteraction();
        settings = oldSettings;
        app.m_device = nullptr;
        context.renderer = nullptr;
        context.resourceManager = nullptr;
        renderer.shutdown();
        resources.shutdown();
        app.m_editorCameraRotation = {-10, 0, 0};
    }

    void shortcuts()
    {
        // Command tests start with one explicit owner; browser gestures can retain panel focus.
        app.m_assetsFocused = false;
        app.m_shaderFocused = false;
        app.m_assetDeleteRequested = false;
        app.m_materialGraphEditor.hide();
        app.m_materialAssetEditor.hide();
        const Entity entity = world.createEntity("Selected");
        app.m_selected = entity;
        app.m_selection = {entity};
        world.GetComponent<TransformComponent>(entity)->position = {14.0f, 2.0f, -6.0f};
        app.m_editorCameraPosition = {0.0f, 2.5f, 7.0f};
        app.m_sceneCommandsFocused = false;
        Key(ImGuiKey_F);
        check(app.m_editorCameraPosition.x == 0.0f, "F in another panel does not move the scene camera");
        app.m_sceneCommandsFocused = true;
        Key(ImGuiKey_F, false, false, true);
        check(app.m_editorCameraPosition.x == 0.0f, "text editing owns the F key");
        Key(ImGuiKey_F);
        check(std::abs(app.m_editorCameraPosition.x - 14.0f) < 0.01f,
              "F frames the selected entity when scene commands have focus");
        app.m_sceneCommandsFocused = false;
        Key(ImGuiKey_Delete);
        check(world.isAlive(entity) && app.m_undo.empty(), "Delete in another panel does not delete scene objects");
        app.m_sceneCommandsFocused = true;
        Key(ImGuiKey_Delete, false, false, true);
        check(world.isAlive(entity), "text editing owns Delete");
        Key(ImGuiKey_Delete, false, false, false, true);
        check(world.isAlive(entity), "popup at any stack level blocks scene Delete");
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true);
        frame();
        app.m_sceneOrbitActive = true;
        Key(ImGuiKey_Delete);
        check(world.isAlive(entity), "camera gesture blocks scene commands");
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        frame();
        app.m_sceneOrbitActive = false;
        app.m_marqueeActive = true;
        Key(ImGuiKey_Escape);
        check(!app.m_marqueeActive && app.m_selection.empty() && world.isAlive(entity),
              "Escape ends selection gestures without deleting objects");
        app.m_selected = entity;
        app.m_selection = {entity};
        Key(ImGuiKey_Delete);
        check(world.GetEntityManager().GetAliveEntities().empty() && app.m_undo.size() == 1,
              "focused Delete removes objects once");
        Key(ImGuiKey_Z, true);
        check(world.GetEntityManager().GetAliveEntities().size() == 1 && app.m_selection.empty(),
              "Ctrl+Z restores scene and clears stale selection");
        Key(ImGuiKey_Z, true, true);
        check(world.GetEntityManager().GetAliveEntities().empty(), "Ctrl+Shift+Z redoes instead of undoing");
        app.m_sceneCommandsFocused = false;
        Key(ImGuiKey_Z, true);
        check(world.GetEntityManager().GetAliveEntities().empty(),
              "Ctrl+Z in another editor cannot undo scene changes");
        app.m_inspectorFocused = true;
        Key(ImGuiKey_Z, true);
        check(world.GetEntityManager().GetAliveEntities().size() == 1,
              "Inspector retains scene Undo after field editing ends");
        app.m_inspectorFocused = false;
    }

    void documents()
    {
        app.m_undo.clear();
        app.m_redo.clear();
        app.recordUndo();
        world.createEntity("A edit");
        const auto sceneA = app.m_engine.GetSceneManager().createSnapshot();
        app.m_selection = {*world.GetEntityManager().GetAliveEntities().begin()};
        app.m_lastPickValid = true;
        app.m_activeGizmoAxis = 1;
        app.newSceneDocument();
        check(app.m_selection.empty() && !app.m_lastPickValid && app.m_activeGizmoAxis == -1,
              "new scene clears selection, picking cache and active drag");
        app.recordUndo();
        world.createEntity("B edit");
        check(app.switchSceneDocument(0) && app.m_undo.size() == 1, "switch back restores A Undo history");
        check(app.m_engine.GetSceneManager().createSnapshot() == sceneA, "switch back restores A contents");
        app.undo();
        const auto undoneA = app.m_engine.GetSceneManager().createSnapshot();
        check(undoneA != sceneA, "A Undo restores its own earlier state");
        check(app.switchSceneDocument(1) && app.m_undo.size() == 1 && app.m_redo.empty(),
              "B history stays independent");
        check(app.switchSceneDocument(0) && app.m_redo.size() == 1, "A Redo survives switching tabs");
        app.redo();
        check(app.m_engine.GetSceneManager().createSnapshot() == sceneA, "A Redo restores exact contents");
        app.m_undo.push_back("TINYSCENE 11\ninvalid\n");
        const auto undoSize = app.m_undo.size();
        app.undo();
        check(app.m_undo.size() == undoSize && app.m_engine.GetSceneManager().createSnapshot() == sceneA,
              "malformed Undo snapshot preserves both scene and history");
        app.m_undo.pop_back();
        const auto selected = *world.GetEntityManager().GetAliveEntities().begin();
        app.m_selected = selected;
        app.m_selection = {selected};
        check(app.closeSceneDocument(1) && app.m_undo.size() == 1 && app.m_selected == selected,
              "closing a background tab preserves active selection and history");
        app.newSceneDocument();
        app.m_sceneDirty = false;
        app.requestEditorExit();
        check(app.m_requestExitPopup && app.m_activeSceneDocument == 0, "exit finds unsaved background scenes");
        app.m_requestExitPopup = false;
        app.m_exitDiscardedDocuments.push_back(0);
        app.m_sceneDocuments[1].dirty = true;
        app.requestEditorExit(true);
        check(app.m_activeSceneDocument == 1 && app.m_sceneDocuments[0].dirty,
              "discard during exit keeps earlier dirty state until all prompts complete");
        app.m_requestExitPopup = false;
        app.m_exitInProgress = false;
        app.m_exitDiscardedDocuments.clear();
        app.requestEditorExit();
        check(app.m_activeSceneDocument == 0, "canceling exit allows earlier unsaved scenes to be prompted again");
        app.m_requestExitPopup = false;
        app.m_exitInProgress = false;
    }

    struct BrowserFixture
    {
        EditorInteractionTests& tests;
        ResourceManager resources;
        ProjectSettings previous;
        std::filesystem::path root;
        BrowserFixture(EditorInteractionTests& owner, const char* name) : tests(owner)
        {
            root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/AssetBrowserRegression")) / name /
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) / "assets";
            std::filesystem::create_directories(root / "Subfolder");
            PlatformFile::writeAllText(root / "a.mat", "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" Vec4 1 0 0 1\n");
            PlatformFile::writeAllText(root / "b.mat", "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" Vec4 0 1 0 1\n");
            PlatformFile::writeAllText(root / "c.ppm", "P3\n1 1\n255\n255 255 255\n");
            PlatformFile::writeAllText(root / "d.mesh", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
            resources.initialize(nullptr, root);
            const_cast<EngineContext&>(tests.app.m_engine.GetContext()).resourceManager = &resources;
            auto& settings = const_cast<ProjectSettings&>(tests.app.m_engine.getProjectSettings());
            previous = settings;
            settings.projectFile = root.parent_path() / "Browser.tinyproject";
            settings.assetDirectory = "assets";
            tests.app.m_showAssets = true;
            tests.app.m_assetFolder = root;
            tests.app.m_assetViewMode = EditorApp::AssetViewMode::List;
            tests.app.m_assetSearch.fill(0);
            tests.app.m_selectedAsset.clear();
            tests.app.m_selectedAssets.clear();
            tests.frame([&] { tests.app.drawAssets(); });
            tests.frame([&] { tests.app.drawAssets(); });
        }
        ~BrowserFixture()
        {
            ImGui::ClosePopupsOverWindow(nullptr, false);
            tests.app.m_showAssetPreview = false;
            tests.app.m_showAssetReferences = false;
            tests.app.m_selectedAsset.clear();
            tests.app.m_selectedAssets.clear();
            const_cast<EngineContext&>(tests.app.m_engine.GetContext()).resourceManager = nullptr;
            const_cast<ProjectSettings&>(tests.app.m_engine.getProjectSettings()) = previous;
            resources.shutdown();
        }
    };

    ImGuiWindow* browserItems()
    {
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (window->ParentWindow == ImGui::FindWindowByName("Assets") &&
                std::string(window->Name).find("AssetBrowserItems") != std::string::npos)
                return window;
        throw std::runtime_error("Asset browser items window is missing");
    }

    void materialAssignmentRegression()
    {
        BrowserFixture fixture(*this, "MaterialAssignment");
        const auto graphPath = toUtf8GenericPath((fixture.root / "NodeMaterial.mat"));
        const auto definition = makeBuiltinPbrDefinition();
        check(PlatformFile::writeAllText(fromUtf8Path(graphPath), definition.graph.serialize()),
              "write assignable node material fixture");
        fixture.resources.getAssetRegistry().scan(fixture.root);
        world.clear();
        app.resetSceneInteraction();
        app.m_undo.clear();
        app.m_redo.clear();
        app.m_sceneDirty = false;
        const auto a = world.createEntity("Material target A"), b = world.createEntity("Material target B");
        for (const auto entity : {a, b})
            world.AddComponent(entity, MeshComponent{"assets/d.mesh", "assets/a.mat"});
        app.m_selected = a;
        app.m_selection = {a, b};
        const auto& mesh = *ReflectionRegistry::Get().findComponent("Mesh Renderer");
        const auto field = std::find_if(mesh.properties.begin(), mesh.properties.end(),
                                        [](const auto& p) { return p.assetType == "Material"; });
        check(field != mesh.properties.end(), "mesh exposes material reference field");
        check(app.applyInspectorAsset(mesh, *field, graphPath, 7241),
              "material picker and drop accept node material assets");
        check(world.GetComponent<MeshComponent>(a)->materialPath == "assets/NodeMaterial.mat" &&
                  world.GetComponent<MeshComponent>(b)->materialPath == "assets/NodeMaterial.mat" &&
                  app.m_undo.size() == 1,
              "node material assignment updates all selected meshes with one portable undo transaction");
        app.undo();
        for (auto entity : world.GetEntityManager().GetAliveEntities())
            check(world.GetComponent<MeshComponent>(entity)->materialPath == "assets/a.mat",
                  "undo restores prior mesh materials");
        MaterialSystem system(fixture.resources);
        const auto proxy = system.BuildProxy(graphPath);
        check(proxy && proxy->compiled, "node material produces a runtime material proxy");
        Renderer renderer;
        renderer.setResourceManager(&fixture.resources);
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.renderer = &renderer;
        app.m_selection.clear();
        for (auto entity : world.GetEntityManager().GetAliveEntities())
            app.m_selection.push_back(entity);
        app.m_selected = app.m_selection.front();
        app.m_undo.clear();
        app.m_redo.clear();
        app.selectAsset(graphPath, false);
        app.openAsset(*fixture.resources.getAssetRegistry().Find(graphPath));
        const auto draw = [&] { app.drawMaterialGraphEditor(); };
        frame(draw);
        frame(draw);
        app.m_materialGraphEditor.addNode(MaterialNodeType::TexCoord, "UV");
        auto* window = ImGui::FindWindowByID(ImHashStr("###MaterialGraphEditor"));
        check(window != nullptr, "node material opens its authoring document");
        ImGui::ActivateItemByID(window->GetID("###ApplyMaterial"));
        frame(draw);
        frame(draw);
        check(!app.m_materialGraphEditor.isDirty() && app.m_undo.size() == 1 && app.m_selectedAsset.empty(),
              "material document saves and applies draft to selected meshes through editor integration");
        for (auto entity : app.m_selection)
            check(world.GetComponent<MeshComponent>(entity)->materialPath == "assets/NodeMaterial.mat",
                  "document Apply assigns node material to every selected mesh");
        check(app.applyMaterialToMeshes(graphPath, app.m_selection) && app.m_undo.size() == 1,
              "reapplying current material succeeds without creating an undo record");
        ImGui::FocusWindow(window);
        frame(draw);
        app.m_assetsFocused = false;
        app.m_sceneCommandsFocused = true;
        Key(ImGuiKey_Delete);
        check(world.GetEntityManager().GetAliveEntities().size() == 2,
              "material document focus prevents scene Delete even with stale scene panel focus");
        app.m_materialGraphEditor.hide();
        app.m_materialAssetEditor.hide();
        app.m_materialGraphEditor.setSaveRequest({});
        app.m_materialGraphEditor.setApplyRequest({});
        app.m_editingAsset.clear();
        app.m_openMaterialGraphAsset.clear();
        context.renderer = nullptr;
        renderer.shutdown();
        app.resetSceneInteraction();
    }

    void assetDeleteShortcutRegression()
    {
        BrowserFixture fixture(*this, "DeleteShortcut");
        world.clear();
        app.resetSceneInteraction();
        app.m_undo.clear();
        const auto entity = world.createEntity("Must survive asset Delete");
        app.m_selected = entity;
        app.m_selection = {entity};
        const auto path = toUtf8GenericPath((fixture.root / "a.mat"));
        app.selectAsset(path, false);
        app.m_assetsFocused = true;
        app.m_sceneCommandsFocused = false;
        Key(ImGuiKey_Delete, false, false, true);
        frame([&] { app.drawAssets(); });
        check(!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
              "asset Delete respects text input ownership");
        app.m_assetsFocused = true;
        Key(ImGuiKey_Delete);
        frame([&] { app.drawAssets(); });
        check(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel),
              "asset Delete opens a recoverable deletion confirmation");
        check(world.isAlive(entity) && std::filesystem::exists(fromUtf8Path(path)),
              "asset Delete leaves files and entities intact until confirmation");
        const auto info = fixture.resources.getAssetRegistry().Find(path);
        check(info.has_value(), "deleted asset starts with persistent metadata");
        browserActivate(browserPopup(), "###ConfirmDeleteAssets");
        check(!std::filesystem::exists(fromUtf8Path(path)) && !app.m_assetDeleteHistory.empty() &&
                  world.isAlive(entity),
              "confirmed asset Delete moves file to trash without deleting scene entity");
        app.m_assetTrashProject.clear();
        app.m_assetDeleteHistory.clear();
        app.loadAssetTrashHistory();
        check(app.m_assetDeleteHistory.size() == 1,
              "project trash manifests remain discoverable after editor state is reloaded");
        app.m_assetsFocused = true;
        Key(ImGuiKey_Z, true);
        check(std::filesystem::exists(fromUtf8Path(path)) &&
                  fixture.resources.getAssetRegistry().Find(path)->id == info->id,
              "asset panel Ctrl+Z restores original file and GUID");
        world.AddComponent(entity, MeshComponent{"assets/d.mesh", "assets/a.mat"});
        app.m_assetsFocused = true;
        app.selectAsset(path, false);
        Key(ImGuiKey_Delete);
        frame([&] { app.drawAssets(); });
        browserActivate(browserPopup(), "###ConfirmDeleteAssets");
        check(std::filesystem::exists(fromUtf8Path(path)) && !app.AssetDeletionBlockers({path}).empty(),
              "current unsaved scene reference blocks asset deletion");
        ImGui::ClosePopupsOverWindow(nullptr, false);
        world.GetComponent<MeshComponent>(entity)->materialPath.clear();
        const auto before = app.m_sceneDocuments;
        world.GetComponent<MeshComponent>(entity)->materialPath = "assets/a.mat";
        const auto snapshot = SceneSerializer::serialize(world);
        world.GetComponent<MeshComponent>(entity)->materialPath.clear();
        app.m_sceneDocuments.push_back({fixture.root / "Other.scene", snapshot, true});
        check(!app.AssetDeletionBlockers({path}).empty(),
              "inactive unsaved scene tab protects its material references");
        app.m_sceneDocuments = before;
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        settings.environmentTexture = "assets/c.ppm";
        check(!app.AssetDeletionBlockers({toUtf8GenericPath((fixture.root / "c.ppm"))}).empty(),
              "project environment reference blocks texture deletion");
        PlatformFile::writeAllText(fixture.root / "draft.frag", "void main() {}\n");
        PlatformFile::writeAllText(fixture.root / "shared.glsl", "vec3 tint = vec3(1);\n");
        fixture.resources.getAssetRegistry().scan(fixture.root);
        app.m_openShaderAsset = toUtf8GenericPath((fixture.root / "draft.frag"));
        app.m_savedShaderSource = "void main() {}\n";
        const std::string shaderDraft = "#include \"shared.glsl\"\nvoid main() {}\n";
        app.m_shaderSource.assign(shaderDraft.begin(), shaderDraft.end());
        app.m_shaderSource.push_back('\0');
        check(!app.AssetDeletionBlockers({toUtf8GenericPath((fixture.root / "shared.glsl"))}).empty(),
              "unsaved shader include blocks deletion of its dependency");
        app.m_openShaderAsset.clear();
        app.m_shaderSource.clear();
        app.m_savedShaderSource.clear();
        const auto previousUndo = app.m_undo, previousRedo = app.m_redo;
        app.m_undo = {snapshot};
        check(!app.AssetDeletionBlockers({path}).empty(),
              "scene undo history protects material references it can restore");
        app.m_undo.clear();
        app.m_redo = {snapshot};
        check(!app.AssetDeletionBlockers({path}).empty(),
              "scene redo history protects material references it can restore");
        app.m_redo.clear();
        app.m_sceneDocuments.push_back(
            {fixture.root / "History.scene", SceneSerializer::serialize(world), true, {snapshot}, {}});
        check(!app.AssetDeletionBlockers({path}).empty(),
              "inactive scene tab undo history protects its retained asset references");
        app.m_sceneDocuments.pop_back();
        app.m_undo = previousUndo;
        app.m_redo = previousRedo;
        app.m_undo.clear();
        app.resetSceneInteraction();
        app.m_assetsFocused = false;
    }

    void browserActivate(ImGuiWindow* window, const char* label)
    {
        ImGui::ActivateItemByID(window->GetID(label));
        const auto draw = [&]
        {
            app.drawAssets();
            app.drawAssetReferenceViewer();
        };
        frame(draw);
        frame(draw);
    }

    void materialTrashCacheRegression()
    {
        BrowserFixture fixture(*this, "MaterialTrashCache");
        world.clear();
        app.resetSceneInteraction();
        const auto path = toUtf8GenericPath((fixture.root / "NewMaterial.mat"));
        auto definition = makeBuiltinPbrDefinition();
        const auto write = [&](float roughness)
        {
            for (auto& parameter : definition.parameters)
                if (parameter.name == "Roughness")
                    parameter.defaultValue = roughness;
            definition.graph.GetParameters() = definition.parameters;
            return PlatformFile::writeAllText(fromUtf8Path(path), definition.graph.serialize());
        };
        check(write(.2f), "write material before caching and deletion");
        fixture.resources.getAssetRegistry().scan(fixture.root);
        Renderer renderer;
        renderer.setResourceManager(&fixture.resources);
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.renderer = &renderer;
        const auto old = renderer.GetMaterialSystem().BuildProxy(path);
        const auto roughness = [](const auto& proxy)
        {
            for (const auto& slot : proxy->compiled->layout.numericSlots)
                if (slot.name == "Roughness")
                    return proxy->numericSlots[slot.slot].x;
            return -1.f;
        };
        check(old && std::abs(roughness(old) - .2f) < .001f, "cache original material values");
        app.selectAsset(path, false);
        app.requestDeleteAssets();
        frame([&] { app.drawAssets(); });
        browserActivate(browserPopup(), "###ConfirmDeleteAssets");
        check(!std::filesystem::exists(fromUtf8Path(path)),
              "delete cached unused material through actual asset dialog");
        check(write(.9f), "recreate material at the same available path");
        fixture.resources.getAssetRegistry().scan(fixture.root);
        const auto replacement = renderer.GetMaterialSystem().BuildProxy(path);
        check(replacement && std::abs(roughness(replacement) - .9f) < .001f,
              "reused deleted material path resolves the new contents instead of cached deleted values");
        check(std::abs(roughness(old) - .2f) < .001f, "evicting deleted material preserves retained render snapshots");
        context.renderer = nullptr;
        renderer.shutdown();
    }

    ImGuiWindow* browserPopup()
    {
        auto& stack = ImGui::GetCurrentContext()->OpenPopupStack;
        return stack.empty() ? nullptr : stack.back().Window;
    }

    void browserInput(ImGuiWindow* window, const char* label, const char* text)
    {
        browserActivate(window, label);
        const auto draw = [&] { app.drawAssets(); };
        auto& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        frame(draw);
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame(draw);
        io.AddInputCharactersUTF8(text);
        frame(draw);
    }

    void assetBrowserMenus()
    {
        BrowserFixture fixture(*this, "menus");
        app.m_assetFolder = fixture.root / "Subfolder";
        const auto draw = [&] { app.drawAssets(); };
        frame(draw);
        frame(draw);
        auto* items = browserItems();
        auto& io = ImGui::GetIO();
        const auto point = items->InnerRect.GetCenter();
        io.AddMousePosEvent(point.x, point.y);
        frame(draw);
        io.AddMouseButtonEvent(1, true);
        frame(draw);
        io.AddMouseButtonEvent(1, false);
        frame(draw);
        frame(draw);
        check(browserPopup() != nullptr, "right click on empty browser space opens the creation menu");
        browserActivate(browserPopup(), "New Folder");
        auto* modal = ImGui::FindWindowByName("New Folder###NewAssetFolder");
        check(modal && modal->Active, "new folder from the context menu opens its name dialog");
        io.AddInputCharactersUTF8("新建 测试");
        frame(draw);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frame(draw);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        frame(draw);
        frame(draw);
        const auto created = fixture.root / "Subfolder" / fromUtf8Path("新建 测试");
        check(std::filesystem::is_directory(created),
              "context creation writes a UTF-8 folder inside the current subfolder");
        check(app.m_assetFolder.lexically_normal() == created.lexically_normal(),
              "created folder becomes the current selected folder");
        browserActivate(ImGui::FindWindowByName("Assets"), "Create...");
        check(browserPopup() != nullptr, "toolbar and right click share the creation menu");
        browserActivate(browserPopup(), "Material");
        check(std::filesystem::exists(created / "NewMaterial.mat") &&
                  fromUtf8Path(app.m_selectedAsset).parent_path().lexically_normal() == created.lexically_normal(),
              "toolbar creates and selects a material graph in the current directory");
        check(!std::filesystem::exists(fixture.root / "NewMaterial.mat"),
              "nested creation does not write to the asset root");
        browserActivate(ImGui::FindWindowByName("Assets"), "Create...");
        browserActivate(browserPopup(), "+ Blend Tree");
        BlendTree1D blend;
        check(loadBlendTreeAsset(created / "NewBlendTree.blendtree", blend) && blend.samples.size() == 1 &&
                  blend.samples.front().clipPath == "assets/Subfolder/新建 测试/NewAnimationClip.skanim",
              "blend trees created in Unicode directories serialize a valid UTF-8 clip reference");
        browserActivate(ImGui::FindWindowByName("Assets"), "Create...");
        browserActivate(browserPopup(), "+ Animation Graph");
        AnimationGraph animation;
        check(loadAnimationGraphAsset(created / "NewAnimationGraph.animgraph", animation) &&
                  animation.states.size() == 1 &&
                  animation.states.front().motionPath == "assets/Subfolder/新建 测试/NewAnimationClip.skanim",
              "animation graphs reuse Unicode starter clips without corrupting dependency paths");
        const auto animationInfo =
            fixture.resources.getAssetRegistry().Find(toUtf8GenericPath((created / "NewAnimationGraph.animgraph")));
        check(animationInfo && animationInfo->dependencies.size() == 1 &&
                  animationInfo->dependencies.front() == "assets/Subfolder/新建 测试/NewAnimationClip.skanim",
              "new animation assets publish their dependencies under the UTF-8 registry key");
        browserActivate(ImGui::FindWindowByName("Assets"), "Create...");
        browserActivate(browserPopup(), "New Folder");
        modal = ImGui::FindWindowByName("New Folder###NewAssetFolder");
        browserInput(modal, "Name", "../escape");
        browserActivate(modal, "Create");
        check(modal->Active && !app.m_assetFolderError.empty() &&
                  !std::filesystem::exists(created.parent_path() / "escape"),
              "folder name validation rejects parent traversal without closing the dialog");
        std::filesystem::create_directory(created / "Existing");
        browserInput(modal, "Name", "Existing");
        browserActivate(modal, "Create");
        check(modal->Active && !app.m_assetFolderError.empty(), "new folder never overwrites an existing directory");
        browserActivate(modal, "Cancel");
        app.m_assetFolder = fixture.root;
        app.m_assetViewMode = EditorApp::AssetViewMode::Grid;
        frame(draw);
        frame(draw);
        items = browserItems();
        io.AddMousePosEvent(items->DC.CursorStartPos.x + 32, items->DC.CursorStartPos.y + 32);
        frame(draw);
        io.AddMouseButtonEvent(1, true);
        frame(draw);
        io.AddMouseButtonEvent(1, false);
        frame(draw);
        frame(draw);
        check(browserPopup() != nullptr, "folder tiles have their own right click creation menu");
        browserActivate(browserPopup(), "Material");
        check(std::filesystem::exists(fixture.root / "Subfolder/NewMaterial.mat"),
              "right click on a folder creates assets inside that folder");
        check(fixture.resources.getAssetRegistry().Find(toUtf8GenericPath((fixture.root / "a.mat"))).has_value(),
              "creating an asset in a subfolder retains assets elsewhere in the registry");
        app.m_assetViewMode = EditorApp::AssetViewMode::List;
        app.m_assetFolder = fixture.root;
        frame(draw);
        frame(draw);
        ImGuiWindow* folders = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (window->ParentWindow == ImGui::FindWindowByName("Assets") &&
                std::string(window->Name).find("AssetFolderTree") != std::string::npos)
                folders = window;
        check(folders != nullptr, "wide browser exposes its folder tree");
        io.AddMousePosEvent(folders->DC.CursorStartPos.x + 60,
                            folders->DC.CursorStartPos.y + ImGui::GetTextLineHeightWithSpacing() * 1.5f);
        frame(draw);
        io.AddMouseButtonEvent(1, true);
        frame(draw);
        io.AddMouseButtonEvent(1, false);
        frame(draw);
        frame(draw);
        check(browserPopup() != nullptr, "folder tree entries expose the shared right click creation actions");
        browserActivate(browserPopup(), "Material");
        check(std::filesystem::exists(fixture.root / "Subfolder/NewMaterial1.mat"),
              "folder tree creation targets the clicked directory and chooses a unique filename");
    }

    void assetBrowserFilters()
    {
        BrowserFixture fixture(*this, "filters");
        auto* assets = ImGui::FindWindowByName("Assets");
        browserActivate(assets, "Filters");
        check(browserPopup() != nullptr, "filters opens the type list directly with one activation");
        browserActivate(browserPopup(), "Material");
        auto rows = [&]
        {
            auto* table = ImGui::GetCurrentContext()->Tables.GetByKey(browserItems()->GetID("AssetList"));
            return table ? table->CurrentRow : -1;
        };
        check(rows() == 2, "material checkbox immediately restricts browser rows to both material assets");
        browserActivate(browserPopup(), "Texture");
        check(rows() == 3, "multiple type filters include the union of material and texture assets");
        ImGui::ClosePopupsOverWindow(nullptr, false);
        frame([&] { app.drawAssets(); });
        const auto tag = ImHashStr("###RemoveFilter", 0, ImHashStr("Material", 0, assets->ID));
        ImGui::ActivateItemByID(tag);
        frame([&] { app.drawAssets(); });
        frame([&] { app.drawAssets(); });
        check(rows() == 1, "removing one visible filter tag leaves the other type filter active");
        browserActivate(assets, "Display");
        check(browserPopup() != nullptr, "display options has its own popup independent of type filters");
        browserActivate(browserPopup(), "Grid");
        check(app.m_assetViewMode == EditorApp::AssetViewMode::Grid, "display popup changes the browser view");
        app.m_assetViewMode = EditorApp::AssetViewMode::List;
        ImGui::ClosePopupsOverWindow(nullptr, false);
        frame([&] { app.drawAssets(); });
        check(rows() == 1, "changing display mode preserves the selected type filters");
        app.locateAsset(toUtf8GenericPath((fixture.root / "a.mat")));
        frame([&] { app.drawAssets(); });
        check(rows() == 4, "locating an asset clears all type filters and reveals the target");
    }

    void assetBrowserReferences()
    {
        BrowserFixture fixture(*this, "references");
        auto& registry = fixture.resources.getAssetRegistry();
        const auto a = toUtf8GenericPath((fixture.root / "a.mat"));
        const auto b = toUtf8GenericPath((fixture.root / "b.mat"));
        registry.setDependencies(a, {"c.ppm", "Missing.png"});
        registry.setDependencies(b, {"a.mat"});
        app.selectAsset(b, false);
        const auto draw = [&]
        {
            app.drawAssets();
            app.drawAssetReferenceViewer();
        };
        frame(draw);
        frame(draw);
        auto* items = browserItems();
        const auto* table = ImGui::GetCurrentContext()->Tables.GetByKey(items->GetID("AssetList"));
        const float height = ImGui::GetFontSize() + ImGui::GetStyle().CellPadding.y * 2;
        const ImVec2 point{table->Columns[0].MinX + 24, table->OuterRect.Min.y + height * 1.5f};
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(point.x, point.y);
        frame(draw);
        io.AddMouseButtonEvent(1, true);
        frame(draw);
        io.AddMouseButtonEvent(1, false);
        frame(draw);
        frame(draw);
        check(browserPopup() != nullptr, "asset right click opens its own context menu");
        browserActivate(browserPopup(), "Reference Viewer");
        auto* viewer = ImGui::FindWindowByName("Asset References###AssetReferences");
        check(viewer && viewer->Active, "asset context menu opens an independent reference graph window");
        check(app.m_selectedAsset == a,
              "right click operations target the clicked asset instead of the previous selection");
        const auto& graph = app.m_assetReferenceGraph;
        check(graph.nodes.size() == 4 && graph.edges.size() == 3,
              "graph includes the relative-path referencer and both real and missing dependencies");
        const auto node = [&](const AssetReferenceGraph& model, const char* name) -> const AssetReferenceNode*
        {
            for (const auto& value : model.nodes)
                if (fromUtf8Path(value.path).filename() == name)
                    return &value;
            return nullptr;
        };
        check(node(graph, "b.mat") && node(graph, "b.mat")->column == -1 && node(graph, "c.ppm") &&
                  node(graph, "c.ppm")->column == 1 && node(graph, "Missing.png") &&
                  node(graph, "Missing.png")->missing,
              "reference graph lays out referencers left, dependencies right, and flags the missing node");
        const auto texture = registry.Find(toUtf8GenericPath((fixture.root / "c.ppm")));
        registry.setDependencies(a, {"c.ppm", texture->id, "Missing.png"});
        const auto deduplicated = buildAssetReferenceGraph(fixture.resources, a, 1);
        check(deduplicated.nodes.size() == 4 && deduplicated.edges.size() == 3,
              "GUID and relative-path references to one asset share one graph node and edge");
        registry.setDependencies(toUtf8GenericPath((fixture.root / "c.ppm")), {"d.mesh"});
        registry.setDependencies(toUtf8GenericPath((fixture.root / "d.mesh")), {"a.mat"});
        const auto cyclic = buildAssetReferenceGraph(fixture.resources, a, 8);
        check(cyclic.nodes.size() == 5 && cyclic.edges.size() == 5,
              "deep graph traversal terminates cycles while retaining all actual reference edges");
        const auto limited = buildAssetReferenceGraph(fixture.resources, a, 8, 3);
        check(limited.nodes.size() == 3 && limited.truncated,
              "large reference graphs enforce the node budget and report truncation");
        const auto unicodeFile = fixture.root / fromUtf8Path("中文 路径") / "Color.mat";
        PlatformFile::writeAllText(unicodeFile, "TINY_MAT 3 instance\nparent \"builtin:PBR\"\nparameter \"BaseColor\" Vec4 1 0 0 1\n");
        registry.scan(fixture.root);
        const auto unicodeGraph = buildAssetReferenceGraph(fixture.resources, toUtf8GenericPath(unicodeFile));
        check(unicodeGraph.nodes.size() == 1 && !unicodeGraph.nodes.front().missing,
              "existing assets under UTF-8 paths are not marked as missing graph references");
        ImGuiWindow* canvas = nullptr;
        for (auto* window : ImGui::GetCurrentContext()->Windows)
            if (window->ParentWindow == viewer &&
                std::string(window->Name).find("ReferenceCanvas") != std::string::npos)
                canvas = window;
        check(canvas != nullptr, "reference viewer renders its interactive graph canvas");
        const auto center = canvas->InnerRect.GetCenter();
        const auto previousZoom = app.m_assetReferenceZoom;
        io.AddMousePosEvent(center.x, center.y);
        frame(draw);
        io.AddMouseWheelEvent(0, 1);
        frame(draw);
        check(app.m_assetReferenceZoom > previousZoom, "mouse wheel zooms the reference graph");
        const auto previousPan = app.m_assetReferencePan;
        io.AddMouseButtonEvent(2, true);
        frame(draw);
        io.AddMousePosEvent(center.x + 50, center.y + 20);
        frame(draw);
        io.AddMouseButtonEvent(2, false);
        frame(draw);
        check(app.m_assetReferencePan.x > previousPan.x + 10, "middle mouse drag pans the reference graph");
        const auto* textureNode = node(app.m_assetReferenceGraph, "c.ppm");
        const auto canvasCenter = canvas->InnerRect.GetCenter();
        const ImVec2 target{canvasCenter.x + app.m_assetReferencePan.x +
                                textureNode->column * app.scaled(280) * app.m_assetReferenceZoom,
                            canvasCenter.y + app.m_assetReferencePan.y +
                                textureNode->row * app.scaled(110) * app.m_assetReferenceZoom};
        io.AddMousePosEvent(target.x, target.y);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        check(app.m_selectedAssets.size() == 1 &&
                  app.m_selectedAssets.front() == toUtf8GenericPath((fixture.root / "c.ppm")),
              "clicking a graph node locates that asset in the browser");
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        frame(draw);
        check(app.m_assetReferenceRoot == toUtf8GenericPath((fixture.root / "c.ppm")) &&
                  app.m_assetReferenceGraph.nodes.front().path == toUtf8GenericPath((fixture.root / "c.ppm")),
              "double clicking a graph node rebuilds the viewer around that asset");
        app.m_showAssetReferences = false;
        app.selectAsset(b, false);
        app.m_assetFolder = fixture.root;
        frame(draw);
        frame(draw);
        table = ImGui::GetCurrentContext()->Tables.GetByKey(browserItems()->GetID("AssetList"));
        io.AddMousePosEvent(table->Columns[0].MinX + 24, table->OuterRect.Min.y + height * 1.5f);
        frame(draw);
        io.AddMouseButtonEvent(1, true);
        frame(draw);
        io.AddMouseButtonEvent(1, false);
        frame(draw);
        frame(draw);
        browserActivate(browserPopup(), "Details / Preview");
        check(browserPopup() && browserPopup()->PopupId == ImGui::FindWindowByName("Assets")->GetID("AssetDetails") &&
                  app.m_selectedAsset == a,
              "asset context details opens for the clicked asset using the correct popup scope");
    }

    void assetClicks()
    {
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/EditorP0Clicks/assets"));
        std::filesystem::create_directories(root);
        PlatformFile::writeAllText(root / "a.mesh", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        PlatformFile::writeAllText(root / "b.frag", "#version 330 core\nvoid main() {}\n");
        ResourceManager resources;
        resources.initialize(nullptr, root);
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.resourceManager = &resources;
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        const auto oldSettings = settings;
        settings.projectFile = root.parent_path() / "Clicks.tinyproject";
        settings.assetDirectory = "assets";
        app.m_assetFolder = root;
        app.m_assetViewMode = EditorApp::AssetViewMode::List;
        app.m_selectedAsset.clear();
        app.m_assetSearch.fill('\0');
        app.m_assetTypeFilters.clear();
        world.clear();
        const auto entity = world.createEntity("Keep scene object");
        app.m_selected = entity;
        app.m_selection = {entity};
        const auto draw = [&] { app.drawAssets(); };
        app.m_sceneDirty = false;
        auto& io = ImGui::GetIO();
        frame(draw);
        frame(draw);
        ImGuiWindow* browser = nullptr;
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
            if (candidate->ParentWindow == ImGui::FindWindowByName("Assets") &&
                std::string(candidate->Name).find("AssetBrowserItems") != std::string::npos)
                browser = candidate;
        check(browser != nullptr, "asset click fixture exposes the real browser");
        auto* table = ImGui::GetCurrentContext()->Tables.GetByKey(browser->GetID("AssetList"));
        check(table != nullptr, "asset click fixture exposes its rows");
        const float rowHeight = ImGui::GetFontSize() + ImGui::GetStyle().CellPadding.y * 2.0f;
        const float x = table->Columns[0].MinX + 24.0f;
        const float firstRow = table->OuterRect.Min.y + rowHeight * 1.5f;
        const auto click = [&](const float y)
        {
            io.AddMousePosEvent(x, y);
            frame(draw);
            io.AddMouseButtonEvent(0, true);
            frame(draw);
            io.AddMouseButtonEvent(0, false);
            frame(draw);
        };
        click(firstRow);
        check(std::filesystem::path(app.m_selectedAsset).filename() == "a.mesh", "single click selects an asset");
        click(firstRow);
        check(world.GetEntityManager().GetAliveEntities().size() == 1 && !app.m_sceneDirty && app.m_showAssetPreview,
              "double clicking a mesh previews it without changing the scene");
        for (int frameIndex = 0; frameIndex < 24; ++frameIndex)
            frame(draw);
        click(firstRow + rowHeight);
        check(std::filesystem::path(app.m_selectedAsset).filename() == "b.frag" && app.m_openShaderAsset.empty(),
              "single clicking a shader selects without opening its document");
        click(firstRow + rowHeight);
        check(std::filesystem::path(app.m_openShaderAsset).filename() == "b.frag",
              "double clicking a shader opens its document");
        app.m_openShaderAsset.clear();
        app.m_shaderSource.clear();
        app.m_savedShaderSource.clear();
        app.m_selectedAsset.clear();
        app.m_selectedAssets.clear();
        app.m_showAssetPreview = false;
        app.resetSceneInteraction();
        settings = oldSettings;
        context.resourceManager = nullptr;
        resources.shutdown();
    }

    void inspector()
    {
        world.clear();
        const auto entity = world.createEntity("Inspector");
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
        const auto draw = [&]() { app.drawInspector(); };
        frame(draw);
        frame(draw);
        const auto* window = ImGui::FindWindowByName("Inspector");
        const auto& style = ImGui::GetStyle();
        const auto fieldPoint = [&](const char* component, bool vector)
        {
            const auto id = ImHashStr("Properties", 0, ImHashStr(component, 0, window->ID));
            const auto* table = ImGui::GetCurrentContext()->Tables.GetByKey(id);
            if (!table)
                throw std::runtime_error("Inspector property table is missing");
            return ImVec2{table->Columns[1].WorkMinX + (vector ? ImGui::GetFrameHeight() + style.ItemSpacing.x : 0) + 8,
                          table->OuterRect.Min.y + style.CellPadding.y + ImGui::GetFrameHeight() * .5f};
        };
        auto point = fieldPoint("Transform", true);
        const float x = point.x, y = point.y;
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        check(ImGui::IsAnyItemActive(), "test pointer activates the real Inspector position field");
        check(app.m_undo.empty() && !app.m_sceneDirty, "clicking a numeric field without editing leaves scene clean");
        io.AddMousePosEvent(x + 35.0f, y);
        frame(draw);
        io.AddMousePosEvent(x + 65.0f, y);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        check(world.GetComponent<TransformComponent>(entity)->position.x != 0.0f && app.m_undo.size() == 1 &&
                  app.m_sceneDirty,
              "multi-frame property drag changes the value with exactly one Undo entry");
        const float editedPosition = world.GetComponent<TransformComponent>(entity)->position.x;
        app.undo();
        const auto restored = *world.GetEntityManager().GetAliveEntities().begin();
        check(world.GetComponent<TransformComponent>(restored)->position.x == 0.0f,
              "property Undo restores pre-drag value");
        app.redo();
        check(
            world.GetComponent<TransformComponent>(*world.GetEntityManager().GetAliveEntities().begin())->position.x ==
                editedPosition,
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
        frame(draw);
        point = fieldPoint("Name", false);
        io.AddMousePosEvent(point.x, point.y);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        io.AddInputCharactersUTF8("X");
        frame(draw);
        io.AddInputCharactersUTF8("Y");
        frame(draw);
        check(world.GetComponent<NameComponent>(renamed)->name != "Before" && app.m_undo.size() == 1,
              "text edits persist without Enter and coalesce into one Undo entry");
        io.AddMousePosEvent(300, 850);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        app.undo();
        check(world.GetComponent<NameComponent>(*world.GetEntityManager().GetAliveEntities().begin())->name == "Before",
              "text field Undo restores the complete original name");
    }

    void assetGridDrag()
    {
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/EditorP0Clicks/assets"));
        ResourceManager resources;
        resources.initialize(nullptr, root);
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.resourceManager = &resources;
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        const auto previous = settings;
        settings.projectFile = root.parent_path() / "Clicks.tinyproject";
        settings.assetDirectory = "assets";
        world.clear();
        const auto entity = world.createEntity("Drop target");
        world.RemoveComponent<NameComponent>(entity);
        world.RemoveComponent<ActiveComponent>(entity);
        world.RemoveComponent<TagComponent>(entity);
        world.RemoveComponent<LayerComponent>(entity);
        world.RemoveComponent<TransformComponent>(entity);
        world.AddComponent(entity, MeshComponent{});
        app.m_selected = entity;
        app.m_selection = {entity};
        app.m_selectedAsset.clear();
        app.m_assetFolder = root;
        app.m_assetViewMode = EditorApp::AssetViewMode::Grid;
        app.m_assetThumbnailSize = 64;
        app.m_undo.clear();
        app.m_sceneDirty = false;
        const auto draw = [&]
        {
            app.drawInspector();
            app.drawAssets();
        };
        frame(draw);
        frame(draw);
        ImGuiWindow* browser = nullptr;
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
            if (candidate->ParentWindow == ImGui::FindWindowByName("Assets") &&
                std::string(candidate->Name).find("AssetBrowserItems") != std::string::npos)
                browser = candidate;
        const ImVec2 source{browser->DC.CursorStartPos.x + 32, browser->DC.CursorStartPos.y + 32};
        const auto* inspector = ImGui::FindWindowByName("Inspector");
        const auto* table = ImGui::GetCurrentContext()->Tables.GetByKey(
            ImHashStr("Properties", 0, ImHashStr("Mesh Renderer", 0, inspector->ID)));
        const ImVec2 target{table->Columns[1].WorkMinX + 20,
                            table->OuterRect.Min.y + ImGui::GetStyle().CellPadding.y + ImGui::GetFrameHeight() * .5f};
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(source.x, source.y);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        frame(draw);
        check(app.m_selectedAsset.empty(), "grid mouse-down preserves the entity inspector until click release");
        io.AddMousePosEvent(source.x + 20, source.y + 20);
        frame(draw);
        frame(draw);
        io.AddMousePosEvent(target.x, target.y);
        frame(draw);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        frame(draw);
        check(world.GetComponent<MeshComponent>(entity)->meshPath == "assets/a.mesh" && app.m_undo.size() == 1,
              "real grid drag assigns a typed property without hiding its target");
        const auto asset = resources.getAssetRegistry().Find(toUtf8GenericPath((root / "a.mesh")));
        app.selectAsset(asset->path, false);
        check(app.placeAssetInScene(*asset) && app.m_selected != entity && app.m_selectedAsset.empty(),
              "explicit placement transfers the inspector to the new scene object");
        app.m_assetViewMode = EditorApp::AssetViewMode::List;
        app.resetSceneInteraction();
        app.m_selectedAsset.clear();
        context.resourceManager = nullptr;
        settings = previous;
        resources.shutdown();
    }

    void gizmo()
    {
        Renderer renderer;
        const_cast<EngineContext&>(app.m_engine.GetContext()).renderer = &renderer;
        auto& view = const_cast<FrameView&>(renderer.GetFrameView());
        view.cameraPosition = {0, 0, 5};
        view.view = lookAt(view.cameraPosition, {}, {0, 1, 0});
        view.projection = perspective(60, 1, .1f, 100);
        app.m_editorCameraPosition = view.cameraPosition;
        app.m_gizmoSpace = EditorApp::GizmoSpace::World;
        world.clear();
        const auto entity = world.createEntity("Gizmo");
        app.resetSceneInteraction();
        app.m_selected = entity;
        app.m_selection = {entity};
        app.m_undo.clear();
        app.m_sceneDirty = false;
        app.m_gizmoSnap = true;
        app.m_translateSnap = 1.0f;
        const auto draw = [&]()
        {
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({600, 600});
            ImGui::Begin("GizmoTest");
            app.drawGizmo(50, 50, 400, 400, true);
            ImGui::End();
        };
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(50, 50);
        frame(draw);
        io.AddMousePosEvent(300, 250);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        check(app.m_activeGizmoAxis == 0 && app.m_undo.empty() && !app.m_sceneDirty &&
                  world.GetComponent<TransformComponent>(entity)->position.x == 0,
              "clicking Gizmo ignores pointer arrival delta and leaves scene clean");
        io.AddMousePosEvent(302, 250);
        frame(draw);
        check(app.m_undo.empty() && !app.m_sceneDirty, "sub-threshold snapped Gizmo drag creates no undo record");
        io.AddMousePosEvent(390, 250);
        frame(draw);
        io.AddMousePosEvent(490, 250);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        check(app.m_undo.size() == 1 && world.GetComponent<TransformComponent>(entity)->position.x != 0,
              "snapped Gizmo drag commits one Undo for actual movement");
        app.undo();
        check(
            world.GetComponent<TransformComponent>(*world.GetEntityManager().GetAliveEntities().begin())->position.x ==
                0,
            "Gizmo Undo restores the position before the gesture");
        const_cast<EngineContext&>(app.m_engine.GetContext()).renderer = nullptr;
    }

    void inspectorBatchAndValidation()
    {
        world.clear();
        app.resetSceneInteraction();
        app.m_undo.clear();
        app.m_redo.clear();
        app.m_sceneDirty = false;
        const auto first = world.createEntity("First"), second = world.createEntity("Second");
        world.GetComponent<TransformComponent>(first)->position = {1, 2, 3};
        world.GetComponent<TransformComponent>(second)->position = {4, 5, 6};
        app.m_selected = first;
        app.m_selection = {first, second};
        const auto& transform = *ReflectionRegistry::Get().findComponent("Transform");
        const auto& position = transform.properties[0];
        const Vec3 edited{10, 2, 3};
        frame(
            [&]
            {
                ImGui::Begin("Batch host");
                app.applyInspectorProperty(transform, position, &edited, false, 0);
                ImGui::End();
            });
        check(world.GetComponent<TransformComponent>(first)->position.x == 10 &&
                  world.GetComponent<TransformComponent>(second)->position.x == 10 &&
                  world.GetComponent<TransformComponent>(second)->position.y == 5 &&
                  world.GetComponent<TransformComponent>(second)->position.z == 6 && app.m_undo.size() == 1,
              "batch axis edit preserves untouched mixed axes and records one transaction");
        app.undo();
        Entity restoredFirst = kInvalidEntity, restoredSecond = kInvalidEntity;
        for (const auto entity : world.GetEntityManager().GetAliveEntities())
        {
            const auto& name = world.GetComponent<NameComponent>(entity)->name;
            if (name == "First")
                restoredFirst = entity;
            if (name == "Second")
                restoredSecond = entity;
        }
        check(world.GetComponent<TransformComponent>(restoredFirst)->position.x == 1 &&
                  world.GetComponent<TransformComponent>(restoredSecond)->position.x == 4,
              "one Undo restores both objects after a batch edit");
        app.m_selected = restoredFirst;
        app.m_selection = {restoredFirst, restoredSecond};
        app.m_undo.clear();
        app.m_redo.clear();
        app.m_sceneDirty = false;
        app.resetInspectorComponent(transform);
        check(world.GetComponent<TransformComponent>(restoredFirst)->position.x == 0 &&
                  world.GetComponent<TransformComponent>(restoredSecond)->position.y == 0 &&
                  world.GetComponent<TransformComponent>(restoredSecond)->scale.x == 1 && app.m_undo.size() == 1,
              "component reset applies defaults to the selection as one transaction");
        app.m_undo.clear();
        app.m_sceneDirty = false;
        app.resetInspectorComponent(transform);
        check(app.m_undo.empty() && !app.m_sceneDirty, "resetting default properties does not dirty the scene");

        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/EditorP0Clicks/assets"));
        ResourceManager resources;
        resources.initialize(nullptr, root);
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.resourceManager = &resources;
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        const auto oldSettings = settings;
        settings.projectFile = root.parent_path() / "Clicks.tinyproject";
        settings.assetDirectory = "assets";
        world.AddComponent(restoredFirst, MeshComponent{"assets/a.mesh", ""});
        world.AddComponent(restoredSecond, MeshComponent{"assets/a.mesh", ""});
        const auto& mesh = *ReflectionRegistry::Get().findComponent("Mesh Renderer");
        const auto& meshPath = mesh.properties[0];
        app.m_feedback.clear();
        const auto fieldId = 321u;
        check(!app.applyInspectorAsset(mesh, meshPath, toUtf8GenericPath((root / "b.frag")), fieldId) &&
                  world.GetComponent<MeshComponent>(restoredFirst)->meshPath == "assets/a.mesh" && app.m_undo.empty() &&
                  app.m_inspectorErrors.count(fieldId) && !app.m_feedback.empty() &&
                  app.m_feedback.back().kind == EditorApp::FeedbackKind::Error,
              "wrong asset type retains the old reference and shows inline and global errors");
        check(!app.applyInspectorAsset(mesh, meshPath, "assets/missing.mesh", fieldId) && !app.m_sceneDirty,
              "missing asset path cannot silently overwrite a valid reference");
        check(app.applyInspectorAsset(mesh, meshPath, toUtf8GenericPath((root / "a.mesh")), fieldId) &&
                  !app.m_inspectorErrors.count(fieldId) &&
                  world.GetComponent<MeshComponent>(restoredFirst)->meshPath == "assets/a.mesh" && app.m_undo.empty(),
              "valid asset correction clears the field error and preserves a portable reference");
        check(app.applyInspectorAsset(mesh, meshPath, "", fieldId) && app.m_undo.size() == 1 &&
                  world.GetComponent<MeshComponent>(restoredFirst)->meshPath.empty() &&
                  world.GetComponent<MeshComponent>(restoredSecond)->meshPath.empty(),
              "clearing a resource reference edits the selection in one undoable operation");
        frame([&] { app.drawFeedback(); });
        check(!app.m_feedback.empty(), "operation errors are visible without opening the console");
        const auto oldDelta = ImGui::GetIO().DeltaTime;
        ImGui::GetIO().DeltaTime = 13;
        frame([&] { app.drawFeedback(); });
        ImGui::GetIO().DeltaTime = oldDelta;
        check(app.m_feedback.empty(), "transient feedback expires without blocking the editor");
        context.resourceManager = nullptr;
        settings = oldSettings;
        resources.shutdown();
        app.resetSceneInteraction();
    }

    void inspectorAxisDefaults()
    {
        world.clear();
        app.resetSceneInteraction();
        app.m_selectedAsset.clear();
        const auto entity = world.createEntity("Collider reset");
        world.RemoveComponent<NameComponent>(entity);
        world.RemoveComponent<ActiveComponent>(entity);
        world.RemoveComponent<TagComponent>(entity);
        world.RemoveComponent<LayerComponent>(entity);
        world.RemoveComponent<TransformComponent>(entity);
        ColliderComponent collider;
        collider.halfExtents = {2, 3, 4};
        world.AddComponent(entity, collider);
        app.m_selected = entity;
        app.m_selection = {entity};
        app.m_undo.clear();
        const auto draw = [&] { app.drawInspector(); };
        frame(draw);
        frame(draw);
        const auto* window = ImGui::FindWindowByName("Inspector");
        const auto tableId = ImHashStr("Properties", 0, ImHashStr("Collider", 0, window->ID));
        const auto* table = ImGui::GetCurrentContext()->Tables.GetByKey(tableId);
        check(table != nullptr, "collider reset fixture exposes the actual property controls");
        const float rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2;
        const float x = table->Columns[1].WorkMinX + ImGui::GetFrameHeight() * .5f;
        const float y = table->OuterRect.Min.y + rowHeight * 1.5f;
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        check(world.GetComponent<ColliderComponent>(entity)->halfExtents.x == .5f &&
                  world.GetComponent<ColliderComponent>(entity)->halfExtents.y == 3 && app.m_undo.size() == 1,
              "axis reset uses the component default and preserves the other axes");
        app.resetSceneInteraction();
    }

    void animationAssetRouting()
    {
        EditorApp isolatedApp;
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/AnimationRouting"));
        std::filesystem::create_directories(root);
        SkeletalClip clip;
        clip.name = "Route";
        clip.duration = 1.0f;
        clip.keyframes = {{0.0f, {BonePose{}}}, {1.0f, {BonePose{}}}};
        const auto first = toUtf8GenericPath(root / "First.skanim");
        const auto second = toUtf8GenericPath(root / "Second.skanim");
        check(saveSkeletalClipAsset(clip, fromUtf8Path(first)) &&
                  saveSkeletalClipAsset(clip, fromUtf8Path(second)), "prepare animation routing assets");
        AssetInfo asset;
        asset.path = first;
        asset.extension = ".skanim";
        asset.type = "Animation";
        isolatedApp.openAsset(asset);
        check(isolatedApp.m_animationAssetEditor.path() == first && isolatedApp.m_animationAssetEditor.isVisible(),
              "opening a skeletal clip routes to the animation document");
        isolatedApp.m_animationAssetEditor.setName("Unsaved animation");
        asset.path = second;
        isolatedApp.openAsset(asset);
        check(isolatedApp.m_animationAssetEditor.path() == first && isolatedApp.m_animationAssetEditor.isDirty(),
              "opening another clip retains unsaved animation edits");
        isolatedApp.requestEditorExit();
        check(isolatedApp.m_requestAnimationExitPopup,
              "application exit requests saving an unsaved animation document");
        isolatedApp.m_exitInProgress = false;
        isolatedApp.m_requestAnimationExitPopup = false;
        check(isolatedApp.m_animationAssetEditor.revert(), "animation draft can be reverted before switching");
        isolatedApp.openAsset(asset);
        check(isolatedApp.m_animationAssetEditor.path() == second,
              "a clean animation document can switch to another clip");
    }

    void typedSkeletonAndFailedOpen()
    {
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/EditorP0Typed/assets"));
        PlatformFile::writeAllText(root / "rig.skeleton", "skeleton fixture");
        PlatformFile::writeAllText(root / "locomotion.blendtree", "blend tree fixture");
        ResourceManager resources;
        resources.initialize(nullptr, root);
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.resourceManager = &resources;
        auto& settings = const_cast<ProjectSettings&>(app.m_engine.getProjectSettings());
        const auto previous = settings;
        settings.projectFile = root.parent_path() / "Typed.tinyproject";
        settings.assetDirectory = "assets";
        world.clear();
        const auto entity = world.createEntity("Animated");
        world.AddComponent(entity, AnimationComponent{});
        app.m_selected = entity;
        app.m_selection = {entity};
        app.m_selectedAsset.clear();
        const auto& component = *ReflectionRegistry::Get().findComponent("Animation");
        const auto property = std::find_if(component.properties.begin(), component.properties.end(),
                                           [](const auto& p) { return p.name == "Skeleton"; });
        check(app.applyInspectorAsset(component, *property, toUtf8GenericPath((root / "rig.skeleton")), 963) &&
                  world.GetComponent<AnimationComponent>(entity)->skeletonPath == "assets/rig.skeleton",
              "typed skeleton fields accept registered skeleton assets");
        const auto clip = std::find_if(component.properties.begin(), component.properties.end(),
                                       [](const auto& p) { return p.name == "Clip"; });
        check(app.applyInspectorAsset(component, *clip, toUtf8GenericPath((root / "locomotion.blendtree")), 964) &&
                  world.GetComponent<AnimationComponent>(entity)->clipPath == "assets/locomotion.blendtree",
              "animation clip picker accepts blend trees supported by the runtime");
        context.resourceManager = nullptr;
        settings = previous;
        resources.shutdown();
        app.m_messages.clear();
        app.m_feedback.clear();
        app.m_editingAsset = toUtf8GenericPath((root / "missing.vfx"));
        app.m_openEffectAsset.clear();
        frame([&] { app.drawEffectEditor(); });
        frame([&] { app.drawEffectEditor(); });
        check(app.m_messages.size() == 1 && app.m_feedback.size() == 1,
              "a failed asset open reports once instead of retrying every frame");
        app.resetSceneInteraction();
    }

    void packagingWriteLock()
    {
        world.clear();
        const auto entity = world.createEntity("Packaging snapshot");
        app.m_selected = entity;
        app.m_selection = {entity};
        app.m_selectedAsset.clear();
        const auto path = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/EditorP0Packaging/locked.scene"));
        const auto packageInput = app.m_engine.GetSceneManager().createSnapshot();
        PlatformFile::writeAllText(path, packageInput);
        world.createEntity("Unsaved addition");
        auto& document = app.m_sceneDocuments[app.m_activeSceneDocument];
        const auto previousPath = document.path;
        document.path = path;
        std::promise<PackageResult> completion;
        app.m_packageFuture = completion.get_future();
        app.m_sceneCommandsFocused = true;
        Key(ImGuiKey_Delete);
        check(world.isAlive(entity), "packaging blocks scene keyboard writes until staging completes");
        check(!app.saveCurrentScene() && PlatformFile::readAllText(path) == packageInput,
              "packaging cannot overwrite source scenes while the worker reads them");
        PackageResult result;
        result.success = true;
        result.outputDirectory = path.parent_path();
        completion.set_value(result);
        frame([&] { app.drawPackagingStatus(); });
        check(!app.m_packageFuture.valid(), "completed packaging releases the edit lock through the UI status poll");
        document.path = previousPath;
        app.resetSceneInteraction();
    }

    void hierarchyCameraFocus()
    {
        world.clear();
        app.resetSceneInteraction();
        app.m_selected = kInvalidEntity;
        app.m_selection.clear();
        app.m_hierarchySearch[0] = '\0';
        app.m_editorCameraPosition = {0.0f, 2.5f, 7.0f};
        app.m_editorCameraRotation = {0.0f, 0.0f, 0.0f};
        const Entity target = world.createEntity("Focus target");
        world.GetComponent<TransformComponent>(target)->position = {14.0f, 2.0f, -6.0f};
        ColliderComponent collider;
        collider.shape = ColliderShape::Box;
        collider.halfExtents = {2.0f, 1.0f, 3.0f};
        world.AddComponent(target, collider);
        ImVec2 rowMinimum{}, rowMaximum{};
        const auto draw = [&]()
        {
            app.drawHierarchy();
            const auto* panel = ImGui::FindWindowByName("Hierarchy");
            rowMinimum = {panel->WorkRect.Min.x, panel->DC.CursorPosPrevLine.y};
            rowMaximum = {panel->WorkRect.Max.x, rowMinimum.y + panel->DC.PrevLineSize.y};
        };
        auto& io = ImGui::GetIO();
        frame(draw);
        frame(draw);
        check(rowMaximum.y > rowMinimum.y, "hierarchy exposes a clickable entity row");
        io.AddMousePosEvent(rowMinimum.x + 38.0f, (rowMinimum.y + rowMaximum.y) * .5f);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        check(app.m_selected == target && app.m_editorCameraPosition.x == 0.0f,
              "single click selects without moving the editor camera");
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        check(app.m_selected == target && std::abs(app.m_editorCameraPosition.x - 14.0f) < 0.01f &&
                  std::abs(app.m_editorCameraPosition.y - 2.0f) < 0.01f && app.m_editorCameraPosition.z > -1.0f &&
                  app.m_editorCameraRotation.x == 0.0f && app.m_editorCameraRotation.y == 0.0f,
              "double click frames the entity while preserving camera direction");

        const Entity parent = world.createEntity("Parent");
        world.GetComponent<TransformComponent>(parent)->position = {100.0f, 4.0f, 0.0f};
        const Entity child = world.createEntity("Child");
        world.GetComponent<TransformComponent>(child)->position = {3.0f, 0.0f, 0.0f};
        check(world.setParent(child, parent), "focus fixture parents a child entity");
        app.focusSceneEntity(child);
        check(std::abs(app.m_editorCameraPosition.x - 103.0f) < 0.01f &&
                  std::abs(app.m_editorCameraPosition.y - 4.0f) < 0.01f,
              "focus uses world position for a child without geometry");
        app.m_editorCameraPosition = {0.0f, 0.0f, 0.0f};
        app.focusSceneEntity(kInvalidEntity);
        check(length(app.m_editorCameraPosition) == 0.0f, "focus ignores an invalid entity");
        world.clear();
        app.resetSceneInteraction();
    }

    void combinedGizmo()
    {
        Renderer renderer;
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.renderer = &renderer;
        auto& view = const_cast<FrameView&>(renderer.GetFrameView());
        view.cameraPosition = {4, 3, 6};
        view.view = lookAt(view.cameraPosition, {}, {0, 1, 0});
        view.projection = perspective(60, 1, .1f, 100);
        app.m_editorCameraPosition = view.cameraPosition;
        app.m_gizmoSpace = EditorApp::GizmoSpace::World;
        app.m_pivotMode = EditorApp::PivotMode::Individual;
        app.m_gizmoSnap = false;
        const float lengthValue = length(view.cameraPosition) * .13f;
        const auto project = [&](Vec3 p)
        {
            const auto matrix = view.projection * view.view;
            const float w = matrix.at(3, 0) * p.x + matrix.at(3, 1) * p.y + matrix.at(3, 2) * p.z + matrix.at(3, 3);
            return ImVec2{50 + 400 * (1 + (matrix.at(0, 0) * p.x + matrix.at(0, 1) * p.y + matrix.at(0, 2) * p.z +
                                           matrix.at(0, 3)) /
                                              w),
                          50 + 400 * (1 - (matrix.at(1, 0) * p.x + matrix.at(1, 1) * p.y + matrix.at(1, 2) * p.z +
                                           matrix.at(1, 3)) /
                                              w)};
        };
        const auto draw = [&]
        {
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({1000, 850});
            ImGui::Begin("CombinedGizmo");
            app.drawGizmo(50, 50, 800, 800, true);
            ImGui::End();
        };
        auto& io = ImGui::GetIO();
        const auto select = [&]
        {
            world.clear();
            app.resetSceneInteraction();
            app.m_undo.clear();
            app.m_redo.clear();
            app.m_sceneDirty = false;
            const auto e = world.createEntity("Combined");
            app.m_selected = e;
            app.m_selection = {e};
            frame(draw);
            frame(draw);
            return e;
        };
        const auto drag = [&](ImVec2 from, ImVec2 to, EditorApp::GizmoMode operation, int axis)
        {
            io.AddMousePosEvent(from.x, from.y);
            frame(draw);
            io.AddMouseButtonEvent(0, true);
            frame(draw);
            check(app.m_activeGizmoAxis == axis && app.m_gizmoDragMode == operation,
                  "combined handle chooses its operation and axis without a mode switch");
            io.AddMousePosEvent(to.x, to.y);
            frame(draw);
            io.AddMouseButtonEvent(0, false);
            frame(draw);
        };
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto e = select();
            const Vec3 direction = axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
            const auto from = project(direction * (lengthValue * .52f)), center = project({});
            drag(from, {from.x + (from.x - center.x) * .25f, from.y + (from.y - center.y) * .25f},
                 EditorApp::GizmoMode::Scale, axis);
            const auto* t = world.GetComponent<TransformComponent>(e);
            // Projected axes still admit ImGui's integer mouse-coordinate rounding.
            check(std::abs((&t->scale.x)[axis] - 1.25f) < .03f && length(t->position) < .001f &&
                      length(t->rotation) < .001f && app.m_undo.size() == 1,
                  "inner scale cube changes only scale and records one undo");
            app.undo();
            check(std::abs(world.GetComponent<TransformComponent>(*world.GetEntityManager().GetAliveEntities().begin())
                               ->scale.x -
                           1) < .001f,
                  "undo restores a combined scale gesture");
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto e = select();
            const Vec3 direction = axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
            const auto from = project(direction * lengthValue), center = project({});
            drag(from, {from.x + (from.x - center.x) * .25f, from.y + (from.y - center.y) * .25f},
                 EditorApp::GizmoMode::Translate, axis);
            const auto* t = world.GetComponent<TransformComponent>(e);
            check(dot(t->position, direction) > 0 && length(t->scale - Vec3{1, 1, 1}) < .001f &&
                      length(t->rotation) < .001f && app.m_undo.size() == 1,
                  "outer arrow changes only translation and records one undo");
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto e = select();
            const Vec3 u = axis == 0 ? Vec3{0, 1, 0} : axis == 1 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
            const Vec3 v = axis == 0 ? Vec3{0, 0, 1} : axis == 1 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            const float first = degreesToRadians(140), last = degreesToRadians(175);
            drag(project((u * std::cos(first) + v * std::sin(first)) * (lengthValue * .8f)),
                 project((u * std::cos(last) + v * std::sin(last)) * (lengthValue * .8f)), EditorApp::GizmoMode::Rotate,
                 axis);
            const auto* t = world.GetComponent<TransformComponent>(e);
            check(std::abs((&t->rotation.x)[axis] - 35) < 2 && length(t->position) < .001f &&
                      length(t->scale - Vec3{1, 1, 1}) < .001f && app.m_undo.size() == 1,
                  "rotation ring remains available after scaling and translation without a mode switch");
        }
        const auto e = select();
        world.GetComponent<TransformComponent>(e)->rotation.z = 90;
        const auto localX = project({0, lengthValue * .52f, 0}), center = project({});
        drag(localX, {localX.x + (localX.x - center.x) * .25f, localX.y + (localX.y - center.y) * .25f},
             EditorApp::GizmoMode::Scale, 0);
        const auto* scaled = world.GetComponent<TransformComponent>(e);
        check(std::abs(scaled->scale.x - 1.25f) < .03f && std::abs(scaled->scale.y - 1) < .001f &&
                  length(scaled->position) < .001f,
              "local scale cube wins over an overlapping world translation shaft");
        app.resetSceneInteraction();
        world.clear();
        context.renderer = nullptr;
    }

    void viewportRegression()
    {
        Renderer renderer;
        const_cast<EngineContext&>(app.m_engine.GetContext()).renderer = &renderer;
        auto& view = const_cast<FrameView&>(renderer.GetFrameView());
        view.cameraPosition = {4, 3, 6};
        view.view = lookAt(view.cameraPosition, {}, {0, 1, 0});
        view.projection = perspective(60, 1, 0.1f, 100);
        app.m_editorCameraPosition = view.cameraPosition;
        app.m_gizmoSnap = false;
        app.m_gizmoSpace = EditorApp::GizmoSpace::World;
        app.m_pivotMode = EditorApp::PivotMode::Individual;
        auto project = [&](Vec3 point)
        {
            const auto m = view.projection * view.view;
            const float w = m.at(3, 0) * point.x + m.at(3, 1) * point.y + m.at(3, 2) * point.z + m.at(3, 3);
            return ImVec2{
                50 + 400 * (1 + (m.at(0, 0) * point.x + m.at(0, 1) * point.y + m.at(0, 2) * point.z + m.at(0, 3)) / w),
                50 + 400 * (1 - (m.at(1, 0) * point.x + m.at(1, 1) * point.y + m.at(1, 2) * point.z + m.at(1, 3)) / w)};
        };
        auto draw = [&]()
        {
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({1000, 850});
            ImGui::Begin("GizmoGeometry");
            app.drawGizmo(50, 50, 800, 800, true);
            ImGui::End();
        };
        auto& io = ImGui::GetIO();
        const float lengthValue = length(view.cameraPosition) * 0.13f;
        auto drag = [&](ImVec2 from, ImVec2 to, int axis)
        {
            io.AddMousePosEvent(from.x, from.y);
            frame(draw);
            io.AddMouseButtonEvent(0, true);
            frame(draw);
            check(app.m_activeGizmoAxis == axis, "projected handle selects the intended axis");
            io.AddMousePosEvent(to.x, to.y);
            frame(draw);
            io.AddMouseButtonEvent(0, false);
            frame(draw);
        };
        for (int axis = 0; axis < 3; ++axis)
        {
            world.clear();
            const auto entity = world.createEntity("Axis");
            app.resetSceneInteraction();
            app.m_selected = entity;
            app.m_selection = {entity};
            app.m_undo.clear();
            const Vec3 direction = axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
            const auto from = project(direction * lengthValue), end = project(direction * lengthValue),
                       origin = project({});
            drag(from, {from.x + (end.x - origin.x) * 0.3f, from.y + (end.y - origin.y) * 0.3f}, axis);
            check(dot(world.GetComponent<TransformComponent>(entity)->position, direction) > 0 &&
                      app.m_undo.size() == 1,
                  "dragging along each visible arrow moves positively on that axis with one Undo");
        }
        world.clear();
        const auto scaled = world.createEntity("Scale");
        app.resetSceneInteraction();
        app.m_selected = scaled;
        app.m_selection = {scaled};
        const auto blue = project({0, 0, lengthValue * .52f}), origin = project({});
        drag(blue, {blue.x + (blue.x - origin.x) * 0.25f, blue.y + (blue.y - origin.y) * 0.25f}, 2);
        check(std::abs(world.GetComponent<TransformComponent>(scaled)->scale.z - 1.25f) < 0.02f,
              "blue scale handle grows in its displayed direction by the drag ratio");

        for (int axis = 0; axis < 3; ++axis)
        {
            world.clear();
            const auto rotated = world.createEntity("Rotate");
            app.resetSceneInteraction();
            app.m_selected = rotated;
            app.m_selection = {rotated};
            const Vec3 u = axis == 0 ? Vec3{0, 1, 0} : axis == 1 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
            const Vec3 v = axis == 0 ? Vec3{0, 0, 1} : axis == 1 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            const float firstAngle = degreesToRadians(140), lastAngle = degreesToRadians(175);
            drag(project((u * std::cos(firstAngle) + v * std::sin(firstAngle)) * (lengthValue * .8f)),
                 project((u * std::cos(lastAngle) + v * std::sin(lastAngle)) * (lengthValue * .8f)), axis);
            const auto rotation = world.GetComponent<TransformComponent>(rotated)->rotation;
            const float angle = axis == 0 ? rotation.x : axis == 1 ? rotation.y : rotation.z;
            // ImGui 会把注入的指针位置取整到像素，角度允许相应的微小误差。
            check(std::abs(angle - 35) < 2.0f, "each rotation ring follows a 35 degree projected gesture");
        }

        world.clear();
        const auto parent = world.createEntity("Parent"), child = world.createEntity("Child");
        world.setParent(child, parent);
        world.GetComponent<TransformComponent>(parent)->rotation.y = 90;
        world.GetComponent<TransformComponent>(parent)->scale = {2, 2, 2};
        app.resetSceneInteraction();
        app.m_selected = child;
        app.m_selection = {child};
        auto red = project({lengthValue, 0, 0}), redEnd = project({lengthValue, 0, 0});
        drag(red, {red.x + (redEnd.x - origin.x) * 0.3f, red.y + (redEnd.y - origin.y) * 0.3f}, 0);
        const auto childWorld = world.getWorldMatrix(child);
        check(childWorld.at(0, 3) > 0 && std::abs(childWorld.at(2, 3)) < 0.001f,
              "world translation under a rotated scaled parent stays on the displayed world axis");
        world.GetComponent<TransformComponent>(child)->position = {};
        app.m_selected = parent;
        app.m_selection = {parent, child};
        drag(red, {red.x + (redEnd.x - origin.x) * 0.3f, red.y + (redEnd.y - origin.y) * 0.3f}, 0);
        check(length(world.GetComponent<TransformComponent>(child)->position) == 0 &&
                  std::abs(world.getWorldMatrix(child).at(0, 3) - world.getWorldMatrix(parent).at(0, 3)) < 0.001f,
              "selected parent and child receive world translation only once");
        const_cast<EngineContext&>(app.m_engine.GetContext()).renderer = nullptr;

        app.resetSceneInteraction();
        app.m_editorCameraPosition = {0, 0, 5};
        app.m_editorCameraRotation = {};
        const auto camera = [&]() { app.updateSceneCamera(true, true); };
        io.AddKeyEvent(ImGuiKey_W, true);
        frame(camera);
        io.AddKeyEvent(ImGuiKey_W, false);
        frame(camera);
        check(app.m_editorCameraPosition.z < 5, "focused WASD moves the scene camera without holding RMB");
        const Vec3 before = app.m_editorCameraPosition;
        io.AddKeyEvent(ImGuiKey_A, true);
        frame([&]() { app.updateSceneCamera(true, false); });
        io.AddKeyEvent(ImGuiKey_A, false);
        frame(camera);
        check(length(app.m_editorCameraPosition - before) == 0, "hover alone cannot steal WASD from another panel");
        io.AddKeyEvent(ImGuiKey_W, true);
        frame(
            [&]()
            {
                io.WantTextInput = true;
                app.updateSceneCamera(true, true);
            });
        io.AddKeyEvent(ImGuiKey_W, false);
        frame(camera);
        check(length(app.m_editorCameraPosition - before) == 0, "text input prevents camera movement");
        const auto rotationBefore = app.m_editorCameraRotation;
        io.AddMousePosEvent(100, 100);
        frame(camera);
        io.AddMousePosEvent(200, 200);
        io.AddMouseButtonEvent(1, true);
        frame(camera);
        check(length(app.m_editorCameraRotation - rotationBefore) == 0,
              "starting RMB look ignores pointer arrival delta");
        io.AddMouseButtonEvent(1, false);
        frame(camera);

        check(renderer.initialize(std::make_unique<NullRenderDevice>()), "stable projection renderer initializes");
        renderer.setCameraOverride(true, {0, 0, 5}, {});
        renderer.renderFrame(world);
        const auto projection = renderer.GetFrameView().projection;
        renderer.renderFrame(world);
        check(projection.data == renderer.GetFrameView().projection.data &&
                  renderer.GetFrameView().temporalJitter.x == 0 && renderer.GetFrameView().temporalJitter.y == 0,
              "stationary editor projection is identical across frames");
        renderer.setCameraOverride(false);
        renderer.renderFrame(world);
        check(renderer.GetFrameView().temporalJitter.x == 0 && renderer.GetFrameView().temporalJitter.y == 0,
              "render device without temporal AA receives no projection jitter");
        renderer.shutdown();
    }

    void sceneOpenRegression()
    {
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/SceneOpenRegression")) /
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root / "assets");
        const auto oldSettings = app.m_engine.getProjectSettings();
        auto& settings = app.m_engine.getProjectSettings();
        settings.projectFile = root / "Test.tinyproject";
        settings.assetDirectory = "assets";
        ResourceManager resources;
        resources.initialize(nullptr, root / "assets");
        const_cast<EngineContext&>(app.m_engine.GetContext()).resourceManager = &resources;
        app.newSceneDocument();
        world.createEntity("Saved scene object");
        const auto saved = root / "assets/Level.scene";
        check(app.saveSceneDocumentTo(app.m_activeSceneDocument, saved),
              "scene is saved through the editor save transaction");
        const auto snapshot = app.m_engine.GetSceneManager().createSnapshot();
        app.newSceneDocument();
        world.createEntity("Keep unsaved work");
        app.m_sceneDirty = true;
        const auto work = app.m_engine.GetSceneManager().createSnapshot();
        // 先移除已保存的标签，确保打开操作确实从磁盘读取，而非只切换标签。
        for (std::size_t index = 0; index < app.m_sceneDocuments.size(); ++index)
            if (app.m_sceneDocuments[index].path == saved)
            {
                app.closeSceneDocument(index);
                break;
            }
        const auto unsavedIndex = app.m_activeSceneDocument;
        app.m_shaderFocused = false;
        Key(ImGuiKey_O, true);
        check(app.m_requestSceneOpenPopup && std::find(app.m_sceneOpenFiles.begin(), app.m_sceneOpenFiles.end(),
                                                       saved) != app.m_sceneOpenFiles.end(),
              "Ctrl+O opens a refreshed list containing the newly saved scene");
        const auto draw = [&]() { app.drawSceneOpenDialog(); };
        frame(draw);
        frame(draw);
        auto* dialog = ImGui::FindWindowByName("Open Scene###SceneOpenDialog");
        check(dialog && dialog->Active && !dialog->Hidden, "Open Scene modal is visible");
        const auto path = toUtf8GenericPath(saved);
        std::copy(path.begin(), path.end(), app.m_sceneOpenPath.begin());
        app.m_sceneOpenPath[path.size()] = '\0';
        ImGui::ActivateItemByID(dialog->GetID("Open"));
        frame(draw);
        check(app.m_engine.GetSceneManager().createSnapshot() == snapshot,
              "Open button loads the saved scene contents from disk");
        check(app.switchSceneDocument(unsavedIndex) && app.m_sceneDirty &&
                  app.m_engine.GetSceneManager().createSnapshot() == work,
              "opening a scene preserves the previous tab's unsaved changes");
        for (float width : {800.0f, 1280.0f, 1920.0f})
        {
            const auto layout = app.getWorkspaceLayout(width, 720);
            check(width - layout.left - layout.right - 6 > 300 && layout.upper > 300 && 720 - layout.upper > 180,
                  "workspace leaves usable viewport and asset space across window widths");
        }
        auto& io = ImGui::GetIO();
        const auto splitters = [&]() { app.drawWorkspaceSplitters(); };
        frame(splitters);
        frame(splitters);
        const auto* grip = ImGui::FindWindowByName("##LeftSplit");
        const float originalRatio = app.m_leftPanelRatio;
        io.AddMousePosEvent(grip->Pos.x + 3, grip->Pos.y + 120);
        frame(splitters);
        io.AddMouseButtonEvent(0, true);
        frame(splitters);
        io.AddMousePosEvent(grip->Pos.x + 63, grip->Pos.y + 120);
        frame(splitters);
        io.AddMouseButtonEvent(0, false);
        frame(splitters);
        check(app.m_leftPanelRatio > originalRatio, "dragging the real splitter resizes the hierarchy panel");
        const float storedRatio = app.m_leftPanelRatio;
        app.m_leftPanelRatio = originalRatio;
        app.loadEditorSettings();
        check(std::abs(app.m_leftPanelRatio - storedRatio) < 0.0001f,
              "workspace ratios survive saving and reloading settings");
        app.m_leftPanelRatio = originalRatio;
        Key(ImGuiKey_F11);
        check(app.m_sceneMaximized, "F11 expands the scene viewport");
        Key(ImGuiKey_F11);
        check(!app.m_sceneMaximized, "F11 restores the complete workspace");
        const auto selected = world.createEntity("Keep selected object on failed open");
        app.m_selected = selected;
        app.m_selection = {selected};
        const auto beforeFailedOpen = app.m_engine.GetSceneManager().createSnapshot();
        check(!app.openSceneDocument(root / "missing.scene") && world.isAlive(selected) && app.m_selected == selected &&
                  app.m_engine.GetSceneManager().createSnapshot() == beforeFailedOpen,
              "failed scene open preserves the live world and selected entity handles");
        check(PlatformFile::writeAllText(app.getAutosavePath(),
                                         "TINYSCENE 11\nentity 1 0 \"broken\"\ntransform invalid\n"),
              "malformed recovery fixture is written");
        check(!app.restoreAutosave() && world.isAlive(selected) && app.m_selected == selected &&
                  app.m_engine.GetSceneManager().createSnapshot() == beforeFailedOpen,
              "failed recovery preserves the live world and selected entity handles");
        app.m_sceneDirty = true;
        check(app.autosave(), "dirty scene can create a recovery file");
        const auto recovery = PlatformFile::readAllText(app.getAutosavePath());
        app.newSceneDocument();
        check(app.saveSceneDocumentTo(app.m_activeSceneDocument, root / "assets/Other.scene") &&
                  PlatformFile::readAllText(app.getAutosavePath()) == recovery,
              "saving another tab preserves recovery while unsaved scenes remain open");
        settings = oldSettings;
        const_cast<EngineContext&>(app.m_engine.GetContext()).resourceManager = nullptr;
    }

    void compactWorkspace()
    {
        auto& io = ImGui::GetIO();
        const auto oldSize = io.DisplaySize;
        io.DisplaySize = {800, 600};
        const auto layout = app.getWorkspaceLayout(800, 600);
        check(layout.left >= 160 && layout.right >= 240 && 800 - layout.left - layout.right - 6 >= 300,
              "compact workspace reserves readable sidebars and a usable scene viewport");
        world.clear();
        const auto entity = world.createEntity("Compact inspector");
        world.AddComponent<CameraComponent>(entity, {});
        app.m_selected = entity;
        app.m_selection = {entity};
        frame([&]() { app.drawInspector(); });
        frame([&]() { app.drawInspector(); });
        const auto* inspector = ImGui::FindWindowByName("Inspector");
        check(inspector->ContentSize.x <= inspector->InnerRect.GetWidth() + 1,
              "compact inspector keeps property labels and controls inside the panel");

        app.m_messages.clear();
        for (int index = 0; index < 100; ++index)
            app.m_messages.push_back("Log entry " + std::to_string(index));
        frame([&]() { app.drawConsole(); });
        frame([&]() { app.drawConsole(); });
        auto* console = ImGui::FindWindowByName("Console / Stats");
        check(console->ScrollMax.y == 0, "long console output cannot scroll the command input out of view");
        ImGuiWindow* log = nullptr;
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
            if (candidate->ParentWindow == console && candidate->ScrollMax.y > 0)
                log = candidate;
        check(log != nullptr, "console output has its own scrollable history");
        ImGui::SetScrollY(log, 0);
        frame([&]() { app.drawConsole(); });
        app.m_messages.emplace_back("New message while reading old output");
        frame([&]() { app.drawConsole(); });
        frame([&]() { app.drawConsole(); });
        check(log->Scroll.y == 0, "new output preserves the position when reading earlier console messages");
        ImGui::SetScrollY(log, log->ScrollMax.y);
        frame([&]() { app.drawConsole(); });
        app.m_messages.emplace_back("New message while following output");
        frame([&]() { app.drawConsole(); });
        frame([&]() { app.drawConsole(); });
        check(log->Scroll.y >= log->ScrollMax.y - 1, "console follows new messages only when already at the bottom");
        app.m_messages.clear();
        io.DisplaySize = oldSize;
        app.resetSceneInteraction();
    }

    void shaderDocuments()
    {
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/EditorShaderRegression")) /
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root);
        const auto first = toUtf8GenericPath((root / "first.frag"));
        const auto second = toUtf8GenericPath((root / "second.vert"));
        const std::string original = "// original shader\nvoid main() {}\n";
        const std::string other = "// second shader\nvoid main() {}\n";
        check(PlatformFile::writeAllText(first, original) && PlatformFile::writeAllText(second, other),
              "shader fixtures created");
        app.m_exitInProgress = false;
        app.m_requestExitPopup = false;
        app.requestShaderAction(EditorApp::ShaderAction::Open, first);
        check(app.m_openShaderAsset == first && !app.isShaderDirty(),
              "opening Shader reads a clean independent document");
        const auto draw = [&]() { app.drawShaderEditor(); };
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(1100, 800);
        frame(draw);
        frame(draw);
        const auto* window = ImGui::FindWindowByName("Shader Source Editor");
        const auto start = window->DC.CursorStartPos;
        io.AddMousePosEvent(start.x + 30, start.y + ImGui::GetTextLineHeightWithSpacing() + 12);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        check(ImGui::IsAnyItemActive(), "real Shader multiline field receives input focus");
        io.AddInputCharactersUTF8("edited");
        frame(draw);
        const std::string edited = app.m_shaderSource.data();
        check(app.isShaderDirty() && PlatformFile::readAllText(first) == original,
              "typing Shader source changes memory without touching its file");
        app.m_selectedAsset = "some-other-material.mat";
        frame(draw);
        check(!app.m_shaderEditorHidden && app.m_openShaderAsset == first &&
                  std::string(app.m_shaderSource.data()) == edited,
              "selecting a different asset preserves the open Shader and its text");
        app.requestShaderAction(EditorApp::ShaderAction::Close);
        check(app.m_shaderAction == EditorApp::ShaderAction::Close && !app.m_shaderEditorHidden,
              "closing dirty Shader waits for a decision");
        const auto drawDialog = [&]()
        {
            app.drawShaderEditor();
            app.drawShaderCloseDialog();
        };
        frame(drawDialog);
        frame(drawDialog);
        auto* dialog = ImGui::FindWindowByName("Unsaved shader###ShaderCloseDialog");
        check(dialog && dialog->Active && !dialog->Hidden, "dirty Shader displays its actual confirmation modal");
        ImGui::ActivateItemByID(dialog->GetID("Cancel"));
        frame(drawDialog);
        check(app.m_shaderAction == EditorApp::ShaderAction::None && app.isShaderDirty() && !app.m_shaderEditorHidden &&
                  PlatformFile::readAllText(first) == original,
              "confirmation Cancel button keeps edits and leaves disk unchanged");
        app.requestShaderAction(EditorApp::ShaderAction::Open, second);
        check(app.m_openShaderAsset == first && app.m_pendingShaderPath == second,
              "switching Shader cannot replace a dirty buffer before confirmation");
        app.resolveShaderAction(EditorApp::ShaderDecision::Cancel);
        // 文本框占用键盘焦点时，Ctrl+S 仍应保存文档。
        io.AddMousePosEvent(start.x + 30, start.y + ImGui::GetTextLineHeightWithSpacing() + 12);
        frame(draw);
        io.AddMouseButtonEvent(0, true);
        frame(draw);
        io.AddMouseButtonEvent(0, false);
        frame(draw);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_S, true);
        frame(
            [&]()
            {
                app.drawShaderEditor();
                app.handleShortcuts();
            });
        io.AddKeyEvent(ImGuiKey_S, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        frame(draw);
        check(!app.isShaderDirty() && PlatformFile::readAllText(first) == edited &&
                  PlatformFile::readAllText(first + ".bak") == original &&
                  app.m_sceneSaveDocument == EditorApp::kInvalidSceneDocument,
              "Shader Ctrl+S atomically saves with backup and does not open scene Save As");
        io.AddInputCharactersUTF8("discard-me");
        frame(draw);
        check(app.isShaderDirty(), "further text editing marks Shader dirty again");
        app.requestShaderAction(EditorApp::ShaderAction::Open, second);
        check(app.resolveShaderAction(EditorApp::ShaderDecision::Discard) && app.m_openShaderAsset == second &&
                  !app.isShaderDirty() && PlatformFile::readAllText(first) == edited,
              "Discard switches Shader without writing discarded text");
        const std::string changed = other + "// pending\n";
        app.m_shaderSource.assign(changed.begin(), changed.end());
        app.m_shaderSource.push_back('\0');
        app.requestShaderAction(EditorApp::ShaderAction::Close);
        // 使用非空的 .bak 目录阻止原子写入，无需依赖平台权限设置。
        std::filesystem::create_directories(second + ".bak");
        PlatformFile::writeAllText(std::filesystem::path(second + ".bak") / "blocker", "keep");
        check(!app.resolveShaderAction(EditorApp::ShaderDecision::Save) && app.isShaderDirty() &&
                  !app.m_shaderEditorHidden && app.m_shaderAction == EditorApp::ShaderAction::Close &&
                  PlatformFile::readAllText(second) == other && !std::filesystem::exists(second + ".tmp"),
              "failed atomic save preserves original file, dirty buffer and pending close");
        app.resolveShaderAction(EditorApp::ShaderDecision::Cancel);
        PlatformFile::writeAllText(second, "// external edit\n");
        check(!app.saveShaderDocument() && app.isShaderDirty() &&
                  PlatformFile::readAllText(second) == "// external edit\n",
              "external edits are not overwritten by a stale Shader buffer");
        for (auto& document : app.m_sceneDocuments)
            document.dirty = false;
        app.m_sceneDirty = false;
        app.m_selectedAsset.clear();
        app.requestEditorExit();
        check(app.m_shaderAction == EditorApp::ShaderAction::Exit && app.m_exitInProgress,
              "application exit checks Shader changes even with no dirty scenes");
        app.resolveShaderAction(EditorApp::ShaderDecision::Cancel);
        check(!app.m_exitInProgress && app.isShaderDirty(),
              "canceling Shader exit preserves the document and cancels exit");
        app.requestShaderAction(EditorApp::ShaderAction::Close);
        check(app.resolveShaderAction(EditorApp::ShaderDecision::Discard) && app.m_shaderEditorHidden &&
                  !app.isShaderDirty(),
              "discarding close hides Shader without saving");
        app.requestShaderAction(EditorApp::ShaderAction::Open, second);
        check(std::string(app.m_shaderSource.data()) == "// external edit\n",
              "reopening a closed Shader reloads current disk content");
        check(!app.openShaderDocument((root / "missing.frag").generic_string()) && app.m_openShaderAsset == second,
              "failed Shader open preserves the current document");
        app.requestShaderAction(EditorApp::ShaderAction::Open, first);
        const std::string savedOnClose = edited + "// explicit save on close\n";
        app.m_shaderSource.assign(savedOnClose.begin(), savedOnClose.end());
        app.m_shaderSource.push_back('\0');
        app.requestShaderAction(EditorApp::ShaderAction::Close);
        frame(drawDialog);
        frame(drawDialog);
        dialog = ImGui::FindWindowByName("Unsaved shader###ShaderCloseDialog");
        ImGui::ActivateItemByID(dialog->GetID("Save"));
        frame(drawDialog);
        if (app.m_shaderAction != EditorApp::ShaderAction::None || !app.m_shaderEditorHidden ||
            PlatformFile::readAllText(first) != savedOnClose)
            std::cerr << "Shader save diagnostic: action=" << static_cast<int>(app.m_shaderAction)
                      << " hidden=" << app.m_shaderEditorHidden << " error=" << app.m_shaderError
                      << " dirty=" << app.isShaderDirty() << " modal=" << dialog->Active
                      << " disk=" << PlatformFile::readAllText(first) << " expected=" << savedOnClose << '\n';
        check(app.m_shaderAction == EditorApp::ShaderAction::None && app.m_shaderEditorHidden &&
                  PlatformFile::readAllText(first) == savedOnClose,
              "confirmation Save button persists edits before hiding the window");
    }

    void windowClose()
    {
        check(SDL_Init(SDL_INIT_EVENTS) == 0, "SDL event queue initializes without a graphics window");
        SdlGlRenderDevice device(1, 1, "Headless close request");
        int closeRequests = 0;
        device.setCloseRequestCallback([&]() { ++closeRequests; });
        SDL_Event event{};
        event.type = SDL_QUIT;
        check(SDL_PushEvent(&event) == 1 && device.pumpEvents() && closeRequests == 1,
              "system window close is routed to confirmation without ending the event loop");
        device.setCloseRequestCallback({});
        check(SDL_PushEvent(&event) == 1 && !device.pumpEvents(),
              "runtime without a close handler still exits on SDL_QUIT");
    }
    void materialDocuments()
    {
        MaterialAssetData data;
        data.formatVersion = 3;
        data.parentPath = "builtin:PBR";
        MaterialAssetEditor asset;
        const auto definition = makeBuiltinPbrDefinition();
        check(asset.Open("saved/EditorMaterials/instance.mat", data, definition.parameters),
              "open typed material draft");
        check(asset.setOverride("BaseColor", Vec4{1, 0, 0, 1}) && asset.isDirty(),
              "typed override marks real draft dirty");
        check(!asset.setOverride("BaseColor", 1.f) && asset.GetDraft().overrides.size() == 1,
              "wrong override type rejected");
        check(asset.resetOverride("BaseColor") && asset.GetDraft().overrides.empty(),
              "reset removes override for inheritance");
        asset.setOverride("Metallic", .8f);
        asset.setSaveRequest(
            [](const auto&, const auto&, auto& diagnostics)
            {
                diagnostics.push_back({MaterialDiagnosticSeverity::Error, {}, 0, -1, "test validator rejection"});
                return false;
            });
        check(!asset.Save() && asset.isDirty() && !asset.GetDiagnostics().empty(),
              "failed candidate preserves dirty draft and diagnostics");
        asset.setSaveRequest([](const auto& path, const auto& data, auto&)
                             { return PlatformFile::writeAllText(fromUtf8Path(path), serializeMaterialAsset(data)); });
        check(asset.Save() && !asset.isDirty(), "successful save clears dirty state");
        MaterialResource saved;
        check(saved.load("saved/EditorMaterials/instance.mat") &&
                  std::abs(std::get<float>(saved.GetMaterialData().overrides.at("Metallic")) - .8f) < .00001f,
              "saved typed material reopens without loss");
        std::string error;
        check(MaterialAssetEditor::createInstance("saved/EditorMaterials/child.mat",
                                                  "saved/EditorMaterials/instance.mat", error),
              "create child instance asset");
        check(saved.load("saved/EditorMaterials/child.mat") && saved.GetMaterialData().parentPath == "instance.mat",
              "child has v3 relative parent reference");
        MaterialDefinitionData graph;
        graph.graph.setFormatVersion(3);
        graph.sourcePath = "saved/EditorMaterials/parent.mat";
        graph.graph.addNode(MaterialNodeType::SurfaceOutput, "Surface");
        MaterialGraphEditor editor;
        check(editor.Open(graph.sourcePath, graph), "open graph draft");
        auto color = editor.addNode(MaterialNodeType::Constant, "Color");
        check(!editor.connectPorts(color, 0, 1, 0), "Vec4 to Vec3 connection requires explicit mask");
        auto mask = editor.addNode(MaterialNodeType::ComponentMask, "RGB");
        check(editor.connectPorts(color, 0, mask, 0) && editor.connectPorts(mask, 0, 1, 0),
              "real typed output and input ports connect");
        check(editor.deleteNode(mask) && editor.GetDraft().graph.GetLinks().empty(),
              "deleting a node removes attached links");
        frame(
            [&]
            {
                asset.draw();
                editor.draw();
            });
        check(editor.isDirty(), "actual ImGui material modules retain draft state");
    }
    void materialTexturePortability()
    {
        const auto directory = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/EditorPortable"));
        const auto assets = directory / "assets";
        std::filesystem::create_directories(assets);
        PlatformFile::writeAllText(assets / "white.ppm", "P3\n1 1\n255\n255 255 255\n");
        MaterialAssetData data;
        data.formatVersion = 3;
        data.parentPath = "builtin:PBR";
        auto schema = makeBuiltinPbrDefinition().parameters;
        for (auto& p : schema)
            if (p.name == "BaseMap")
                p.defaultValue = toUtf8GenericPath((assets / "white.ppm"));
        MaterialAssetEditor editor;
        editor.Open(toUtf8GenericPath((assets / "child.mat")), data, schema);
        editor.setSaveRequest(
            [](const auto& path, const auto& draft, auto&)
            { return PlatformFile::writeAllText(fromUtf8Path(path), serializeMaterialAsset(draft)); });
        frame([&] { editor.draw(); });
        frame([&] { editor.draw(); });
        auto window = ImGui::FindWindowByID(ImHashStr("###MaterialAssetEditor"));
        ImGui::ActivateItemByID(ImHashStr("###Override", 0, ImHashStr("BaseMap", 0, window->ID)));
        frame([&] { editor.draw(); });
        check(editor.GetDraft().overrides.count("BaseMap") &&
                  !fromUtf8Path(std::get<std::string>(editor.GetDraft().overrides.at("BaseMap"))).is_absolute(),
              "actual inherited texture checkbox stores a portable reference");
        check(editor.Save(), "save inherited texture override");
        check(PakArchive::packDirectory(directory, directory.parent_path() / "EditorPortable.tpak"),
              "package checkbox-created override");
        const auto unavailable = directory / "assets-unavailable";
        std::filesystem::remove_all(unavailable);
        std::filesystem::rename(assets, unavailable);
        ResourceManager resources;
        resources.initialize(nullptr, assets);
        PlatformFile::clearMounts();
        PlatformFile::mountPak(directory.parent_path() / "EditorPortable.tpak");
        PlatformFile::setReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
        MaterialSystem system(resources);
        const auto proxy = system.BuildProxy("assets/child.mat");
        const bool loaded = std::any_of(proxy->textures.begin(), proxy->textures.end(),
                                        [](const auto& t) { return t.parameterName == "BaseMap" && t.resource; });
        PlatformFile::clearMounts();
        resources.shutdown();
        std::filesystem::rename(unavailable, assets);
        check(loaded, "checkbox-created texture resolves from package with original sources unavailable");
    }
    void materialExternalReload()
    {
        const auto assets = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/EditorReload/assets"));
        auto definition = makeBuiltinPbrDefinition();
        PlatformFile::writeAllText(assets / "parent.mat", definition.graph.serialize());
        MaterialAssetData data;
        data.formatVersion = 3;
        data.parentPath = "parent.mat";
        data.overrides["Roughness"] = .2f;
        PlatformFile::writeAllText(assets / "child.mat", serializeMaterialAsset(data));
        ResourceManager resources;
        resources.initialize(nullptr, assets);
        Renderer renderer;
        renderer.setResourceManager(&resources);
        EditorApp app;
        auto& context = const_cast<EngineContext&>(app.m_engine.GetContext());
        context.resourceManager = &resources;
        context.renderer = &renderer;
        const auto select = [&](const std::string& name)
        {
            for (const auto& a : resources.getAssetRegistry().GetAssets())
                if (fromUtf8Path(a.path).filename() == name)
                {
                    app.selectAsset(a.path, false);
                    app.openAsset(a);
                }
        };
        const auto draw = [&] { app.drawMaterialGraphEditor(); };
        const auto reload = [&]
        {
            auto& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddKeyEvent(ImGuiKey_R, true);
            frame(draw);
            io.AddKeyEvent(ImGuiKey_R, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame(draw);
        };
        select("child.mat");
        frame(draw);
        check(app.m_materialAssetEditor.GetPath() == toUtf8GenericPath(assets / "child.mat") &&
                  !app.m_materialGraphEditor.isVisible(),
              "instance content opens parameter editor for the unified Material asset type");
        auto* instanceWindow = ImGui::FindWindowByID(ImHashStr("###MaterialAssetEditor"));
        ImGui::ActivateItemByID(ImHashStr("###OpenParentMaterial", 0, instanceWindow->ID));
        frame(draw);
        frame(draw);
        check(app.m_materialGraphEditor.GetPath() == toUtf8GenericPath(assets / "parent.mat") &&
                  app.m_materialGraphEditor.isVisible(),
              "Open parent navigates from an instance to its graph material with the same extension");
        select("child.mat");
        frame(draw);
        data.overrides["Roughness"] = .8f;
        PlatformFile::writeAllText(assets / "child.mat", serializeMaterialAsset(data));
        reload();
        check(app.m_materialAssetEditor.Save(), "save clean instance after external Ctrl+R");
        MaterialResource loaded;
        loaded.load(toUtf8GenericPath((assets / "child.mat")));
        check(std::get<float>(loaded.GetMaterialData().overrides.at("Roughness")) == .8f,
              "Ctrl+R refreshes clean instance draft and saved baseline");
        select("parent.mat");
        frame(draw);
        definition.graph.GetParameters()[1].defaultValue = .6f;
        PlatformFile::writeAllText(assets / "parent.mat", definition.graph.serialize());
        reload();
        check(app.m_materialGraphEditor.Save(), "save clean graph after external Ctrl+R");
        MaterialDefinitionResource saved;
        saved.load(toUtf8GenericPath((assets / "parent.mat")));
        check(std::get<float>(saved.GetData().parameters[1].defaultValue) == .6f, "Ctrl+R refreshes clean graph draft");
        select("child.mat");
        frame(draw);
        app.m_materialAssetEditor.setOverride("Roughness", .5f);
        data.overrides["Roughness"] = .9f;
        PlatformFile::writeAllText(assets / "child.mat", serializeMaterialAsset(data));
        reload();
        check(app.m_materialAssetEditor.isDirty() &&
                  std::get<float>(app.m_materialAssetEditor.GetDraft().overrides.at("Roughness")) == .5f &&
                  !app.m_materialAssetEditor.Save(),
              "dirty external reload retains draft and blocks stale overwrite with conflict");
        app.m_materialAssetEditor.Discard();
        check(std::get<float>(app.m_materialAssetEditor.GetDraft().overrides.at("Roughness")) == .9f,
              "discard after reload restores latest external baseline");
        context.renderer = nullptr;
        context.resourceManager = nullptr;
        renderer.shutdown();
        resources.shutdown();
    }
    void materialWorkflowGpu()
    {
        const auto root = std::filesystem::weakly_canonical(std::filesystem::absolute("saved/MaterialWorkflow"));
        const auto assets = root / "assets";
        std::filesystem::create_directories(assets);
        PlatformFile::writeAllText(assets / "quad.mesh",
                                   "v -0.5 -0.5 0\nv 0.5 -0.5 0\nv 0.5 0.5 0\nv -0.5 0.5 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt "
                                   "0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n");
        PlatformFile::writeAllText(assets / "default.input", PlatformFile::readAllText("assets/default.input"));
        ResourceManager resources;
        resources.initialize(nullptr, assets);
        Renderer renderer;
        renderer.setResourceManager(&resources);
        auto gpu = std::make_unique<SdlGlRenderDevice>(128, 64, "Material editor workflow");
        auto* device = gpu.get();
        check(renderer.initialize(std::move(gpu)), "material editor workflow initializes real GPU");
        SDL_HideWindow(static_cast<SDL_Window*>(device->getNativeWindow()));
        device->enableSceneRenderTarget(true, 128, 64);
        device->setTaaEnabled(false);
        device->setGtaoEnabled(false);
        device->setBloomEnabled(false);
        device->setFxaaEnabled(false);
        auto& system = renderer.GetMaterialSystem();
        const auto validator = [&](const auto& compiled, auto& diagnostics)
        { return device->validateMaterial(compiled, diagnostics); };
        MaterialDefinitionData definition;
        definition.sourcePath = toUtf8GenericPath((assets / "Shared.mat"));
        definition.renderState.shadingModel = MaterialShadingModel::Unlit;
        definition.graph.setFormatVersion(3);
        definition.graph.addNode(MaterialNodeType::SurfaceOutput, "Surface");
        MaterialParameterDecl color;
        color.name = "Tint";
        color.type = MaterialParameterType::Vec3;
        color.defaultValue = Vec3{.3f, .3f, .3f};
        definition.parameters = {color};
        auto id = definition.graph.addNode(MaterialNodeType::VectorParameter, "Tint");
        definition.graph.GetNodes().back().parameterName = "Tint";
        definition.graph.connect(id, 1, 0);
        MaterialGraphEditor graph;
        graph.Open(definition.sourcePath, definition);
        graph.setSaveRequest(
            [&](const auto& path, const auto& data, auto& diagnostics)
            {
                const auto r = system.applyDefinitionDraft(path, data, validator);
                diagnostics = r.diagnostics;
                return r.success;
            });
        check(graph.Save(), "real graph editor saves a validated parent definition");
        std::string error;
        for (const auto& name : {"Red", "Blue"})
            check(MaterialAssetEditor::createInstance(toUtf8GenericPath((assets / (std::string(name) + ".mat"))),
                                                      definition.sourcePath, error),
                  "create instance through material editor command");
        MaterialAssetEditor red, blue;
        MaterialResource data;
        data.load(toUtf8GenericPath((assets / "Red.mat")));
        red.Open(toUtf8GenericPath((assets / "Red.mat")), data.GetMaterialData(), definition.parameters);
        data.load(toUtf8GenericPath((assets / "Blue.mat")));
        blue.Open(toUtf8GenericPath((assets / "Blue.mat")), data.GetMaterialData(), definition.parameters);
        const auto save = [&](const auto& path, const auto& draft, auto& diagnostics)
        {
            const auto r = system.applyInstanceDraft(path, draft, validator);
            diagnostics = r.diagnostics;
            return r.success;
        };
        red.setSaveRequest(save);
        blue.setSaveRequest(save);
        red.setOverride("Tint", Vec3{1, 0, 0});
        blue.setOverride("Tint", Vec3{0, 0, 1});
        check(red.Save() && blue.Save(), "save independent red and blue asset overrides");
        frame(
            [&]
            {
                graph.draw();
                red.draw();
            });
        World scene;
        const auto a = scene.createEntity("Red"), b = scene.createEntity("Blue");
        for (auto e : {a, b})
        {
            scene.AddComponent(e, MeshComponent{"assets/quad.mesh", e == a ? "assets/Red.mat" : "assets/Blue.mat"});
            scene.GetComponent<TransformComponent>(e)->position = {e == a ? -.65f : .65f, 0, 0};
        }
        renderer.setCameraOverride(true, {0, 0, 3}, {});
        renderer.renderFrame(scene);
        auto hdr = device->ReadHdrAttachmentForTests(0);
        check(hdr[32 * 128 + 52].x > .9f && hdr[32 * 128 + 76].z > .9f,
              "assigned instances render red and blue in the actual scene");
        scene.GetComponent<MeshComponent>(a)->materialPath = "assets/Shared.mat";
        renderer.renderFrame(scene);
        hdr = device->ReadHdrAttachmentForTests(0);
        check(std::abs(hdr[32 * 128 + 52].x - .3f) < .02f && std::abs(hdr[32 * 128 + 52].y - .3f) < .02f,
              "node material renders its defaults directly on a scene mesh");
        scene.GetComponent<MeshComponent>(a)->materialPath = "assets/Red.mat";
        check(graph.setParameterDefault("Tint", Vec3{0, 1, 0}) && graph.Save(),
              "parent default changes through the graph editor command");
        auto dynamic = system.CreateDynamicInstance("assets/Blue.mat");
        dynamic->setParameter("Tint", Vec3{1, 1, 0});
        renderer.setDynamicMaterial(b, dynamic);
        renderer.renderFrame(scene);
        hdr = device->ReadHdrAttachmentForTests(0);
        check(hdr[32 * 128 + 52].x > .9f && hdr[32 * 128 + 76].x > .9f && hdr[32 * 128 + 76].y > .9f,
              "dynamic color affects only its assigned scene entity");
        auto broken = graph.GetDraft();
        broken.graph.GetLinks()[0].fromNode = 999;
        graph.Open(definition.sourcePath, broken);
        graph.addNode(MaterialNodeType::Constant, "Unused");
        check(!graph.Save() && graph.isDirty() && !graph.GetDiagnostics().empty(),
              "broken editor graph cannot replace the live scene material");
        renderer.renderFrame(scene);
        hdr = device->ReadHdrAttachmentForTests(0);
        check(hdr[32 * 128 + 76].y > .9f, "failed editor save keeps the current rendered material");
        graph.disconnectPort(1, 0);
        check(graph.connectPorts(id, 0, 1, 0) && graph.Save(),
              "repair graph link and publish from the same editor draft");
        SceneManager scenes(&scene);
        check(scenes.saveScene(assets / "MaterialDemo.scene"), "save scene referencing the new material assets");
        ProjectSettings project;
        project.name = "MaterialWorkflow";
        project.projectFile = root / "MaterialWorkflow.tinyproject";
        project.assetDirectory = "assets";
        project.startupScene = "assets/MaterialDemo.scene";
        check(project.Save(project.projectFile), "save isolated material example project");
        const auto package =
            ProjectPackager::package({project.projectFile, root / "Package", TINYENGINE_TEST_GAME_PATH});
        check(package.success, "package complete edited material scene with offline validation");
        renderer.shutdown();
        resources.shutdown();
    }
};
} // namespace tinyengine

int main(int argc, char** argv)
{
    try
    {
        std::cout << std::unitbuf;
        tinyengine::EditorInteractionTests tests;
        if (argc > 1 && std::string(argv[1]) != "--legacy")
        {
            const std::string filter = argv[1];
            if (filter == "--logical-ui")
                tests.logicalUiMetrics();
            else if (filter == "--project-create")
                tests.projectCreation();
            else if (filter == "--dpi")
                tests.nativeDialogDpi();
            else if (filter == "--ground-grid")
                tests.groundGridContinuity();
            else if (filter == "--view-cube")
                tests.viewCubeNavigation();
            else if (filter == "--combined-gizmo")
                tests.combinedGizmo();
            else if (filter == "--p1-drop")
                tests.p1SceneDrag();
            else if (filter == "--p1-orbit")
                tests.p1OrbitStability();
            else if (filter == "--p1-import")
                tests.p1HiddenImport();
            else if (filter == "--p1-docking")
                tests.p1Docking();
            else if (filter == "--p1-settings")
                tests.p1Settings();
            else if (filter == "--p1-assets")
                tests.p1AssetWorkflow();
            else if (filter == "--p1-scene")
                tests.p1SceneNavigation();
            else if (filter == "--animation-assets")
                tests.animationAssetRouting();
            else if (filter == "--asset-menus")
                tests.assetBrowserMenus();
            else if (filter == "--asset-filters")
                tests.assetBrowserFilters();
            else if (filter == "--asset-references")
                tests.assetBrowserReferences();
            else if (filter == "--material-assignment")
                tests.materialAssignmentRegression();
            else if (filter == "--asset-delete")
                tests.assetDeleteShortcutRegression();
            else if (filter == "--material-trash-cache")
                tests.materialTrashCacheRegression();
            return 0;
        }
        if (argc == 1)
        {
            tests.p1Settings();
            tests.p1HiddenImport();
            tests.p1Docking();
            tests.p1AssetWorkflow();
            tests.p1SceneNavigation();
            tests.p1OrbitStability();
            tests.p1SceneDrag();
        }
        tests.logicalUiMetrics();
        tests.groundGridContinuity();
        tests.viewCubeNavigation();
        tests.shortcuts();
        tests.documents();
        tests.assetClicks();
        tests.assetBrowserMenus();
        tests.assetBrowserFilters();
        tests.assetBrowserReferences();
        tests.materialAssignmentRegression();
        tests.assetDeleteShortcutRegression();
        tests.materialTrashCacheRegression();
        tests.assetGridDrag();
        tests.inspector();
        tests.inspectorAxisDefaults();
        tests.inspectorBatchAndValidation();
        tests.animationAssetRouting();
        tests.typedSkeletonAndFailedOpen();
        tests.packagingWriteLock();
        tests.gizmo();
        tests.hierarchyCameraFocus();
        tests.combinedGizmo();
        tests.viewportRegression();
        tests.compactWorkspace();
        tests.sceneOpenRegression();
        tests.shaderDocuments();
        tests.windowClose();
        tests.materialDocuments();
        tests.materialTexturePortability();
        tests.materialExternalReload();
        tests.materialWorkflowGpu();
        std::cout << tests.checks << " interaction checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
