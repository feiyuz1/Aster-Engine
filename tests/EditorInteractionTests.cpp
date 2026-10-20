// 不启动窗口，直接用 ImGui 输入驱动真实 World，检查编辑器命令；不依赖 SDL、DPI 或输入法。
#include "EditorApp.hpp"
#include "MaterialAssetEditor.hpp"
#include "MaterialGraphEditor.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#include "MiniEngine/Project/ProjectPackager.hpp"
#include "MiniEngine/Resource/PakArchive.hpp"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <SDL_syswm.h>
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <fstream>

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

    void LogicalUiMetrics()
    {
        const auto dpi=app.m_dpiScale, ui=app.m_uiScale;
        app.m_uiScale=1; app.m_dpiScale=1;
        Check(std::abs(app.Scaled(96)-96)<.001f,"logical UI dimensions use the configured point size at 100 percent DPI");
        app.m_dpiScale=2;
        Check(std::abs(app.Scaled(96)-96)<.001f,"higher framebuffer DPI does not double asset and inspector UI dimensions");
        app.m_uiScale=1.25f;
        Check(std::abs(app.Scaled(96)-120)<.001f,"UI size preference scales logical dimensions independently from monitor DPI");
        app.m_dpiScale=dpi; app.m_uiScale=ui;
    }

    void ProjectCreation()
    {
        namespace fs = std::filesystem;
        const auto root = fs::temp_directory_path()/("TinyCreateUI-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))/fs::u8path(u8"中文 with spaces");
        fs::create_directories(root);
        std::snprintf(app.m_newProjectName.data(), app.m_newProjectName.size(), "%s", "UIFirstGame");
        std::snprintf(app.m_newProjectParent.data(), app.m_newProjectParent.size(), "%s", root.u8string().c_str());
        app.StartProjectCreation();
        Check(app.m_projectCreationFuture.valid() && app.m_requestedProject.empty(),
              "create and open starts a background build before requesting a project switch");
        const auto wait = [&] {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(70);
            int frames = 0;
            while (app.m_projectCreationFuture.valid() && std::chrono::steady_clock::now() < until)
            {
                Frame([&] { app.DrawProjectLauncher(); });
                ++frames;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Check(!app.m_projectCreationFuture.valid() && frames > 1, "launcher keeps drawing while C++ code builds");
        };
        wait();
        const auto project = root/"UIFirstGame/UIFirstGame.tinyproject";
        if (app.m_requestedProject != project || !app.m_projectLauncherError.empty())
            std::cerr << app.m_projectLauncherError << '\n' << app.m_projectCreationOutput << '\n';
        Check(app.m_requestedProject == project && app.m_projectLauncherError.empty(),
              "successful first compilation automatically opens the new C++ project");
        // 模拟清理忽略的 build/ 后，通过现有菜单重新生成 VS 工程。
        fs::rename(project.parent_path()/"build", project.parent_path()/"Saved/InitialBuild");
        const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings()).projectFile = project;
        app.StartSolutionGeneration();
        app.m_solutionFuture.wait();
        const auto regenerated = app.m_solutionFuture.get();
        if (!regenerated.success) std::cerr << regenerated.output << '\n';
        Check(regenerated.success && fs::is_regular_file(regenerated.solutionFile),
              "Generate VS rediscovers the installed SDK after clearing the build cache");
        app.m_requestedProject.clear();
        const auto sourceFile = project.parent_path()/"Source/Game.cpp";
        const auto read = [](const fs::path &path) { std::ifstream stream(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(stream), {}); };
        const auto source = read(sourceFile);
        std::ofstream(sourceFile, std::ios::app) << "\n#error deliberate_ui_build_failure\n";
        app.StartCreatedProjectBuild();
        wait();
        Check(app.m_requestedProject.empty() && !app.m_projectLauncherError.empty() && !app.m_projectCreationOutput.empty() && fs::exists(project),
              "compile failure shows diagnostics, retains files and does not open an unbuilt project");
        std::ofstream(sourceFile, std::ios::trunc) << source;
        app.StartCreatedProjectBuild();
        wait();
        Check(app.m_requestedProject == project && app.m_projectLauncherError.empty(),
              "retry after fixing the error opens the retained project");
    }

    void NativeDialogDpi()
    {
#ifdef _WIN32
        const auto root=std::filesystem::absolute("Saved/EditorDpi")/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(root/"assets");
        ProjectSettings project; project.name="DPI regression"; project.projectFile=root/"Dpi.tinyproject"; project.assetDirectory="assets";
        project.startupScene="assets/Startup.scene";
        World startupWorld; startupWorld.CreateEntity("Player");
        Check(SceneSerializer::Save(startupWorld,root/project.startupScene),"save valid isolated DPI startup scene");
        Check(project.Save(project.projectFile),"save isolated DPI startup fixture");
        auto *previousContext=ImGui::GetCurrentContext(); EditorApp editor;
        struct Restore { EditorApp &editor; ImGuiContext *context; ~Restore() { editor.Shutdown(); ImGui::SetCurrentContext(context); } } restore{editor,previousContext};
        ImGui::SetCurrentContext(nullptr);
        Check(editor.Initialize(project.projectFile),"initialize actual editor before any native picker");
        auto *window=static_cast<SDL_Window *>(editor.m_device->GetNativeWindow()); SDL_HideWindow(window);
        SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
        Check(SDL_GetWindowWMInfo(window,&info)==SDL_TRUE,"DPI fixture exposes the real editor HWND");
        const auto context=GetWindowDpiAwarenessContext(info.info.win.window);
        const auto awareness=GetAwarenessFromDpiAwarenessContext(context);
        std::cout << "Editor startup DPI awareness: " << awareness << '\n';
        Check(awareness==DPI_AWARENESS_PER_MONITOR_AWARE,"editor locks per-monitor DPI awareness before creating windows");
        int width=0,height=0,pixelsWide=0,pixelsHigh=0;
        SDL_GetWindowSize(window,&width,&height); SDL_GL_GetDrawableSize(window,&pixelsWide,&pixelsHigh);
        const float scale=GetDpiForWindow(info.info.win.window)/96.0f;
        Check(std::abs(pixelsWide-width*scale)<=2 && std::abs(pixelsHigh-height*scale)<=2,
              "editor SDL coordinates remain logical points on scaled displays");
        // A legacy Shell context-menu extension must not change the process from virtualized to aware mid-session.
        SetProcessDPIAware(); SDL_PumpEvents();
        int nextWidth=0,nextHeight=0,nextPixelsWide=0,nextPixelsHigh=0;
        SDL_GetWindowSize(window,&nextWidth,&nextHeight); SDL_GL_GetDrawableSize(window,&nextPixelsWide,&nextPixelsHigh);
        Check(AreDpiAwarenessContextsEqual(context,GetWindowDpiAwarenessContext(info.info.win.window)),
              "late Shell DPI initialization cannot change editor window awareness");
        Check(width==nextWidth && height==nextHeight && pixelsWide==nextPixelsWide && pixelsHigh==nextPixelsHigh,
              "late Shell DPI initialization preserves logical and physical editor sizes");
        editor.m_dpiScale=scale>1.2f ? 1.0f : 2.0f;
        editor.Run(1);
        Check(std::abs(editor.m_dpiScale-static_cast<float>(pixelsWide)/width)<.01f,
              "editor refreshes scene framebuffer scale from the current SDL frame after a DPI change");
#endif
    }

    void P1Settings()
    {
        auto &settings = const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings());
        const auto old = settings;
        settings.projectFile = std::filesystem::absolute("Saved/P1Settings/Settings.tinyproject");
        app.m_uiScale = 1.25f; app.m_cameraSpeed = 2.5f; app.m_showSceneGrid = false;
        app.m_showHierarchy = false; app.m_showAssets = false; app.m_showPerformance = false;
        Check(app.SaveEditorSettings(), "P1 preferences save to project-local settings");
        app.m_uiScale = 1; app.m_cameraSpeed = 1; app.m_showSceneGrid = true;
        app.m_showHierarchy = true; app.m_showAssets = true; app.m_showPerformance = true;
        app.LoadEditorSettings();
        Check(std::abs(app.m_uiScale-1.25f)<.01f && std::abs(app.m_cameraSpeed-2.5f)<.01f && !app.m_showSceneGrid,
              "UI scale, camera speed and grid preference survive restart");
        Check(!app.m_showHierarchy && !app.m_showAssets && !app.m_showPerformance, "closed panels survive restart");
        settings = old; app.m_uiScale=1; app.m_cameraSpeed=1; app.m_showSceneGrid=true;
        app.m_showHierarchy=app.m_showAssets=app.m_showPerformance=true;
    }

    void P1Docking()
    {
        auto *previousContext=ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(ImGui::CreateContext());
        auto &io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={1280,900}; io.DeltaTime=1.0f/60;
        unsigned char *pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
        auto &settings=const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings()); const auto old=settings;
        settings.projectFile=std::filesystem::absolute("Saved/P1Docking/Workspace.tinyproject");
        app.m_resetWorkspace=true;
        const auto draw=[&] {
            app.DrawWorkspace();
            const auto panel=[&](const char* name,bool &open) { if(open) { app.BeginEditorPanel(name,std::string(name)=="Inspector" ? "属性检查器" : name,open,{0,0},{300,300},std::string(name)=="Scene View" && app.m_sceneMaximized); ImGui::TextUnformatted(name); ImGui::End(); } };
            if(!app.m_sceneMaximized) { panel("Hierarchy",app.m_showHierarchy); panel("Inspector",app.m_showInspector); panel("Assets",app.m_showAssets); panel("Console / Stats",app.m_showConsole); panel("Performance",app.m_showPerformance); }
            bool scene=true; panel("Scene View",scene);
        };
        Frame(draw); Frame(draw); Frame(draw);
        const auto assetDock=ImGui::FindWindowByName("Assets")->DockId;
        const auto sceneDock=ImGui::FindWindowByName("Scene View")->DockId;
        Check(assetDock && sceneDock && assetDock!=sceneDock,"default workspace docks panels around the scene");
        Check(ImGui::FindWindowByName("Console / Stats")->DockId==assetDock && ImGui::FindWindowByName("Performance")->DockId==assetDock,
              "assets console and performance share bottom tabs");
        const auto inspectorId=ImGui::FindWindowByName("Inspector")->ID;
        app.m_interfaceLanguage=EditorApp::InterfaceLanguage::SimplifiedChinese; Frame(draw);
        Check(ImGui::FindWindowByName("Inspector")->ID==inspectorId,"language changes preserve panel identity and layout");
        app.m_interfaceLanguage=EditorApp::InterfaceLanguage::English;
        app.m_showInspector=false; Frame(draw); app.m_showInspector=true; Frame(draw);
        Check(ImGui::FindWindowByName("Inspector")->DockId!=0,"closed panel reopens in its dock");
        app.m_sceneMaximized=true; Frame(draw); Frame(draw);
        Check(ImGui::FindWindowByName("Scene View Focus")->DockId==0 && ImGui::FindWindowByName("Scene View Focus")->Size.x>=io.DisplaySize.x-1,
              "F11 expands scene over the workspace");
        app.m_sceneMaximized=false; Frame(draw); Frame(draw);
        Check(ImGui::FindWindowByName("Scene View")->DockId==sceneDock,"F11 restores the previous scene dock");
        ImGui::DockBuilderDockWindow("Console / Stats",sceneDock); Frame(draw); Frame(draw);
        app.m_showPerformance=false;
        Check(app.SaveWorkspace(),"custom docking layout saves to project");
        app.ResetWorkspace(); Frame(draw); Frame(draw);
        Check(app.m_showPerformance && ImGui::FindWindowByName("Console / Stats")->DockId!=ImGui::FindWindowByName("Scene View")->DockId,
              "reset layout restores default panels and bottom tabs");
        Check(app.RestoreWorkspace(),"saved layout reloads"); Frame(draw); Frame(draw);
        Check(!app.m_showPerformance && ImGui::FindWindowByName("Console / Stats")->DockId==ImGui::FindWindowByName("Scene View")->DockId,
              "saved layout restores custom docking and closed panels");
        settings=old; io.ConfigFlags&=~ImGuiConfigFlags_DockingEnable;
        app.m_showHierarchy=app.m_showInspector=app.m_showAssets=app.m_showConsole=app.m_showPerformance=true;
        app.m_sceneMaximized=false;
        ImGui::DestroyContext(); ImGui::SetCurrentContext(previousContext);
    }

    void P1HiddenImport()
    {
#ifdef TINYENGINE_HAS_ASSIMP
        app.m_importQueue.Initialize(nullptr);
        app.m_activeImportTask=app.m_importQueue.Submit("Saved/NoSuchP1Model.fbx","Saved/P1ImportOutput");
        Check(app.m_importQueue.Get(app.m_activeImportTask).status==AssetImportStatus::Failed,"missing-model import reaches real terminal status");
        app.m_showAssets=false; app.m_sceneMaximized=true;
        const auto messages=app.m_messages.size(); app.PollAssetImport();
        Check(app.m_activeImportTask==0 && app.m_messages.size()==messages+1,"hidden asset panel still publishes import result and releases packaging lock");
        app.PollAssetImport(); Check(app.m_messages.size()==messages+1,"import completion publishes once");
        app.m_showAssets=true; app.m_sceneMaximized=false;
#endif
    }

    void P1AssetWorkflow()
    {
        const auto root=std::filesystem::absolute("Saved/P1AssetWorkflow");
        std::filesystem::create_directories(root/"assets/Models");
        PlatformFile::WriteAllText(root/"assets/Models/a.mesh", "mesh\n");
        ResourceManager resources; resources.Initialize(nullptr,root/"assets");
        auto &context=const_cast<EngineContext &>(app.m_engine.GetContext());
        context.resourceManager=&resources;
        auto &settings=const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings());
        const auto old=settings; settings.projectFile=root/"Workflow.tinyproject"; settings.assetDirectory="assets";
        world.Clear(); const auto e=world.CreateEntity("Keep inspector"); app.m_selected=e; app.m_selection={e};
        app.m_selectedAsset.clear(); app.m_selectedAssets.clear(); app.m_showAssets=false;
        app.m_assetTypeFilters={"Material"}; std::strcpy(app.m_assetSearch.data(),"excluded");
        app.LocateAsset("assets/Models/a.mesh");
        Check(app.m_showAssets && app.m_assetFolder.lexically_normal()==(root/"assets/Models").lexically_normal(),
              "Locate reference opens the asset browser at the containing folder");
        Check(app.m_selectedAsset.empty() && app.m_selected==e && app.m_selection==std::vector<Entity>{e},
              "Locate reference preserves entity inspector ownership");
        Check(app.m_assetSearch[0]==0 && app.m_assetTypeFilters.empty() && app.m_selectedAssets.size()==1,
              "Locate reference clears filters and highlights the target asset");
        app.m_assetRenameRequested=false; app.m_assetsFocused=false; Key(ImGuiKey_F2);
        Check(!app.m_assetRenameRequested,"F2 outside asset browser cannot rename");
        app.m_assetsFocused=true; Key(ImGuiKey_F2,false,false,true);
        Check(!app.m_assetRenameRequested,"text input owns F2 while editing");
        Key(ImGuiKey_F2);
        Check(app.m_assetRenameRequested,"focused asset browser receives F2 rename");
        app.m_assetRenameRequested=false; app.m_assetsFocused=false; app.m_focusAssets=false;
        app.m_selectedAsset="keep-open-asset.mat";
        app.LocateAsset("assets/Models/a.mesh");
        Check(app.m_selectedAsset=="keep-open-asset.mat","Locate preserves an existing asset inspector owner too");
        const auto locatedFolder=app.m_assetFolder; const auto locatedSelection=app.m_selectedAssets;
        app.LocateAsset("builtin:PBR"); app.LocateAsset("assets/missing.mesh");
        Check(app.m_assetFolder==locatedFolder && app.m_selectedAssets==locatedSelection && app.m_selectedAsset=="keep-open-asset.mat",
              "invalid and builtin Locate keep the current browser and inspector state");
        app.m_selectedAsset.clear(); app.m_focusAssets=false; app.m_assetRevealPending=false;
        settings=old; context.resourceManager=nullptr; resources.Shutdown();
    }

    void P1SceneNavigation()
    {
        world.Clear(); const auto e=world.CreateEntity("Orbit target"); app.m_selected=e; app.m_selection={e};
        app.m_editorCameraPosition={0,2,7}; app.m_editorCameraRotation={-15,0,0}; app.m_cameraSpeed=1;
        const auto camera=[&]{ app.UpdateSceneCamera(true,true); };
        auto &io=ImGui::GetIO();
        io.AddMousePosEvent(300,300); Frame(camera);
        io.AddKeyEvent(ImGuiMod_Alt,true); io.AddMouseButtonEvent(0,true); Frame(camera);
        const auto before=app.m_editorCameraPosition; const float distance=Length(before);
        io.AddMousePosEvent(345,315); Frame(camera);
        Check(Length(app.m_editorCameraPosition-before)>.1f && std::abs(Length(app.m_editorCameraPosition)-distance)<.01f,
              "Alt-left orbit changes camera angle while preserving target distance");
        io.AddMouseButtonEvent(0,false); io.AddKeyEvent(ImGuiMod_Alt,false); Frame(camera);
        io.AddMouseButtonEvent(2,true); Frame(camera); const auto panBefore=app.m_editorCameraPosition;
        io.AddMousePosEvent(365,325); Frame(camera);
        Check(Length(app.m_editorCameraPosition-panBefore)>.01f,"middle mouse pans the scene camera");
        io.AddMouseButtonEvent(2,false); Frame(camera);
        const auto baseline=app.m_editorCameraPosition;
        app.m_cameraSpeed=1; io.AddKeyEvent(ImGuiKey_W,true); Frame(camera); const float slow=Length(app.m_editorCameraPosition-baseline);
        io.AddKeyEvent(ImGuiKey_W,false); Frame(camera); app.m_editorCameraPosition=baseline;
        app.m_cameraSpeed=2; io.AddKeyEvent(ImGuiKey_W,true); Frame(camera);
        Check(std::abs(Length(app.m_editorCameraPosition-baseline)-slow*2)<.001f,"camera speed changes actual navigation distance");
        io.AddKeyEvent(ImGuiKey_W,false); Frame(camera); app.m_cameraSpeed=1;
        AssetInfo mesh; mesh.path="assets/player.mesh"; mesh.type="Mesh";
        const Vec3 placement{4,1,-2}; app.m_undo.clear(); const auto original=app.m_engine.GetSceneManager().CreateSnapshot();
        Check(app.PlaceAssetInScene(mesh,&placement),"pointer placement creates a mesh");
        const auto *transform=world.GetComponent<TransformComponent>(app.m_selected);
        Check(transform && Length(transform->position-placement)<.001f && app.m_undo.size()==1,
              "mesh placement and position form one undo transaction");
        app.Undo(); Check(app.m_engine.GetSceneManager().CreateSnapshot()==original,"undo removes the entire pointer placement");
        world.Clear(); const auto surface=world.CreateEntity("Surface"); world.AddComponent(surface,ColliderComponent{});
        app.m_editorCameraPosition={0,0,5}; app.m_editorCameraRotation={}; Vec3 hit;
        const auto querySnapshot=app.m_engine.GetSceneManager().CreateSnapshot();
        Check(app.GetScenePlacement(.5f,.5f,hit) && std::abs(hit.z-.5f)<.001f,"pointer placement hits nearest collider with no device");
        Check(app.m_engine.GetSceneManager().CreateSnapshot()==querySnapshot,"placement preview query never modifies World");
        world.Clear(); app.m_editorCameraPosition={0,2,5}; app.m_editorCameraRotation={-30,0,0};
        Check(app.GetScenePlacement(.5f,.5f,hit) && std::abs(hit.y)<.001f,"empty-space placement falls back to ground plane");
        app.m_editorCameraRotation={30,0,0}; Check(!app.GetScenePlacement(.5f,.5f,hit),"placement above horizon has no valid drop target");
        World prefab; const auto first=prefab.CreateEntity("First"),second=prefab.CreateEntity("Second");
        prefab.GetComponent<TransformComponent>(first)->position={-2,0,0}; prefab.GetComponent<TransformComponent>(second)->position={2,0,0};
        const auto prefabPath=std::filesystem::absolute(std::filesystem::u8path("Saved/P1Scene/中文预制体.prefab")); std::filesystem::create_directories(prefabPath.parent_path());
        Check(SceneSerializer::Save(prefab,prefabPath),"multi-root prefab fixture saves under a Unicode path");
        AssetInfo prefabAsset; prefabAsset.type="Prefab"; prefabAsset.path=prefabPath.generic_u8string(); app.m_undo.clear();
        Check(app.PlaceAssetInScene(prefabAsset,&placement) && app.m_selection.size()==2,"pointer placement loads Unicode multi-root prefab");
        const auto a=world.GetComponent<TransformComponent>(app.m_selection[0])->position;
        const auto b=world.GetComponent<TransformComponent>(app.m_selection[1])->position;
        Check(Length((a+b)*.5f-placement)<.001f && std::abs(Length(a-b)-4)<.001f && app.m_undo.size()==1,
              "prefab roots preserve offsets around pointer in one undo transaction");
        app.Undo(); Check(world.GetEntityManager().GetAliveEntities().empty(),"one undo removes all placed prefab roots");
        app.m_sceneOrbitActive=app.m_scenePanActive=app.m_sceneAltOrbitActive=true; Key(ImGuiKey_F11);
        Check(app.m_sceneMaximized && !app.m_sceneOrbitActive && !app.m_scenePanActive && !app.m_sceneAltOrbitActive,
              "released viewport gestures cannot block F11 while scene tab is hidden");
        app.m_sceneMaximized=false; app.ResetSceneInteraction(); app.m_editorCameraRotation={-10,0,0};
        const auto focus=world.CreateEntity("Dock aspect framing"); world.AddComponent(focus,ColliderComponent{});
        Renderer projectionRenderer; auto &context=const_cast<EngineContext &>(app.m_engine.GetContext()); context.renderer=&projectionRenderer;
        auto &frame=const_cast<FrameView &>(projectionRenderer.GetFrameView());
        frame.projection=Perspective(60,1.6f,.1f,100); app.FocusSceneEntity(focus); const float wideDistance=Length(app.m_editorCameraPosition);
        frame.projection=Perspective(60,.25f,.1f,100); app.FocusSceneEntity(focus);
        Check(Length(app.m_editorCameraPosition)>wideDistance*2,"focus frames selected bounds using the actual narrow dock projection");
        context.renderer=nullptr; world.Clear();
    }

    void GroundGridContinuity()
    {
        world.Clear(); app.m_selected=InvalidEntity; app.m_selection.clear();
        Renderer renderer; auto &context=const_cast<EngineContext &>(app.m_engine.GetContext());
        context.renderer=&renderer; app.m_showSceneGrid=true; app.m_showSelectionBounds=false;
        struct Restore { EditorApp &app; EngineContext &context; ~Restore() { context.renderer=nullptr; app.m_showSelectionBounds=true; } } restore{app,context};
        auto &view=const_cast<FrameView &>(renderer.GetFrameView());
        // Rasterize the real overlay triangles, so coverage/opacity jumps are observable without a GPU.
        int gridVertices=0;
        const auto capture=[&](Vec3 camera,Vec3 direction=Vec3{-.35f,-.55f,-1}) {
            constexpr int w=240,h=180; std::vector<float> pixels(w*h);
            view.cameraPosition=camera;
            view.view=LookAt(camera,camera+Normalize(direction),{0,1,0});
            view.projection=Perspective(60,4.0f/3.0f,.1f,1000);
            Frame([&] {
                ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({1000,800}); ImGui::Begin("Grid continuity");
                auto *draw=ImGui::GetWindowDrawList(); const int first=draw->IdxBuffer.Size,firstVertex=draw->VtxBuffer.Size;
                // Use geometry AA so coverage is represented by vertices rather than an atlas texture.
                const auto flags=draw->Flags; draw->Flags&=~ImDrawListFlags_AntiAliasedLinesUseTex;
                app.DrawSceneOverlays(20,40,800,600,false);
                draw->Flags=flags; gridVertices=draw->VtxBuffer.Size-firstVertex;
                for(int index=first;index+2<draw->IdxBuffer.Size;index+=3)
                {
                    const auto &a=draw->VtxBuffer[draw->IdxBuffer[index]], &b=draw->VtxBuffer[draw->IdxBuffer[index+1]], &c=draw->VtxBuffer[draw->IdxBuffer[index+2]];
                    const ImU32 rgbMask=~(255u<<IM_COL32_A_SHIFT),gridRgb=IM_COL32(165,174,184,0);
                    if((a.col&rgbMask)!=gridRgb || (b.col&rgbMask)!=gridRgb || (c.col&rgbMask)!=gridRgb) continue;
                    const auto cross=[](ImVec2 p,ImVec2 q,ImVec2 r) { return (q.x-p.x)*(r.y-p.y)-(q.y-p.y)*(r.x-p.x); };
                    const float area=cross(a.pos,b.pos,c.pos); if(std::abs(area)<1e-8f) continue;
                    const int minX=std::clamp(static_cast<int>(std::floor((std::min({a.pos.x,b.pos.x,c.pos.x})-20)*w/800)),0,w-1);
                    const int maxX=std::clamp(static_cast<int>(std::ceil((std::max({a.pos.x,b.pos.x,c.pos.x})-20)*w/800)),0,w-1);
                    const int minY=std::clamp(static_cast<int>(std::floor((std::min({a.pos.y,b.pos.y,c.pos.y})-40)*h/600)),0,h-1);
                    const int maxY=std::clamp(static_cast<int>(std::ceil((std::max({a.pos.y,b.pos.y,c.pos.y})-40)*h/600)),0,h-1);
                    const auto alpha=[](ImU32 col) { return static_cast<float>((col>>IM_COL32_A_SHIFT)&255)/255; };
                    for(int row=minY;row<=maxY;++row) for(int column=minX;column<=maxX;++column)
                    {
                        // Ignore the orientation widget in the upper right.
                        if(column>w*3/4 && row<h/3) continue;
                        const ImVec2 point{20+(column+.5f)*800/w,40+(row+.5f)*600/h};
                        const float u=cross(b.pos,c.pos,point)/area,v=cross(c.pos,a.pos,point)/area,t=1-u-v;
                        if(u<0 || v<0 || t<0) continue;
                        const float opacity=u*alpha(a.col)+v*alpha(b.col)+t*alpha(c.col);
                        auto &pixel=pixels[row*w+column]; pixel+=opacity*(1-pixel);
                    }
                }
                ImGui::End();
            });
            return pixels;
        };
        capture({.3f,2,5}); // Establish ImGui window state before comparing frames.
        for(float height : {2.5f,25.0f})
        {
            const auto before=capture({.3f,height-.0001f,5}),after=capture({.3f,height+.0001f,5});
            float difference=0; for(std::size_t i=0;i<before.size();++i) difference+=std::abs(before[i]-after[i]);
            difference/=static_cast<float>(before.size()); std::cout << "Grid height boundary image delta: " << difference << '\n';
            Check(difference<.002f,"tiny vertical camera movement preserves grid coverage and density across old decade boundaries");
        }
        const auto before=capture({.0999f,2,5}),after=capture({.1001f,2,5});
        float difference=0; for(std::size_t i=0;i<before.size();++i) difference+=std::abs(before[i]-after[i]);
        Check(difference/before.size()<.002f,"grid recentering does not expose moving rectangular edges during camera translation");
        const auto initial=capture({0,2.5f,7},{0,-.173648f,-.984808f});
        float coverage=0,horizon=0;
        for(float pixel : initial) coverage+=pixel;
        Check(coverage/initial.size()>.002f,"initial editor ground grid remains visibly rendered");
        for(int row=59;row<76;++row) {
            float sum=0; for(int column=0;column<180;++column) sum+=initial[row*240+column];
            horizon=std::max(horizon,sum/180);
        }
        std::cout << "Grid horizon peak row coverage: " << horizon << '\n';
        Check(horizon<.2f,"distant subpixel grid lines fade before accumulating into a bright horizon band");
        for(Vec3 direction : {Vec3{0,0,-1},Vec3{1,-.001f,0},Vec3{0,-1,-.001f}}) {
            // A top view must keep the ground beyond the fixture's .1 near plane.
            const auto image=capture({.3f,direction.y<-.5f ? .5f : .05f,5},direction);
            float visible=0; for(float pixel : image) visible+=pixel;
            std::cout << "Grid low camera coverage/vertices: " << visible/image.size() << '/' << gridVertices << '\n';
            Check(visible/image.size()>.0001f && gridVertices<60000,
                  "horizontal and near-ground navigation keeps visible grid geometry within the UI mesh budget");
        }
    }

    void ViewCubeNavigation()
    {
        world.Clear(); const auto entity=world.CreateEntity("ViewCube target");
        world.GetComponent<TransformComponent>(entity)->position={3,1,0};
        app.ResetSceneInteraction(); app.m_selected=entity; app.m_selection={entity}; app.m_undo.clear();
        app.m_showSceneGrid=false; app.m_showSelectionBounds=false;
        app.m_editorCameraPosition={3,1,7}; app.m_editorCameraRotation={};
        Renderer renderer; auto &context=const_cast<EngineContext &>(app.m_engine.GetContext()); context.renderer=&renderer;
        auto &io=ImGui::GetIO(); bool hovered=true;
        const auto draw=[&] { ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({1100,850});
            ImGui::Begin("ViewCube test"); app.DrawSceneOverlays(50,50,800,700,hovered); ImGui::End(); };
        io.AddMousePosEvent(772,128); Frame(draw); Frame(draw);
        const auto before=app.m_editorCameraPosition;
        io.AddMouseButtonEvent(0,true); Frame(draw);
        Check(Length(app.m_editorCameraPosition-before)<.001f,"ViewCube press waits for click or drag without jumping");
        io.AddMousePosEvent(808,148); Frame(draw);
        Check(Length(app.m_editorCameraPosition-before)>.1f,"dragging ViewCube orbits the camera");
        Check(std::abs(Length(app.m_editorCameraPosition-Vec3{3,1,0})-7)<.001f,"ViewCube orbit preserves selected target and distance");
        const auto during=app.m_editorCameraPosition; hovered=false;
        io.AddMousePosEvent(910,220); Frame(draw);
        Check(Length(app.m_editorCameraPosition-during)>.1f,"ViewCube drag stays captured beyond the widget and viewport");
        io.AddMouseButtonEvent(0,false); Frame(draw); const auto released=app.m_editorCameraPosition;
        io.AddMousePosEvent(920,230); Frame(draw);
        Check(Length(app.m_editorCameraPosition-released)<.001f,"releasing ViewCube stops orbit without snapping to a face");
        Check(app.m_selected==entity && app.m_undo.empty(),"ViewCube navigation preserves selection and scene undo");
        const auto click=[&](float x,float y) { io.AddMousePosEvent(x,y); Frame(draw); io.AddMouseButtonEvent(0,true); Frame(draw);
            io.AddMouseButtonEvent(0,false); Frame(draw); };
        hovered=true; app.m_editorCameraPosition={3,1,7}; app.m_editorCameraRotation={};
        io.AddMousePosEvent(772,128); Frame(draw); io.AddMouseButtonEvent(0,true); Frame(draw);
        io.AddMouseButtonEvent(0,false); Frame(draw);
        Check(std::abs(app.m_editorCameraRotation.y)<.01f && std::abs(app.m_editorCameraRotation.x)<.01f,
              "clicking the front face selects the front direction");
        click(795,128);
        Check(std::abs(app.m_editorCameraRotation.y+45)<.01f && std::abs(app.m_editorCameraRotation.x)<.01f,
              "clicking a cube edge aligns to its diagonal direction");
        app.m_editorCameraPosition={3,1,7}; app.m_editorCameraRotation={}; click(795,105);
        Check(std::abs(app.m_editorCameraRotation.y+45)<.01f && std::abs(app.m_editorCameraRotation.x+35.264f)<.02f,
              "clicking a cube corner aligns to its isometric direction");
        app.m_editorCameraPosition={3,1,7}; app.m_editorCameraRotation={}; io.AddMousePosEvent(772,128); Frame(draw);
        io.AddMouseButtonEvent(0,true); Frame(draw); io.AddFocusEvent(false); Frame(draw);
        io.AddFocusEvent(true); io.AddMouseButtonEvent(0,false); Frame(draw);
        const auto restored=app.m_editorCameraPosition; io.AddMousePosEvent(800,140); Frame(draw);
        Check(Length(app.m_editorCameraPosition-restored)<.001f,"focus loss cancels ViewCube capture");
        app.m_editorCameraPosition={3,1,7}; app.m_editorCameraRotation={}; io.AddMousePosEvent(772,128); Frame(draw);
        io.AddMouseButtonEvent(0,true); Frame(draw); ImGui::SetNextWindowPos({0,0});
        Frame([&] { ImGui::Begin("ViewCube test"); ImGui::OpenPopup("Block cube");
            if(ImGui::BeginPopup("Block cube")) { app.DrawSceneOverlays(50,50,800,700,true); ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
            ImGui::End(); });
        io.AddMouseButtonEvent(0,false); Frame(draw);
        Check(!app.m_viewCubeActive && std::abs(app.m_editorCameraRotation.y)<.01f,"popup cancels ViewCube capture without moving the camera");
        context.renderer=nullptr; app.m_showSceneGrid=app.m_showSelectionBounds=true; app.ResetSceneInteraction(); world.Clear();
    }

    void P1OrbitStability()
    {
        world.Clear(); const auto e=world.CreateEntity("Off-center orbit");
        world.GetComponent<TransformComponent>(e)->position={10,2,0}; app.m_selected=e; app.m_selection={e};
        app.m_editorCameraPosition={0,2,7}; app.m_editorCameraRotation={0,0,0};
        const auto initialPosition=app.m_editorCameraPosition; const auto initialRotation=app.m_editorCameraRotation;
        const auto draw=[&]{ app.UpdateSceneCamera(true,true); }; auto &io=ImGui::GetIO();
        io.AddMousePosEvent(300,300); Frame(draw);
        io.AddKeyEvent(ImGuiMod_Alt,true); io.AddMouseButtonEvent(0,true); Frame(draw); Frame(draw);
        Check(Length(app.m_editorCameraPosition-initialPosition)<.0001f && Length(app.m_editorCameraRotation-initialRotation)<.0001f,
              "holding Alt-orbit stationary does not snap to an off-center selection");
        io.AddMousePosEvent(320,300); Frame(draw);
        Check(std::abs(app.m_editorCameraRotation.y-3.6f)<.01f,"off-center orbit applies mouse angle without an orientation jump");
        io.AddMouseButtonEvent(0,false); io.AddKeyEvent(ImGuiMod_Alt,false); Frame(draw);
        app.ResetSceneInteraction(); app.m_editorCameraRotation={-10,0,0}; world.Clear();
    }

    void P1SceneDrag()
    {
        const auto root=std::filesystem::absolute("Saved/P1SceneDrag")/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto assets=root/"assets"; std::filesystem::create_directories(assets);
        PlatformFile::WriteAllText(assets/"a.mesh",PlatformFile::ReadAllText("assets/player.mesh"));
        ResourceManager resources; resources.Initialize(nullptr,assets); Renderer renderer; renderer.SetResourceManager(&resources);
        auto gpu=std::make_unique<SdlGlRenderDevice>(256,128,"Viewport drag transaction"); auto *device=gpu.get();
        Check(renderer.Initialize(std::move(gpu)),"viewport drop fixture initializes real render target");
        SDL_HideWindow(static_cast<SDL_Window *>(device->GetNativeWindow())); device->EnableSceneRenderTarget(true,256,128);
        auto &context=const_cast<EngineContext &>(app.m_engine.GetContext()); context.resourceManager=&resources; context.renderer=&renderer;
        auto &settings=const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings()); const auto oldSettings=settings;
        settings.projectFile=root/"Drag.tinyproject"; settings.assetDirectory="assets"; app.m_device=device;
        world.Clear(); app.ResetSceneInteraction(); app.m_undo.clear(); app.m_redo.clear(); app.m_sceneDirty=false;
        app.m_assetFolder=assets; app.m_assetViewMode=EditorApp::AssetViewMode::List; app.m_assetSearch.fill(0); app.m_assetTypeFilters.clear();
        app.m_showAssets=true; app.m_sceneMaximized=false; app.m_editorCameraPosition={0,2,7}; app.m_editorCameraRotation={-20,0,0};
        const auto draw=[&] {
            renderer.SetCameraOverride(true,app.m_editorCameraPosition,app.m_editorCameraRotation,app.m_editorCameraFov);
            renderer.RenderFrame(world); app.DrawSceneView(); app.DrawAssets();
        };
        Frame(draw); Frame(draw);
        ImGuiWindow *browser=nullptr; for(auto *candidate:ImGui::GetCurrentContext()->Windows)
            if(candidate->ParentWindow==ImGui::FindWindowByName("Assets") &&
                std::string(candidate->Name).find("AssetBrowserItems")!=std::string::npos) browser=candidate;
        Check(browser!=nullptr,"viewport drag fixture exposes asset rows");
        auto *table=ImGui::GetCurrentContext()->Tables.GetByKey(browser->GetID("AssetList"));
        Check(table!=nullptr,"viewport drag fixture exposes actual list table");
        const float rowHeight=ImGui::GetFontSize()+ImGui::GetStyle().CellPadding.y*2;
        const ImVec2 source{table->Columns[0].MinX+24,table->OuterRect.Min.y+rowHeight*1.5f};
        const auto *scene=ImGui::FindWindowByName("Scene View");
        auto &io=ImGui::GetIO();
        const ImVec2 cubeHome{scene->WorkRect.Min.x+device->GetSceneTargetWidth()/app.m_dpiScale-130,
            scene->DC.CursorMaxPos.y-device->GetSceneTargetHeight()/app.m_dpiScale+25};
        io.AddMousePosEvent(cubeHome.x,cubeHome.y); Frame(draw); Frame(draw);
        io.AddMouseButtonEvent(0,true); Frame(draw);
        Check(app.m_viewCubeActive,"ViewCube captures input above the actual viewport image");
        io.AddMouseButtonEvent(0,false); Frame(draw); Frame(draw);
        Check(std::abs(app.m_editorCameraRotation.y+35)<.01f,"ViewCube home click completes above the actual viewport image");
        const ImVec2 cubeCenter{cubeHome.x+52,cubeHome.y+53};
        io.AddMousePosEvent(cubeCenter.x,cubeCenter.y); Frame(draw); io.AddMouseButtonEvent(0,true); Frame(draw);
        const auto orbitBefore=app.m_editorCameraPosition;
        io.AddMousePosEvent(cubeCenter.x+20,cubeCenter.y+10); Frame(draw);
        Check(Length(app.m_editorCameraPosition-orbitBefore)>.1f,"ViewCube drag works with real viewport input ownership");
        io.AddMouseButtonEvent(0,false); Frame(draw);
        app.m_editorCameraPosition={0,2,7}; app.m_editorCameraRotation={-20,0,0}; Frame(draw); Frame(draw);
        Check(scene && scene->ScrollMax.y<1,"viewport overlay does not introduce a vertical scrollbar");
        app.m_viewportRenderMode=ViewportRenderMode::Unlit; Frame(draw); Frame(draw);
        Check(renderer.GetFrameView().renderMode==ViewportRenderMode::Unlit,"viewport selection reaches renderer frame state");
        renderer.SetCameraOverride(false); renderer.RenderFrame(world);
        Check(renderer.GetFrameView().renderMode==ViewportRenderMode::Lit,"runtime camera always restores normal Lit rendering");
        app.m_viewportRenderMode=ViewportRenderMode::Lit; Frame(draw); Frame(draw);
        const ImVec2 target{scene->InnerRect.Min.x+scene->InnerRect.GetWidth()*.7f,scene->InnerRect.Max.y-30};
        io.AddMousePosEvent(source.x,source.y); Frame(draw);
        io.AddMouseButtonEvent(0,true); Frame(draw); io.AddMousePosEvent(source.x+20,source.y); Frame(draw);
        Check(ImGui::GetDragDropPayload()!=nullptr,"real asset mouse gesture starts a payload");
        io.AddMousePosEvent(target.x,target.y); Frame(draw); Frame(draw);
        Check(world.GetEntityManager().GetAliveEntities().empty() && app.m_undo.empty() && !app.m_sceneDirty,
              "hovering placement preview never creates an entity or undo record");
        Check(!app.m_scenePreviewAsset.empty() && app.m_scenePreviewBoundsValid,"hovering viewport resolves ghost bounds for the dragged mesh");
        io.AddMouseButtonEvent(0,false); Frame(draw); Frame(draw);
        Check(world.GetEntityManager().GetAliveEntities().size()==1 && app.m_undo.size()==1,
              "drop delivery creates exactly one mesh and one undo record");
        const auto *transform=world.GetComponent<TransformComponent>(app.m_selected);
        Check(transform && std::abs(transform->position.y)<.001f && std::abs(transform->position.x)>.1f,
              "delivered mesh uses the preview pointer position instead of the origin");
        Check(PlatformFile::WriteAllText(assets/"z.matgraph", MakeBuiltinPbrDefinition().graph.Serialize()), "create node material for viewport drag");
        resources.GetAssetRegistry().Scan(assets); Frame(draw); Frame(draw);
        table=ImGui::GetCurrentContext()->Tables.GetByKey(browser->GetID("AssetList"));
        const ImVec2 materialSource{table->Columns[0].MinX+24,table->OuterRect.Min.y+rowHeight*2.5f};
        io.AddMousePosEvent(materialSource.x,materialSource.y); Frame(draw); io.AddMouseButtonEvent(0,true); Frame(draw);
        io.AddMousePosEvent(materialSource.x+20,materialSource.y); Frame(draw);
        Check(ImGui::GetDragDropPayload()!=nullptr, "real node material drag creates an asset payload");
        const auto *materialPayload = ImGui::GetDragDropPayload();
        Check(std::filesystem::u8path(static_cast<const char *>(materialPayload->Data)).filename()=="z.matgraph", "viewport drag carries the actual node material path");
        // Drop inside the triangle, away from the ground-placement pivot on its edge.
        const auto point=transform->position+Vec3{.25f,.25f,0}; const auto matrix=renderer.GetFrameView().projection*renderer.GetFrameView().view;
        const float clipX=matrix.At(0,0)*point.x+matrix.At(0,1)*point.y+matrix.At(0,2)*point.z+matrix.At(0,3);
        const float clipY=matrix.At(1,0)*point.x+matrix.At(1,1)*point.y+matrix.At(1,2)*point.z+matrix.At(1,3);
        const float clipW=matrix.At(3,0)*point.x+matrix.At(3,1)*point.y+matrix.At(3,2)*point.z+matrix.At(3,3);
        const ImVec2 meshTarget{scene->WorkRect.Min.x+(clipX/clipW*.5f+.5f)*device->GetSceneTargetWidth()/app.m_dpiScale,
            scene->WorkRect.Max.y-(.5f+clipY/clipW*.5f)*device->GetSceneTargetHeight()/app.m_dpiScale};
        io.AddMousePosEvent(meshTarget.x,meshTarget.y); Frame(draw); Frame(draw);
        io.AddMouseButtonEvent(0,false); Frame(draw); Frame(draw);
        Check(world.GetEntityManager().GetAliveEntities().size()==1 && app.m_undo.size()==2 &&
              world.GetComponent<MeshComponent>(app.m_selected)->materialPath=="assets/z.matgraph",
              "dropping node material on scene mesh assigns it without spawning an entity");
        app.Undo();
        Check(world.GetComponent<MeshComponent>(*world.GetEntityManager().GetAliveEntities().begin())->materialPath=="assets/default.mat", "one undo reverses viewport node material assignment");
        app.Undo(); Check(world.GetEntityManager().GetAliveEntities().empty(),"one undo reverses the delivered viewport drop");
        app.ResetSceneInteraction(); settings=oldSettings; app.m_device=nullptr; context.renderer=nullptr; context.resourceManager=nullptr;
        renderer.Shutdown(); resources.Shutdown(); app.m_editorCameraRotation={-10,0,0};
    }

    void Shortcuts()
    {
        // Command tests start with one explicit owner; browser gestures can retain panel focus.
        app.m_assetsFocused = false; app.m_shaderFocused = false; app.m_assetDeleteRequested = false;
        app.m_materialGraphEditor.Hide(); app.m_materialAssetEditor.Hide();
        const Entity entity = world.CreateEntity("Selected");
        app.m_selected = entity;
        app.m_selection = {entity};
        world.GetComponent<TransformComponent>(entity)->position = {14.0f, 2.0f, -6.0f};
        app.m_editorCameraPosition = {0.0f, 2.5f, 7.0f};
        app.m_sceneCommandsFocused = false;
        Key(ImGuiKey_F);
        Check(app.m_editorCameraPosition.x == 0.0f, "F in another panel does not move the scene camera");
        app.m_sceneCommandsFocused = true;
        Key(ImGuiKey_F, false, false, true);
        Check(app.m_editorCameraPosition.x == 0.0f, "text editing owns the F key");
        Key(ImGuiKey_F);
        Check(std::abs(app.m_editorCameraPosition.x - 14.0f) < 0.01f,
              "F frames the selected entity when scene commands have focus");
        app.m_sceneCommandsFocused = false;
        Key(ImGuiKey_Delete);
        Check(world.IsAlive(entity) && app.m_undo.empty(), "Delete in another panel does not delete scene objects");
        app.m_sceneCommandsFocused = true;
        Key(ImGuiKey_Delete, false, false, true);
        Check(world.IsAlive(entity), "text editing owns Delete");
        Key(ImGuiKey_Delete, false, false, false, true);
        Check(world.IsAlive(entity), "popup at any stack level blocks scene Delete");
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true); Frame();
        app.m_sceneOrbitActive = true;
        Key(ImGuiKey_Delete);
        Check(world.IsAlive(entity), "camera gesture blocks scene commands");
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false); Frame();
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

    struct BrowserFixture
    {
        EditorInteractionTests &tests;
        ResourceManager resources;
        ProjectSettings previous;
        std::filesystem::path root;
        BrowserFixture(EditorInteractionTests &owner, const char *name) : tests(owner)
        {
            root = std::filesystem::absolute("Saved/AssetBrowserRegression") / name /
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) / "assets";
            std::filesystem::create_directories(root / "Subfolder");
            PlatformFile::WriteAllText(root / "a.mat", "color 1 0 0 1\n");
            PlatformFile::WriteAllText(root / "b.mat", "color 0 1 0 1\n");
            PlatformFile::WriteAllText(root / "c.ppm", "P3\n1 1\n255\n255 255 255\n");
            PlatformFile::WriteAllText(root / "d.mesh", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
            resources.Initialize(nullptr, root);
            const_cast<EngineContext &>(tests.app.m_engine.GetContext()).resourceManager = &resources;
            auto &settings = const_cast<ProjectSettings &>(tests.app.m_engine.GetProjectSettings());
            previous = settings; settings.projectFile = root.parent_path() / "Browser.tinyproject"; settings.assetDirectory = "assets";
            tests.app.m_showAssets = true; tests.app.m_assetFolder = root;
            tests.app.m_assetViewMode = EditorApp::AssetViewMode::List;
            tests.app.m_assetSearch.fill(0); tests.app.m_selectedAsset.clear(); tests.app.m_selectedAssets.clear();
            tests.Frame([&] { tests.app.DrawAssets(); }); tests.Frame([&] { tests.app.DrawAssets(); });
        }
        ~BrowserFixture()
        {
            ImGui::ClosePopupsOverWindow(nullptr, false);
            tests.app.m_showAssetPreview = false;
            tests.app.m_showAssetReferences = false;
            tests.app.m_selectedAsset.clear(); tests.app.m_selectedAssets.clear();
            const_cast<EngineContext &>(tests.app.m_engine.GetContext()).resourceManager = nullptr;
            const_cast<ProjectSettings &>(tests.app.m_engine.GetProjectSettings()) = previous;
            resources.Shutdown();
        }
    };

    ImGuiWindow *BrowserItems()
    {
        for (auto *window : ImGui::GetCurrentContext()->Windows)
            if (window->ParentWindow == ImGui::FindWindowByName("Assets") &&
                std::string(window->Name).find("AssetBrowserItems") != std::string::npos) return window;
        throw std::runtime_error("Asset browser items window is missing");
    }

    void MaterialAssignmentRegression()
    {
        BrowserFixture fixture(*this, "MaterialAssignment");
        const auto graphPath = (fixture.root / "NodeMaterial.matgraph").generic_u8string();
        const auto definition = MakeBuiltinPbrDefinition();
        Check(PlatformFile::WriteAllText(std::filesystem::u8path(graphPath), definition.graph.Serialize()), "write assignable node material fixture");
        fixture.resources.GetAssetRegistry().Scan(fixture.root);
        world.Clear(); app.ResetSceneInteraction(); app.m_undo.clear(); app.m_redo.clear(); app.m_sceneDirty = false;
        const auto a = world.CreateEntity("Material target A"), b = world.CreateEntity("Material target B");
        for (const auto entity : {a,b}) world.AddComponent(entity, MeshComponent{"assets/d.mesh", "assets/a.mat"});
        app.m_selected = a; app.m_selection = {a,b};
        const auto &mesh = *ReflectionRegistry::Get().FindComponent("Mesh Renderer");
        const auto field = std::find_if(mesh.properties.begin(), mesh.properties.end(), [](const auto &p) { return p.assetType == "Material"; });
        Check(field != mesh.properties.end(), "mesh exposes material reference field");
        Check(app.ApplyInspectorAsset(mesh, *field, graphPath, 7241), "material picker and drop accept node material assets");
        Check(world.GetComponent<MeshComponent>(a)->materialPath == "assets/NodeMaterial.matgraph" &&
            world.GetComponent<MeshComponent>(b)->materialPath == "assets/NodeMaterial.matgraph" && app.m_undo.size() == 1,
            "node material assignment updates all selected meshes with one portable undo transaction");
        app.Undo();
        for (auto entity : world.GetEntityManager().GetAliveEntities())
            Check(world.GetComponent<MeshComponent>(entity)->materialPath == "assets/a.mat", "undo restores prior mesh materials");
        MaterialSystem system(fixture.resources);
        const auto proxy = system.BuildProxy(graphPath);
        Check(proxy && proxy->compiled, "node material produces a runtime material proxy");
        Renderer renderer; renderer.SetResourceManager(&fixture.resources);
        auto &context = const_cast<EngineContext &>(app.m_engine.GetContext()); context.renderer = &renderer;
        app.m_selection.clear();
        for (auto entity : world.GetEntityManager().GetAliveEntities()) app.m_selection.push_back(entity);
        app.m_selected = app.m_selection.front(); app.m_undo.clear(); app.m_redo.clear();
        app.SelectAsset(graphPath, false); app.OpenAsset(*fixture.resources.GetAssetRegistry().Find(graphPath));
        const auto draw = [&] { app.DrawMaterialGraphEditor(); }; Frame(draw); Frame(draw);
        app.m_materialGraphEditor.AddNode(MaterialNodeType::TexCoord, "UV");
        auto *window = ImGui::FindWindowByID(ImHashStr("###MaterialGraphEditor"));
        Check(window != nullptr, "node material opens its authoring document");
        ImGui::ActivateItemByID(window->GetID("###ApplyMaterial")); Frame(draw); Frame(draw);
        Check(!app.m_materialGraphEditor.IsDirty() && app.m_undo.size() == 1 && app.m_selectedAsset.empty(), "material document saves and applies draft to selected meshes through editor integration");
        for (auto entity : app.m_selection) Check(world.GetComponent<MeshComponent>(entity)->materialPath == "assets/NodeMaterial.matgraph", "document Apply assigns node material to every selected mesh");
        Check(app.ApplyMaterialToMeshes(graphPath, app.m_selection) && app.m_undo.size() == 1, "reapplying current material succeeds without creating an undo record");
        ImGui::FocusWindow(window); Frame(draw); app.m_assetsFocused = false; app.m_sceneCommandsFocused = true;
        Key(ImGuiKey_Delete);
        Check(world.GetEntityManager().GetAliveEntities().size() == 2, "material document focus prevents scene Delete even with stale scene panel focus");
        app.m_materialGraphEditor.Hide(); app.m_materialAssetEditor.Hide(); app.m_materialGraphEditor.SetSaveRequest({}); app.m_materialGraphEditor.SetApplyRequest({});
        app.m_editingAsset.clear(); app.m_openMaterialGraphAsset.clear(); context.renderer = nullptr; renderer.Shutdown();
        app.ResetSceneInteraction();
    }

    void AssetDeleteShortcutRegression()
    {
        BrowserFixture fixture(*this, "DeleteShortcut");
        world.Clear(); app.ResetSceneInteraction(); app.m_undo.clear();
        const auto entity = world.CreateEntity("Must survive asset Delete");
        app.m_selected = entity; app.m_selection = {entity};
        const auto path = (fixture.root / "a.mat").generic_u8string(); app.SelectAsset(path, false);
        app.m_assetsFocused = true; app.m_sceneCommandsFocused = false;
        Key(ImGuiKey_Delete, false, false, true);
        Frame([&] { app.DrawAssets(); });
        Check(!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel), "asset Delete respects text input ownership");
        app.m_assetsFocused = true; Key(ImGuiKey_Delete);
        Frame([&] { app.DrawAssets(); });
        Check(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel), "asset Delete opens a recoverable deletion confirmation");
        Check(world.IsAlive(entity) && std::filesystem::exists(std::filesystem::u8path(path)), "asset Delete leaves files and entities intact until confirmation");
        const auto info = fixture.resources.GetAssetRegistry().Find(path); Check(info.has_value(), "deleted asset starts with persistent metadata");
        BrowserActivate(BrowserPopup(), "###ConfirmDeleteAssets");
        Check(!std::filesystem::exists(std::filesystem::u8path(path)) && !app.m_assetDeleteHistory.empty() && world.IsAlive(entity), "confirmed asset Delete moves file to trash without deleting scene entity");
        app.m_assetTrashProject.clear(); app.m_assetDeleteHistory.clear(); app.LoadAssetTrashHistory();
        Check(app.m_assetDeleteHistory.size() == 1, "project trash manifests remain discoverable after editor state is reloaded");
        app.m_assetsFocused = true; Key(ImGuiKey_Z, true);
        Check(std::filesystem::exists(std::filesystem::u8path(path)) && fixture.resources.GetAssetRegistry().Find(path)->id == info->id,
            "asset panel Ctrl+Z restores original file and GUID");
        world.AddComponent(entity, MeshComponent{"assets/d.mesh", "assets/a.mat"});
        app.m_assetsFocused = true; app.SelectAsset(path, false); Key(ImGuiKey_Delete); Frame([&] { app.DrawAssets(); });
        BrowserActivate(BrowserPopup(), "###ConfirmDeleteAssets");
        Check(std::filesystem::exists(std::filesystem::u8path(path)) && !app.AssetDeletionBlockers({path}).empty(), "current unsaved scene reference blocks asset deletion");
        ImGui::ClosePopupsOverWindow(nullptr, false);
        world.GetComponent<MeshComponent>(entity)->materialPath.clear();
        const auto before = app.m_sceneDocuments;
        world.GetComponent<MeshComponent>(entity)->materialPath = "assets/a.mat";
        const auto snapshot = SceneSerializer::Serialize(world); world.GetComponent<MeshComponent>(entity)->materialPath.clear();
        app.m_sceneDocuments.push_back({fixture.root / "Other.scene", snapshot, true});
        Check(!app.AssetDeletionBlockers({path}).empty(), "inactive unsaved scene tab protects its material references"); app.m_sceneDocuments = before;
        auto &settings = const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings()); settings.environmentTexture = "assets/c.ppm";
        Check(!app.AssetDeletionBlockers({(fixture.root / "c.ppm").generic_u8string()}).empty(), "project environment reference blocks texture deletion");
        PlatformFile::WriteAllText(fixture.root / "draft.frag", "void main() {}\n");
        PlatformFile::WriteAllText(fixture.root / "shared.glsl", "vec3 tint = vec3(1);\n");
        fixture.resources.GetAssetRegistry().Scan(fixture.root);
        app.m_openShaderAsset = (fixture.root / "draft.frag").generic_u8string(); app.m_savedShaderSource = "void main() {}\n";
        const std::string shaderDraft = "#include \"shared.glsl\"\nvoid main() {}\n";
        app.m_shaderSource.assign(shaderDraft.begin(), shaderDraft.end()); app.m_shaderSource.push_back('\0');
        Check(!app.AssetDeletionBlockers({(fixture.root / "shared.glsl").generic_u8string()}).empty(), "unsaved shader include blocks deletion of its dependency");
        app.m_openShaderAsset.clear(); app.m_shaderSource.clear(); app.m_savedShaderSource.clear();
        const auto previousUndo = app.m_undo, previousRedo = app.m_redo;
        app.m_undo = {snapshot};
        Check(!app.AssetDeletionBlockers({path}).empty(), "scene undo history protects material references it can restore");
        app.m_undo.clear(); app.m_redo = {snapshot};
        Check(!app.AssetDeletionBlockers({path}).empty(), "scene redo history protects material references it can restore");
        app.m_redo.clear();
        app.m_sceneDocuments.push_back({fixture.root / "History.scene", SceneSerializer::Serialize(world), true, {snapshot}, {}});
        Check(!app.AssetDeletionBlockers({path}).empty(), "inactive scene tab undo history protects its retained asset references");
        app.m_sceneDocuments.pop_back(); app.m_undo = previousUndo; app.m_redo = previousRedo;
        app.m_undo.clear(); app.ResetSceneInteraction(); app.m_assetsFocused = false;
    }

    void BrowserActivate(ImGuiWindow *window, const char *label)
    {
        ImGui::ActivateItemByID(window->GetID(label));
        const auto draw = [&] { app.DrawAssets(); app.DrawAssetReferenceViewer(); };
        Frame(draw); Frame(draw);
    }

    void MaterialTrashCacheRegression()
    {
        BrowserFixture fixture(*this, "MaterialTrashCache"); world.Clear(); app.ResetSceneInteraction();
        const auto path = (fixture.root / "NewMaterial.matgraph").generic_u8string(); auto definition = MakeBuiltinPbrDefinition();
        const auto write = [&](float roughness) {
            for (auto &parameter : definition.parameters) if (parameter.name == "Roughness") parameter.defaultValue = roughness;
            definition.graph.GetParameters() = definition.parameters;
            return PlatformFile::WriteAllText(std::filesystem::u8path(path), definition.graph.Serialize());
        };
        Check(write(.2f), "write material before caching and deletion"); fixture.resources.GetAssetRegistry().Scan(fixture.root);
        Renderer renderer; renderer.SetResourceManager(&fixture.resources);
        auto &context = const_cast<EngineContext &>(app.m_engine.GetContext()); context.renderer = &renderer;
        const auto old = renderer.GetMaterialSystem().BuildProxy(path);
        const auto roughness = [](const auto &proxy) {
            for (const auto &slot : proxy->compiled->layout.numericSlots) if (slot.name == "Roughness") return proxy->numericSlots[slot.slot].x;
            return -1.f;
        };
        Check(old && std::abs(roughness(old)-.2f)<.001f, "cache original material values");
        app.SelectAsset(path, false); app.RequestDeleteAssets(); Frame([&] { app.DrawAssets(); }); BrowserActivate(BrowserPopup(), "###ConfirmDeleteAssets");
        Check(!std::filesystem::exists(std::filesystem::u8path(path)), "delete cached unused material through actual asset dialog");
        Check(write(.9f), "recreate material at the same available path"); fixture.resources.GetAssetRegistry().Scan(fixture.root);
        const auto replacement = renderer.GetMaterialSystem().BuildProxy(path);
        Check(replacement && std::abs(roughness(replacement)-.9f)<.001f, "reused deleted material path resolves the new contents instead of cached deleted values");
        Check(std::abs(roughness(old)-.2f)<.001f, "evicting deleted material preserves retained render snapshots");
        context.renderer = nullptr; renderer.Shutdown();
    }

    ImGuiWindow *BrowserPopup()
    {
        auto &stack = ImGui::GetCurrentContext()->OpenPopupStack;
        return stack.empty() ? nullptr : stack.back().Window;
    }

    void BrowserInput(ImGuiWindow *window, const char *label, const char *text)
    {
        BrowserActivate(window, label);
        const auto draw = [&] { app.DrawAssets(); };
        auto &io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_A, true); Frame(draw);
        io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame(draw);
        io.AddInputCharactersUTF8(text); Frame(draw);
    }

    void AssetBrowserMenus()
    {
        BrowserFixture fixture(*this, "menus");
        app.m_assetFolder = fixture.root / "Subfolder";
        const auto draw = [&] { app.DrawAssets(); };
        Frame(draw); Frame(draw);
        auto *items = BrowserItems(); auto &io = ImGui::GetIO();
        const auto point = items->InnerRect.GetCenter();
        io.AddMousePosEvent(point.x, point.y); Frame(draw);
        io.AddMouseButtonEvent(1, true); Frame(draw);
        io.AddMouseButtonEvent(1, false); Frame(draw); Frame(draw);
        Check(BrowserPopup() != nullptr, "right click on empty browser space opens the creation menu");
        BrowserActivate(BrowserPopup(), "New Folder");
        auto *modal = ImGui::FindWindowByName("New Folder###NewAssetFolder");
        Check(modal && modal->Active, "new folder from the context menu opens its name dialog");
        io.AddInputCharactersUTF8("新建 测试"); Frame(draw);
        io.AddKeyEvent(ImGuiKey_Enter, true); Frame(draw);
        io.AddKeyEvent(ImGuiKey_Enter, false); Frame(draw); Frame(draw);
        const auto created = fixture.root / "Subfolder" / std::filesystem::u8path("新建 测试");
        Check(std::filesystem::is_directory(created), "context creation writes a UTF-8 folder inside the current subfolder");
        Check(app.m_assetFolder.lexically_normal() == created.lexically_normal(), "created folder becomes the current selected folder");
        BrowserActivate(ImGui::FindWindowByName("Assets"), "Create...");
        Check(BrowserPopup() != nullptr, "toolbar and right click share the creation menu");
        BrowserActivate(BrowserPopup(), "Material");
        Check(std::filesystem::exists(created / "NewMaterial.matgraph") &&
              std::filesystem::u8path(app.m_selectedAsset).parent_path().lexically_normal() == created.lexically_normal(),
              "toolbar creates and selects a material graph in the current directory");
        Check(!std::filesystem::exists(fixture.root / "NewMaterial.matgraph"), "nested creation does not write to the asset root");
        BrowserActivate(ImGui::FindWindowByName("Assets"), "Create...");
        BrowserActivate(BrowserPopup(), "+ Blend Tree");
        BlendTree1D blend;
        Check(LoadBlendTreeAsset(created / "NewBlendTree.blendtree", blend) && blend.samples.size() == 1 &&
              blend.samples.front().clipPath == "assets/Subfolder/新建 测试/NewAnimationClip.skanim",
              "blend trees created in Unicode directories serialize a valid UTF-8 clip reference");
        BrowserActivate(ImGui::FindWindowByName("Assets"), "Create...");
        BrowserActivate(BrowserPopup(), "+ Animation Graph");
        AnimationGraph animation;
        Check(LoadAnimationGraphAsset(created / "NewAnimationGraph.animgraph", animation) && animation.states.size() == 1 &&
              animation.states.front().motionPath == "assets/Subfolder/新建 测试/NewAnimationClip.skanim",
              "animation graphs reuse Unicode starter clips without corrupting dependency paths");
        const auto animationInfo = fixture.resources.GetAssetRegistry().Find((created / "NewAnimationGraph.animgraph").generic_u8string());
        Check(animationInfo && animationInfo->dependencies.size() == 1 &&
              animationInfo->dependencies.front() == "assets/Subfolder/新建 测试/NewAnimationClip.skanim",
              "new animation assets publish their dependencies under the UTF-8 registry key");
        BrowserActivate(ImGui::FindWindowByName("Assets"), "Create...");
        BrowserActivate(BrowserPopup(), "New Folder");
        modal = ImGui::FindWindowByName("New Folder###NewAssetFolder");
        BrowserInput(modal, "Name", "../escape");
        BrowserActivate(modal, "Create");
        Check(modal->Active && !app.m_assetFolderError.empty() && !std::filesystem::exists(created.parent_path() / "escape"),
              "folder name validation rejects parent traversal without closing the dialog");
        std::filesystem::create_directory(created / "Existing");
        BrowserInput(modal, "Name", "Existing");
        BrowserActivate(modal, "Create");
        Check(modal->Active && !app.m_assetFolderError.empty(), "new folder never overwrites an existing directory");
        BrowserActivate(modal, "Cancel");
        app.m_assetFolder = fixture.root; app.m_assetViewMode = EditorApp::AssetViewMode::Grid;
        Frame(draw); Frame(draw);
        items = BrowserItems();
        io.AddMousePosEvent(items->DC.CursorStartPos.x + 32, items->DC.CursorStartPos.y + 32); Frame(draw);
        io.AddMouseButtonEvent(1, true); Frame(draw); io.AddMouseButtonEvent(1, false); Frame(draw); Frame(draw);
        Check(BrowserPopup() != nullptr, "folder tiles have their own right click creation menu");
        BrowserActivate(BrowserPopup(), "Material");
        Check(std::filesystem::exists(fixture.root / "Subfolder/NewMaterial.matgraph"),
              "right click on a folder creates assets inside that folder");
        Check(fixture.resources.GetAssetRegistry().Find((fixture.root / "a.mat").generic_u8string()).has_value(),
              "creating an asset in a subfolder retains assets elsewhere in the registry");
        app.m_assetViewMode = EditorApp::AssetViewMode::List;
        app.m_assetFolder = fixture.root;
        Frame(draw); Frame(draw);
        ImGuiWindow *folders = nullptr;
        for (auto *window : ImGui::GetCurrentContext()->Windows)
            if (window->ParentWindow == ImGui::FindWindowByName("Assets") &&
                std::string(window->Name).find("AssetFolderTree") != std::string::npos) folders = window;
        Check(folders != nullptr, "wide browser exposes its folder tree");
        io.AddMousePosEvent(folders->DC.CursorStartPos.x + 60,
                            folders->DC.CursorStartPos.y + ImGui::GetTextLineHeightWithSpacing() * 1.5f); Frame(draw);
        io.AddMouseButtonEvent(1, true); Frame(draw); io.AddMouseButtonEvent(1, false); Frame(draw); Frame(draw);
        Check(BrowserPopup() != nullptr, "folder tree entries expose the shared right click creation actions");
        BrowserActivate(BrowserPopup(), "Material");
        Check(std::filesystem::exists(fixture.root / "Subfolder/NewMaterial1.matgraph"),
              "folder tree creation targets the clicked directory and chooses a unique filename");
    }

    void AssetBrowserFilters()
    {
        BrowserFixture fixture(*this, "filters");
        auto *assets = ImGui::FindWindowByName("Assets");
        BrowserActivate(assets, "Filters");
        Check(BrowserPopup() != nullptr, "filters opens the type list directly with one activation");
        BrowserActivate(BrowserPopup(), "Material");
        auto rows = [&] {
            auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(BrowserItems()->GetID("AssetList"));
            return table ? table->CurrentRow : -1;
        };
        Check(rows() == 2, "material checkbox immediately restricts browser rows to both material assets");
        BrowserActivate(BrowserPopup(), "Texture");
        Check(rows() == 3, "multiple type filters include the union of material and texture assets");
        ImGui::ClosePopupsOverWindow(nullptr, false); Frame([&] { app.DrawAssets(); });
        const auto tag = ImHashStr("###RemoveFilter", 0, ImHashStr("Material", 0, assets->ID));
        ImGui::ActivateItemByID(tag); Frame([&] { app.DrawAssets(); }); Frame([&] { app.DrawAssets(); });
        Check(rows() == 1, "removing one visible filter tag leaves the other type filter active");
        BrowserActivate(assets, "Display");
        Check(BrowserPopup() != nullptr, "display options has its own popup independent of type filters");
        BrowserActivate(BrowserPopup(), "Grid");
        Check(app.m_assetViewMode == EditorApp::AssetViewMode::Grid, "display popup changes the browser view");
        app.m_assetViewMode = EditorApp::AssetViewMode::List;
        ImGui::ClosePopupsOverWindow(nullptr, false); Frame([&] { app.DrawAssets(); });
        Check(rows() == 1, "changing display mode preserves the selected type filters");
        app.LocateAsset((fixture.root / "a.mat").generic_u8string()); Frame([&] { app.DrawAssets(); });
        Check(rows() == 4, "locating an asset clears all type filters and reveals the target");
    }

    void AssetBrowserReferences()
    {
        BrowserFixture fixture(*this, "references");
        auto &registry = fixture.resources.GetAssetRegistry();
        const auto a = (fixture.root / "a.mat").generic_u8string();
        const auto b = (fixture.root / "b.mat").generic_u8string();
        registry.SetDependencies(a, {"c.ppm", "Missing.png"});
        registry.SetDependencies(b, {"a.mat"});
        app.SelectAsset(b, false);
        const auto draw = [&] { app.DrawAssets(); app.DrawAssetReferenceViewer(); };
        Frame(draw); Frame(draw);
        auto *items = BrowserItems();
        const auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(items->GetID("AssetList"));
        const float height = ImGui::GetFontSize() + ImGui::GetStyle().CellPadding.y * 2;
        const ImVec2 point{table->Columns[0].MinX + 24, table->OuterRect.Min.y + height * 1.5f};
        auto &io = ImGui::GetIO(); io.AddMousePosEvent(point.x, point.y); Frame(draw);
        io.AddMouseButtonEvent(1, true); Frame(draw);
        io.AddMouseButtonEvent(1, false); Frame(draw); Frame(draw);
        Check(BrowserPopup() != nullptr, "asset right click opens its own context menu");
        BrowserActivate(BrowserPopup(), "Reference Viewer");
        auto *viewer = ImGui::FindWindowByName("Asset References###AssetReferences");
        Check(viewer && viewer->Active, "asset context menu opens an independent reference graph window");
        Check(app.m_selectedAsset == a, "right click operations target the clicked asset instead of the previous selection");
        const auto &graph = app.m_assetReferenceGraph;
        Check(graph.nodes.size() == 4 && graph.edges.size() == 3, "graph includes the relative-path referencer and both real and missing dependencies");
        const auto node = [&](const AssetReferenceGraph &model, const char *name) -> const AssetReferenceNode * {
            for (const auto &value : model.nodes) if (std::filesystem::u8path(value.path).filename() == name) return &value;
            return nullptr;
        };
        Check(node(graph, "b.mat") && node(graph, "b.mat")->column == -1 &&
              node(graph, "c.ppm") && node(graph, "c.ppm")->column == 1 &&
              node(graph, "Missing.png") && node(graph, "Missing.png")->missing,
              "reference graph lays out referencers left, dependencies right, and flags the missing node");
        const auto texture = registry.Find((fixture.root / "c.ppm").generic_u8string());
        registry.SetDependencies(a, {"c.ppm", texture->id, "Missing.png"});
        const auto deduplicated = BuildAssetReferenceGraph(fixture.resources, a, 1);
        Check(deduplicated.nodes.size() == 4 && deduplicated.edges.size() == 3,
              "GUID and relative-path references to one asset share one graph node and edge");
        registry.SetDependencies((fixture.root / "c.ppm").generic_u8string(), {"d.mesh"});
        registry.SetDependencies((fixture.root / "d.mesh").generic_u8string(), {"a.mat"});
        const auto cyclic = BuildAssetReferenceGraph(fixture.resources, a, 8);
        Check(cyclic.nodes.size() == 5 && cyclic.edges.size() == 5,
              "deep graph traversal terminates cycles while retaining all actual reference edges");
        const auto limited = BuildAssetReferenceGraph(fixture.resources, a, 8, 3);
        Check(limited.nodes.size() == 3 && limited.truncated, "large reference graphs enforce the node budget and report truncation");
        const auto unicodeFile = fixture.root / std::filesystem::u8path("中文 路径") / "Color.mat";
        PlatformFile::WriteAllText(unicodeFile, "color 1 0 0 1\n");
        registry.Scan(fixture.root);
        const auto unicodeGraph = BuildAssetReferenceGraph(fixture.resources, unicodeFile.generic_u8string());
        Check(unicodeGraph.nodes.size() == 1 && !unicodeGraph.nodes.front().missing,
              "existing assets under UTF-8 paths are not marked as missing graph references");
        ImGuiWindow *canvas = nullptr;
        for (auto *window : ImGui::GetCurrentContext()->Windows)
            if (window->ParentWindow == viewer && std::string(window->Name).find("ReferenceCanvas") != std::string::npos) canvas = window;
        Check(canvas != nullptr, "reference viewer renders its interactive graph canvas");
        const auto center = canvas->InnerRect.GetCenter();
        const auto previousZoom = app.m_assetReferenceZoom;
        io.AddMousePosEvent(center.x, center.y); Frame(draw);
        io.AddMouseWheelEvent(0, 1); Frame(draw);
        Check(app.m_assetReferenceZoom > previousZoom, "mouse wheel zooms the reference graph");
        const auto previousPan = app.m_assetReferencePan;
        io.AddMouseButtonEvent(2, true); Frame(draw);
        io.AddMousePosEvent(center.x + 50, center.y + 20); Frame(draw);
        io.AddMouseButtonEvent(2, false); Frame(draw);
        Check(app.m_assetReferencePan.x > previousPan.x + 10, "middle mouse drag pans the reference graph");
        const auto *textureNode = node(app.m_assetReferenceGraph, "c.ppm");
        const auto canvasCenter = canvas->InnerRect.GetCenter();
        const ImVec2 target{canvasCenter.x + app.m_assetReferencePan.x + textureNode->column * app.Scaled(280) * app.m_assetReferenceZoom,
                            canvasCenter.y + app.m_assetReferencePan.y + textureNode->row * app.Scaled(110) * app.m_assetReferenceZoom};
        io.AddMousePosEvent(target.x, target.y); Frame(draw);
        io.AddMouseButtonEvent(0, true); Frame(draw); io.AddMouseButtonEvent(0, false); Frame(draw);
        Check(app.m_selectedAssets.size() == 1 && app.m_selectedAssets.front() == (fixture.root / "c.ppm").generic_u8string(),
              "clicking a graph node locates that asset in the browser");
        io.AddMouseButtonEvent(0, true); Frame(draw); io.AddMouseButtonEvent(0, false); Frame(draw); Frame(draw);
        Check(app.m_assetReferenceRoot == (fixture.root / "c.ppm").generic_u8string() &&
              app.m_assetReferenceGraph.nodes.front().path == (fixture.root / "c.ppm").generic_u8string(),
              "double clicking a graph node rebuilds the viewer around that asset");
        app.m_showAssetReferences = false;
        app.SelectAsset(b, false); app.m_assetFolder = fixture.root;
        Frame(draw); Frame(draw);
        table = ImGui::GetCurrentContext()->Tables.GetByKey(BrowserItems()->GetID("AssetList"));
        io.AddMousePosEvent(table->Columns[0].MinX + 24, table->OuterRect.Min.y + height * 1.5f); Frame(draw);
        io.AddMouseButtonEvent(1, true); Frame(draw); io.AddMouseButtonEvent(1, false); Frame(draw); Frame(draw);
        BrowserActivate(BrowserPopup(), "Details / Preview");
        Check(BrowserPopup() && BrowserPopup()->PopupId == ImGui::FindWindowByName("Assets")->GetID("AssetDetails") && app.m_selectedAsset == a,
              "asset context details opens for the clicked asset using the correct popup scope");
    }

    void AssetClicks()
    {
        const auto root = std::filesystem::absolute("Saved/EditorP0Clicks/assets");
        std::filesystem::create_directories(root);
        PlatformFile::WriteAllText(root / "a.mesh", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
        PlatformFile::WriteAllText(root / "b.frag", "#version 330 core\nvoid main() {}\n");
        ResourceManager resources;
        resources.Initialize(nullptr, root);
        auto &context = const_cast<EngineContext &>(app.m_engine.GetContext());
        context.resourceManager = &resources;
        auto &settings = const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings());
        const auto oldSettings = settings;
        settings.projectFile = root.parent_path() / "Clicks.tinyproject";
        settings.assetDirectory = "assets";
        app.m_assetFolder = root;
        app.m_assetViewMode = EditorApp::AssetViewMode::List;
        app.m_selectedAsset.clear();
        app.m_assetSearch.fill('\0');
        app.m_assetTypeFilters.clear();
        world.Clear();
        const auto entity = world.CreateEntity("Keep scene object");
        app.m_selected = entity;
        app.m_selection = {entity};
        const auto draw = [&] { app.DrawAssets(); };
        app.m_sceneDirty = false;
        auto &io = ImGui::GetIO();
        Frame(draw);
        Frame(draw);
        ImGuiWindow *browser = nullptr;
        for (auto *candidate : ImGui::GetCurrentContext()->Windows)
            if (candidate->ParentWindow == ImGui::FindWindowByName("Assets") &&
                std::string(candidate->Name).find("AssetBrowserItems") != std::string::npos) browser = candidate;
        Check(browser != nullptr, "asset click fixture exposes the real browser");
        auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(browser->GetID("AssetList"));
        Check(table != nullptr, "asset click fixture exposes its rows");
        const float rowHeight = ImGui::GetFontSize() + ImGui::GetStyle().CellPadding.y * 2.0f;
        const float x = table->Columns[0].MinX + 24.0f;
        const float firstRow = table->OuterRect.Min.y + rowHeight * 1.5f;
        const auto click = [&](const float y) {
            io.AddMousePosEvent(x, y); Frame(draw);
            io.AddMouseButtonEvent(0, true); Frame(draw);
            io.AddMouseButtonEvent(0, false); Frame(draw);
        };
        click(firstRow);
        Check(std::filesystem::path(app.m_selectedAsset).filename() == "a.mesh", "single click selects an asset");
        click(firstRow);
        Check(world.GetEntityManager().GetAliveEntities().size() == 1 && !app.m_sceneDirty && app.m_showAssetPreview,
              "double clicking a mesh previews it without changing the scene");
        for (int frame = 0; frame < 24; ++frame) Frame(draw);
        click(firstRow + rowHeight);
        Check(std::filesystem::path(app.m_selectedAsset).filename() == "b.frag" && app.m_openShaderAsset.empty(),
              "single clicking a shader selects without opening its document");
        click(firstRow + rowHeight);
        Check(std::filesystem::path(app.m_openShaderAsset).filename() == "b.frag",
              "double clicking a shader opens its document");
        app.m_openShaderAsset.clear();
        app.m_shaderSource.clear();
        app.m_savedShaderSource.clear();
        app.m_selectedAsset.clear();
        app.m_selectedAssets.clear();
        app.m_showAssetPreview = false;
        app.ResetSceneInteraction();
        settings = oldSettings;
        context.resourceManager = nullptr;
        resources.Shutdown();
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
        const auto fieldPoint = [&](const char *component, bool vector) {
            const auto id = ImHashStr("Properties", 0, ImHashStr(component, 0, window->ID));
            const auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(id);
            if (!table) throw std::runtime_error("Inspector property table is missing");
            return ImVec2{table->Columns[1].WorkMinX + (vector ? ImGui::GetFrameHeight() + style.ItemSpacing.x : 0) + 8,
                          table->OuterRect.Min.y + style.CellPadding.y + ImGui::GetFrameHeight() * .5f};
        };
        auto point = fieldPoint("Transform", true);
        const float x = point.x, y = point.y;
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
        point = fieldPoint("Name", false);
        io.AddMousePosEvent(point.x, point.y);
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

    void AssetGridDrag()
    {
        const auto root = std::filesystem::absolute("Saved/EditorP0Clicks/assets");
        ResourceManager resources; resources.Initialize(nullptr,root);
        auto &context = const_cast<EngineContext &>(app.m_engine.GetContext()); context.resourceManager = &resources;
        auto &settings = const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings()); const auto previous = settings;
        settings.projectFile = root.parent_path()/"Clicks.tinyproject"; settings.assetDirectory = "assets";
        world.Clear(); const auto entity = world.CreateEntity("Drop target");
        world.RemoveComponent<NameComponent>(entity); world.RemoveComponent<ActiveComponent>(entity);
        world.RemoveComponent<TagComponent>(entity); world.RemoveComponent<LayerComponent>(entity);
        world.RemoveComponent<TransformComponent>(entity); world.AddComponent(entity,MeshComponent{});
        app.m_selected = entity; app.m_selection = {entity}; app.m_selectedAsset.clear();
        app.m_assetFolder = root; app.m_assetViewMode = EditorApp::AssetViewMode::Grid; app.m_assetThumbnailSize = 64;
        app.m_undo.clear(); app.m_sceneDirty = false;
        const auto draw = [&] { app.DrawInspector(); app.DrawAssets(); };
        Frame(draw); Frame(draw);
        ImGuiWindow *browser = nullptr;
        for (auto *candidate : ImGui::GetCurrentContext()->Windows)
            if (candidate->ParentWindow == ImGui::FindWindowByName("Assets") &&
                std::string(candidate->Name).find("AssetBrowserItems") != std::string::npos) browser = candidate;
        const ImVec2 source{browser->DC.CursorStartPos.x + 32,browser->DC.CursorStartPos.y + 32};
        const auto *inspector = ImGui::FindWindowByName("Inspector");
        const auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(ImHashStr("Properties",0,ImHashStr("Mesh Renderer",0,inspector->ID)));
        const ImVec2 target{table->Columns[1].WorkMinX + 20,table->OuterRect.Min.y + ImGui::GetStyle().CellPadding.y + ImGui::GetFrameHeight() * .5f};
        auto &io = ImGui::GetIO(); io.AddMousePosEvent(source.x,source.y); Frame(draw);
        io.AddMouseButtonEvent(0,true); Frame(draw); Frame(draw);
        Check(app.m_selectedAsset.empty(),"grid mouse-down preserves the entity inspector until click release");
        io.AddMousePosEvent(source.x+20,source.y+20); Frame(draw); Frame(draw);
        io.AddMousePosEvent(target.x,target.y); Frame(draw); Frame(draw);
        io.AddMouseButtonEvent(0,false); Frame(draw); Frame(draw);
        Check(world.GetComponent<MeshComponent>(entity)->meshPath == "assets/a.mesh" && app.m_undo.size() == 1,
              "real grid drag assigns a typed property without hiding its target");
        const auto asset = resources.GetAssetRegistry().Find((root/"a.mesh").generic_u8string());
        app.SelectAsset(asset->path,false);
        Check(app.PlaceAssetInScene(*asset) && app.m_selected != entity && app.m_selectedAsset.empty(),
              "explicit placement transfers the inspector to the new scene object");
        app.m_assetViewMode = EditorApp::AssetViewMode::List;
        app.ResetSceneInteraction(); app.m_selectedAsset.clear();
        context.resourceManager = nullptr; settings = previous; resources.Shutdown();
    }

    void Gizmo()
    {
        Renderer renderer;
        const_cast<EngineContext &>(app.m_engine.GetContext()).renderer = &renderer;
        auto &view=const_cast<FrameView &>(renderer.GetFrameView());
        view.cameraPosition={0,0,5}; view.view=LookAt(view.cameraPosition,{}, {0,1,0}); view.projection=Perspective(60,1,.1f,100);
        app.m_editorCameraPosition=view.cameraPosition; app.m_gizmoSpace=EditorApp::GizmoSpace::World;
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

    void InspectorBatchAndValidation()
    {
        world.Clear();
        app.ResetSceneInteraction();
        app.m_undo.clear(); app.m_redo.clear(); app.m_sceneDirty = false;
        const auto first = world.CreateEntity("First"), second = world.CreateEntity("Second");
        world.GetComponent<TransformComponent>(first)->position = {1,2,3};
        world.GetComponent<TransformComponent>(second)->position = {4,5,6};
        app.m_selected = first; app.m_selection = {first,second};
        const auto &transform = *ReflectionRegistry::Get().FindComponent("Transform");
        const auto &position = transform.properties[0];
        const Vec3 edited{10,2,3};
        Frame([&] {
            ImGui::Begin("Batch host");
            app.ApplyInspectorProperty(transform,position,&edited,false,0);
            ImGui::End();
        });
        Check(world.GetComponent<TransformComponent>(first)->position.x == 10 &&
              world.GetComponent<TransformComponent>(second)->position.x == 10 &&
              world.GetComponent<TransformComponent>(second)->position.y == 5 &&
              world.GetComponent<TransformComponent>(second)->position.z == 6 && app.m_undo.size() == 1,
              "batch axis edit preserves untouched mixed axes and records one transaction");
        app.Undo();
        Entity restoredFirst = InvalidEntity, restoredSecond = InvalidEntity;
        for (const auto entity : world.GetEntityManager().GetAliveEntities())
        {
            const auto &name = world.GetComponent<NameComponent>(entity)->name;
            if (name == "First") restoredFirst = entity;
            if (name == "Second") restoredSecond = entity;
        }
        Check(world.GetComponent<TransformComponent>(restoredFirst)->position.x == 1 &&
              world.GetComponent<TransformComponent>(restoredSecond)->position.x == 4,
              "one Undo restores both objects after a batch edit");
        app.m_selected = restoredFirst; app.m_selection = {restoredFirst,restoredSecond};
        app.m_undo.clear(); app.m_redo.clear(); app.m_sceneDirty = false;
        app.ResetInspectorComponent(transform);
        Check(world.GetComponent<TransformComponent>(restoredFirst)->position.x == 0 &&
              world.GetComponent<TransformComponent>(restoredSecond)->position.y == 0 &&
              world.GetComponent<TransformComponent>(restoredSecond)->scale.x == 1 && app.m_undo.size() == 1,
              "component reset applies defaults to the selection as one transaction");
        app.m_undo.clear(); app.m_sceneDirty = false;
        app.ResetInspectorComponent(transform);
        Check(app.m_undo.empty() && !app.m_sceneDirty, "resetting default properties does not dirty the scene");

        const auto root = std::filesystem::absolute("Saved/EditorP0Clicks/assets");
        ResourceManager resources; resources.Initialize(nullptr,root);
        auto &context = const_cast<EngineContext &>(app.m_engine.GetContext()); context.resourceManager = &resources;
        auto &settings = const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings()); const auto oldSettings = settings;
        settings.projectFile = root.parent_path()/"Clicks.tinyproject"; settings.assetDirectory = "assets";
        world.AddComponent(restoredFirst,MeshComponent{"assets/a.mesh",""});
        world.AddComponent(restoredSecond,MeshComponent{"assets/a.mesh",""});
        const auto &mesh = *ReflectionRegistry::Get().FindComponent("Mesh Renderer");
        const auto &meshPath = mesh.properties[0];
        app.m_feedback.clear();
        const auto fieldId = 321u;
        Check(!app.ApplyInspectorAsset(mesh,meshPath,(root/"b.frag").generic_u8string(),fieldId) &&
              world.GetComponent<MeshComponent>(restoredFirst)->meshPath == "assets/a.mesh" && app.m_undo.empty() &&
              app.m_inspectorErrors.count(fieldId) && !app.m_feedback.empty() && app.m_feedback.back().kind == EditorApp::FeedbackKind::Error,
              "wrong asset type retains the old reference and shows inline and global errors");
        Check(!app.ApplyInspectorAsset(mesh,meshPath,"assets/missing.mesh",fieldId) && !app.m_sceneDirty,
              "missing asset path cannot silently overwrite a valid reference");
        Check(app.ApplyInspectorAsset(mesh,meshPath,(root/"a.mesh").generic_u8string(),fieldId) &&
              !app.m_inspectorErrors.count(fieldId) && world.GetComponent<MeshComponent>(restoredFirst)->meshPath == "assets/a.mesh" && app.m_undo.empty(),
              "valid asset correction clears the field error and preserves a portable reference");
        Check(app.ApplyInspectorAsset(mesh,meshPath,"",fieldId) && app.m_undo.size() == 1 &&
              world.GetComponent<MeshComponent>(restoredFirst)->meshPath.empty() && world.GetComponent<MeshComponent>(restoredSecond)->meshPath.empty(),
              "clearing a resource reference edits the selection in one undoable operation");
        Frame([&] { app.DrawFeedback(); });
        Check(!app.m_feedback.empty(), "operation errors are visible without opening the console");
        const auto oldDelta = ImGui::GetIO().DeltaTime; ImGui::GetIO().DeltaTime = 13;
        Frame([&] { app.DrawFeedback(); }); ImGui::GetIO().DeltaTime = oldDelta;
        Check(app.m_feedback.empty(), "transient feedback expires without blocking the editor");
        context.resourceManager = nullptr; settings = oldSettings; resources.Shutdown();
        app.ResetSceneInteraction();
    }

    void InspectorAxisDefaults()
    {
        world.Clear(); app.ResetSceneInteraction(); app.m_selectedAsset.clear();
        const auto entity = world.CreateEntity("Collider reset");
        world.RemoveComponent<NameComponent>(entity); world.RemoveComponent<ActiveComponent>(entity);
        world.RemoveComponent<TagComponent>(entity); world.RemoveComponent<LayerComponent>(entity);
        world.RemoveComponent<TransformComponent>(entity);
        ColliderComponent collider; collider.halfExtents = {2,3,4}; world.AddComponent(entity,collider);
        app.m_selected = entity; app.m_selection = {entity}; app.m_undo.clear();
        const auto draw = [&] { app.DrawInspector(); };
        Frame(draw); Frame(draw);
        const auto *window = ImGui::FindWindowByName("Inspector");
        const auto tableId = ImHashStr("Properties",0,ImHashStr("Collider",0,window->ID));
        const auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(tableId);
        Check(table != nullptr,"collider reset fixture exposes the actual property controls");
        const float rowHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().CellPadding.y * 2;
        const float x = table->Columns[1].WorkMinX + ImGui::GetFrameHeight() * .5f;
        const float y = table->OuterRect.Min.y + rowHeight * 1.5f;
        auto &io = ImGui::GetIO(); io.AddMousePosEvent(x,y); Frame(draw);
        io.AddMouseButtonEvent(0,true); Frame(draw); io.AddMouseButtonEvent(0,false); Frame(draw);
        Check(world.GetComponent<ColliderComponent>(entity)->halfExtents.x == .5f &&
              world.GetComponent<ColliderComponent>(entity)->halfExtents.y == 3 && app.m_undo.size() == 1,
              "axis reset uses the component default and preserves the other axes");
        app.ResetSceneInteraction();
    }

    void TypedSkeletonAndFailedOpen()
    {
        const auto root = std::filesystem::absolute("Saved/EditorP0Typed/assets");
        PlatformFile::WriteAllText(root/"rig.skeleton","skeleton fixture");
        PlatformFile::WriteAllText(root/"locomotion.blendtree","blend tree fixture");
        ResourceManager resources; resources.Initialize(nullptr,root);
        auto &context = const_cast<EngineContext &>(app.m_engine.GetContext()); context.resourceManager = &resources;
        auto &settings = const_cast<ProjectSettings &>(app.m_engine.GetProjectSettings()); const auto previous = settings;
        settings.projectFile = root.parent_path()/"Typed.tinyproject"; settings.assetDirectory = "assets";
        world.Clear(); const auto entity = world.CreateEntity("Animated"); world.AddComponent(entity,AnimationComponent{});
        app.m_selected = entity; app.m_selection = {entity}; app.m_selectedAsset.clear();
        const auto &component = *ReflectionRegistry::Get().FindComponent("Animation");
        const auto property = std::find_if(component.properties.begin(),component.properties.end(),[](const auto &p) { return p.name == "Skeleton"; });
        Check(app.ApplyInspectorAsset(component,*property,(root/"rig.skeleton").generic_u8string(),963) &&
              world.GetComponent<AnimationComponent>(entity)->skeletonPath == "assets/rig.skeleton",
              "typed skeleton fields accept registered skeleton assets");
        const auto clip = std::find_if(component.properties.begin(),component.properties.end(),[](const auto &p) { return p.name == "Clip"; });
        Check(app.ApplyInspectorAsset(component,*clip,(root/"locomotion.blendtree").generic_u8string(),964) &&
              world.GetComponent<AnimationComponent>(entity)->clipPath == "assets/locomotion.blendtree",
              "animation clip picker accepts blend trees supported by the runtime");
        context.resourceManager = nullptr; settings = previous; resources.Shutdown();
        app.m_messages.clear(); app.m_feedback.clear();
        app.m_editingAsset = (root/"missing.vfx").generic_u8string(); app.m_openEffectAsset.clear();
        Frame([&] { app.DrawEffectEditor(); }); Frame([&] { app.DrawEffectEditor(); });
        Check(app.m_messages.size() == 1 && app.m_feedback.size() == 1,
              "a failed asset open reports once instead of retrying every frame");
        app.ResetSceneInteraction();
    }

    void PackagingWriteLock()
    {
        world.Clear(); const auto entity = world.CreateEntity("Packaging snapshot");
        app.m_selected = entity; app.m_selection = {entity}; app.m_selectedAsset.clear();
        const auto path = std::filesystem::absolute("Saved/EditorP0Packaging/locked.scene");
        const auto packageInput = app.m_engine.GetSceneManager().CreateSnapshot();
        PlatformFile::WriteAllText(path,packageInput);
        world.CreateEntity("Unsaved addition");
        auto &document = app.m_sceneDocuments[app.m_activeSceneDocument]; const auto previousPath = document.path;
        document.path = path;
        std::promise<PackageResult> completion; app.m_packageFuture = completion.get_future();
        app.m_sceneCommandsFocused = true;
        Key(ImGuiKey_Delete);
        Check(world.IsAlive(entity),"packaging blocks scene keyboard writes until staging completes");
        Check(!app.SaveCurrentScene() && PlatformFile::ReadAllText(path) == packageInput,
              "packaging cannot overwrite source scenes while the worker reads them");
        PackageResult result; result.success = true; result.outputDirectory = path.parent_path(); completion.set_value(result);
        Frame([&] { app.DrawPackagingStatus(); });
        Check(!app.m_packageFuture.valid(),"completed packaging releases the edit lock through the UI status poll");
        document.path = previousPath; app.ResetSceneInteraction();
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
            const auto *panel=ImGui::FindWindowByName("Hierarchy");
            rowMinimum={panel->WorkRect.Min.x,panel->DC.CursorPosPrevLine.y};
            rowMaximum={panel->WorkRect.Max.x,rowMinimum.y+panel->DC.PrevLineSize.y};
        };
        auto &io = ImGui::GetIO();
        Frame(draw); Frame(draw);
        Check(rowMaximum.y > rowMinimum.y, "hierarchy exposes a clickable entity row");
        io.AddMousePosEvent(rowMinimum.x + 38.0f, (rowMinimum.y+rowMaximum.y)*.5f);
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

    void CombinedGizmo()
    {
        Renderer renderer; auto &context=const_cast<EngineContext &>(app.m_engine.GetContext()); context.renderer=&renderer;
        auto &view=const_cast<FrameView &>(renderer.GetFrameView());
        view.cameraPosition={4,3,6}; view.view=LookAt(view.cameraPosition,{}, {0,1,0}); view.projection=Perspective(60,1,.1f,100);
        app.m_editorCameraPosition=view.cameraPosition; app.m_gizmoSpace=EditorApp::GizmoSpace::World;
        app.m_pivotMode=EditorApp::PivotMode::Individual; app.m_gizmoSnap=false;
        const float length=Length(view.cameraPosition)*.13f;
        const auto project=[&](Vec3 p) {
            const auto matrix=view.projection*view.view;
            const float w=matrix.At(3,0)*p.x+matrix.At(3,1)*p.y+matrix.At(3,2)*p.z+matrix.At(3,3);
            return ImVec2{50+400*(1+(matrix.At(0,0)*p.x+matrix.At(0,1)*p.y+matrix.At(0,2)*p.z+matrix.At(0,3))/w),
                          50+400*(1-(matrix.At(1,0)*p.x+matrix.At(1,1)*p.y+matrix.At(1,2)*p.z+matrix.At(1,3))/w)};
        };
        const auto draw=[&] {
            ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({1000,850});
            ImGui::Begin("CombinedGizmo"); app.DrawGizmo(50,50,800,800,true); ImGui::End();
        };
        auto &io=ImGui::GetIO();
        const auto select=[&] {
            world.Clear(); app.ResetSceneInteraction(); app.m_undo.clear(); app.m_redo.clear(); app.m_sceneDirty=false;
            const auto e=world.CreateEntity("Combined"); app.m_selected=e; app.m_selection={e}; Frame(draw); Frame(draw); return e;
        };
        const auto drag=[&](ImVec2 from,ImVec2 to,EditorApp::GizmoMode operation,int axis) {
            io.AddMousePosEvent(from.x,from.y); Frame(draw); io.AddMouseButtonEvent(0,true); Frame(draw);
            Check(app.m_activeGizmoAxis==axis && app.m_gizmoDragMode==operation,"combined handle chooses its operation and axis without a mode switch");
            io.AddMousePosEvent(to.x,to.y); Frame(draw);
            io.AddMouseButtonEvent(0,false); Frame(draw);
        };
        for(int axis=0;axis<3;++axis)
        {
            const auto e=select(); const Vec3 direction=axis==0 ? Vec3{1,0,0} : axis==1 ? Vec3{0,1,0} : Vec3{0,0,1};
            const auto from=project(direction*(length*.52f)),center=project({});
            drag(from,{from.x+(from.x-center.x)*.25f,from.y+(from.y-center.y)*.25f},EditorApp::GizmoMode::Scale,axis);
            const auto *t=world.GetComponent<TransformComponent>(e);
            // Projected axes still admit ImGui's integer mouse-coordinate rounding.
            Check(std::abs((&t->scale.x)[axis]-1.25f)<.03f && Length(t->position)<.001f && Length(t->rotation)<.001f && app.m_undo.size()==1,
                  "inner scale cube changes only scale and records one undo");
            app.Undo(); Check(std::abs(world.GetComponent<TransformComponent>(*world.GetEntityManager().GetAliveEntities().begin())->scale.x-1)<.001f,
                  "undo restores a combined scale gesture");
        }
        for(int axis=0;axis<3;++axis)
        {
            const auto e=select(); const Vec3 direction=axis==0 ? Vec3{1,0,0} : axis==1 ? Vec3{0,1,0} : Vec3{0,0,1};
            const auto from=project(direction*length),center=project({});
            drag(from,{from.x+(from.x-center.x)*.25f,from.y+(from.y-center.y)*.25f},EditorApp::GizmoMode::Translate,axis);
            const auto *t=world.GetComponent<TransformComponent>(e);
            Check(Dot(t->position,direction)>0 && Length(t->scale-Vec3{1,1,1})<.001f && Length(t->rotation)<.001f && app.m_undo.size()==1,
                  "outer arrow changes only translation and records one undo");
        }
        for(int axis=0;axis<3;++axis)
        {
            const auto e=select(); const Vec3 u=axis==0 ? Vec3{0,1,0} : axis==1 ? Vec3{0,0,1} : Vec3{1,0,0};
            const Vec3 v=axis==0 ? Vec3{0,0,1} : axis==1 ? Vec3{1,0,0} : Vec3{0,1,0};
            const float first=DegreesToRadians(140),last=DegreesToRadians(175);
            drag(project((u*std::cos(first)+v*std::sin(first))*(length*.8f)),
                 project((u*std::cos(last)+v*std::sin(last))*(length*.8f)),EditorApp::GizmoMode::Rotate,axis);
            const auto *t=world.GetComponent<TransformComponent>(e);
            Check(std::abs((&t->rotation.x)[axis]-35)<2 && Length(t->position)<.001f && Length(t->scale-Vec3{1,1,1})<.001f && app.m_undo.size()==1,
                  "rotation ring remains available after scaling and translation without a mode switch");
        }
        const auto e=select(); world.GetComponent<TransformComponent>(e)->rotation.z=90;
        const auto localX=project({0,length*.52f,0}),center=project({});
        drag(localX,{localX.x+(localX.x-center.x)*.25f,localX.y+(localX.y-center.y)*.25f},EditorApp::GizmoMode::Scale,0);
        const auto *scaled=world.GetComponent<TransformComponent>(e);
        Check(std::abs(scaled->scale.x-1.25f)<.03f && std::abs(scaled->scale.y-1)<.001f && Length(scaled->position)<.001f,
              "local scale cube wins over an overlapping world translation shaft");
        app.ResetSceneInteraction(); world.Clear(); context.renderer=nullptr;
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
            return ImVec2{50 + 400 * (1 + (m.At(0, 0) * point.x + m.At(0, 1) * point.y + m.At(0, 2) * point.z + m.At(0, 3)) / w),
                          50 + 400 * (1 - (m.At(1, 0) * point.x + m.At(1, 1) * point.y + m.At(1, 2) * point.z + m.At(1, 3)) / w)};
        };
        auto draw = [&]() {
            ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({1000,850});
            ImGui::Begin("GizmoGeometry"); app.DrawGizmo(50, 50, 800, 800, true); ImGui::End();
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
            const Vec3 direction = axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
            const auto from = project(direction * length), end = project(direction * length), origin = project({});
            drag(from, {from.x + (end.x - origin.x) * 0.3f, from.y + (end.y - origin.y) * 0.3f}, axis);
            Check(Dot(world.GetComponent<TransformComponent>(entity)->position, direction) > 0 && app.m_undo.size() == 1,
                  "dragging along each visible arrow moves positively on that axis with one Undo");
        }
        world.Clear(); const auto scaled = world.CreateEntity("Scale");
        app.ResetSceneInteraction(); app.m_selected = scaled; app.m_selection = {scaled};
        const auto blue = project({0, 0, length*.52f}), origin = project({});
        drag(blue, {blue.x + (blue.x - origin.x) * 0.25f, blue.y + (blue.y - origin.y) * 0.25f}, 2);
        Check(std::abs(world.GetComponent<TransformComponent>(scaled)->scale.z - 1.25f) < 0.02f,
              "blue scale handle grows in its displayed direction by the drag ratio");

        for (int axis = 0; axis < 3; ++axis)
        {
            world.Clear(); const auto rotated = world.CreateEntity("Rotate");
            app.ResetSceneInteraction(); app.m_selected = rotated; app.m_selection = {rotated};
            const Vec3 u = axis == 0 ? Vec3{0, 1, 0} : axis == 1 ? Vec3{0, 0, 1} : Vec3{1, 0, 0};
            const Vec3 v = axis == 0 ? Vec3{0, 0, 1} : axis == 1 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            const float firstAngle = DegreesToRadians(140), lastAngle = DegreesToRadians(175);
            drag(project((u * std::cos(firstAngle) + v * std::sin(firstAngle)) * (length*.8f)),
                 project((u * std::cos(lastAngle) + v * std::sin(lastAngle)) * (length*.8f)), axis);
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
        auto red = project({length, 0, 0}), redEnd = project({length, 0, 0});
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
    void MaterialTexturePortability()
    {
        const auto directory=std::filesystem::absolute("Saved/EditorPortable"); const auto assets=directory/"assets";
        std::filesystem::create_directories(assets); PlatformFile::WriteAllText(assets/"white.ppm","P3\n1 1\n255\n255 255 255\n");
        MaterialAssetData data; data.formatVersion=2; data.definitionPath="builtin:PBR";
        auto schema=MakeBuiltinPbrDefinition().parameters;
        for (auto &p:schema) if (p.name=="BaseMap") p.defaultValue=(assets/"white.ppm").generic_u8string();
        MaterialAssetEditor editor; editor.Open((assets/"child.mat").generic_u8string(),data,schema);
        editor.SetSaveRequest([](const auto &path,const auto &draft,auto &) { return PlatformFile::WriteAllText(std::filesystem::u8path(path),SerializeMaterialAsset(draft)); });
        Frame([&] { editor.Draw(); }); Frame([&] { editor.Draw(); });
        auto window=ImGui::FindWindowByID(ImHashStr("###MaterialAssetEditor"));
        ImGui::ActivateItemByID(ImHashStr("###Override",0,ImHashStr("BaseMap",0,window->ID))); Frame([&] { editor.Draw(); });
        Check(editor.GetDraft().overrides.count("BaseMap") && !std::filesystem::u8path(std::get<std::string>(editor.GetDraft().overrides.at("BaseMap"))).is_absolute(),"actual inherited texture checkbox stores a portable reference");
        Check(editor.Save(),"save inherited texture override");
        Check(PakArchive::PackDirectory(directory,directory.parent_path()/"EditorPortable.tpak"),"package checkbox-created override");
        const auto unavailable=directory/"assets-unavailable"; std::filesystem::remove_all(unavailable); std::filesystem::rename(assets,unavailable);
        ResourceManager resources; resources.Initialize(nullptr,assets); PlatformFile::ClearMounts(); PlatformFile::MountPak(directory.parent_path()/"EditorPortable.tpak"); PlatformFile::SetReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
        MaterialSystem system(resources); const auto proxy=system.BuildProxy("assets/child.mat");
        const bool loaded=std::any_of(proxy->textures.begin(),proxy->textures.end(),[](const auto &t) { return t.parameterName=="BaseMap" && t.resource; });
        PlatformFile::ClearMounts(); resources.Shutdown(); std::filesystem::rename(unavailable,assets);
        Check(loaded,"checkbox-created texture resolves from package with original sources unavailable");
    }
    void MaterialExternalReload()
    {
        const auto assets=std::filesystem::absolute("Saved/EditorReload/assets");
        auto definition=MakeBuiltinPbrDefinition(); PlatformFile::WriteAllText(assets/"parent.matgraph",definition.graph.Serialize());
        MaterialAssetData data; data.formatVersion=2; data.definitionPath="parent.matgraph"; data.overrides["Roughness"]=.2f;
        PlatformFile::WriteAllText(assets/"child.mat",SerializeMaterialAsset(data));
        ResourceManager resources; resources.Initialize(nullptr,assets); Renderer renderer; renderer.SetResourceManager(&resources);
        EditorApp app; auto &context=const_cast<EngineContext &>(app.m_engine.GetContext()); context.resourceManager=&resources; context.renderer=&renderer;
        const auto select=[&](const std::string &name) { for (const auto &a:resources.GetAssetRegistry().GetAssets()) if (std::filesystem::u8path(a.path).filename()==name) { app.SelectAsset(a.path,false); app.OpenAsset(a); } };
        const auto draw=[&] { app.DrawMaterialGraphEditor(); };
        const auto reload=[&] { auto &io=ImGui::GetIO(); io.AddKeyEvent(ImGuiMod_Ctrl,true); io.AddKeyEvent(ImGuiKey_R,true); Frame(draw); io.AddKeyEvent(ImGuiKey_R,false); io.AddKeyEvent(ImGuiMod_Ctrl,false); Frame(draw); };
        select("child.mat"); Frame(draw); data.overrides["Roughness"]=.8f; PlatformFile::WriteAllText(assets/"child.mat",SerializeMaterialAsset(data)); reload();
        Check(app.m_materialAssetEditor.Save(),"save clean instance after external Ctrl+R");
        MaterialResource loaded; loaded.Load((assets/"child.mat").generic_u8string());
        Check(std::get<float>(loaded.GetMaterialData().overrides.at("Roughness"))==.8f,"Ctrl+R refreshes clean instance draft and saved baseline");
        select("parent.matgraph"); Frame(draw); definition.graph.GetParameters()[1].defaultValue=.6f; PlatformFile::WriteAllText(assets/"parent.matgraph",definition.graph.Serialize()); reload();
        Check(app.m_materialGraphEditor.Save(),"save clean graph after external Ctrl+R");
        MaterialDefinitionResource saved; saved.Load((assets/"parent.matgraph").generic_u8string());
        Check(std::get<float>(saved.GetData().parameters[1].defaultValue)==.6f,"Ctrl+R refreshes clean graph draft");
        select("child.mat"); Frame(draw); app.m_materialAssetEditor.SetOverride("Roughness",.5f); data.overrides["Roughness"]=.9f; PlatformFile::WriteAllText(assets/"child.mat",SerializeMaterialAsset(data)); reload();
        Check(app.m_materialAssetEditor.IsDirty() && std::get<float>(app.m_materialAssetEditor.GetDraft().overrides.at("Roughness"))==.5f && !app.m_materialAssetEditor.Save(),"dirty external reload retains draft and blocks stale overwrite with conflict");
        app.m_materialAssetEditor.Discard();
        Check(std::get<float>(app.m_materialAssetEditor.GetDraft().overrides.at("Roughness"))==.9f,"discard after reload restores latest external baseline");
        context.renderer=nullptr; context.resourceManager=nullptr; renderer.Shutdown(); resources.Shutdown();
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
        scene.GetComponent<MeshComponent>(a)->materialPath="assets/Shared.matgraph"; renderer.RenderFrame(scene); hdr=device->ReadHdrAttachmentForTests(0);
        Check(std::abs(hdr[32*128+52].x-.3f)<.02f && std::abs(hdr[32*128+52].y-.3f)<.02f,
              "node material renders its defaults directly on a scene mesh");
        scene.GetComponent<MeshComponent>(a)->materialPath="assets/Red.mat";
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

int main(int argc, char **argv)
{
    try
    {
        std::cout << std::unitbuf;
        MiniEngine::EditorInteractionTests tests;
        if (argc>1 && std::string(argv[1])!="--legacy") {
            const std::string filter=argv[1];
            if(filter=="--logical-ui") tests.LogicalUiMetrics();
            else if(filter=="--project-create") tests.ProjectCreation();
            else if(filter=="--dpi") tests.NativeDialogDpi();
            else if(filter=="--ground-grid") tests.GroundGridContinuity();
            else if(filter=="--view-cube") tests.ViewCubeNavigation();
            else if(filter=="--combined-gizmo") tests.CombinedGizmo();
            else if(filter=="--p1-drop") tests.P1SceneDrag();
            else if(filter=="--p1-orbit") tests.P1OrbitStability();
            else if(filter=="--p1-import") tests.P1HiddenImport();
            else if(filter=="--p1-docking") tests.P1Docking();
            else if(filter=="--p1-settings") tests.P1Settings();
            else if(filter=="--p1-assets") tests.P1AssetWorkflow();
            else if(filter=="--p1-scene") tests.P1SceneNavigation();
            else if(filter=="--asset-menus") tests.AssetBrowserMenus();
            else if(filter=="--asset-filters") tests.AssetBrowserFilters();
            else if(filter=="--asset-references") tests.AssetBrowserReferences();
            else if(filter=="--material-assignment") tests.MaterialAssignmentRegression();
            else if(filter=="--asset-delete") tests.AssetDeleteShortcutRegression();
            else if(filter=="--material-trash-cache") tests.MaterialTrashCacheRegression();
            return 0;
        }
        if(argc==1) { tests.P1Settings(); tests.P1HiddenImport(); tests.P1Docking(); tests.P1AssetWorkflow(); tests.P1SceneNavigation(); tests.P1OrbitStability(); tests.P1SceneDrag(); }
        tests.LogicalUiMetrics();
        tests.GroundGridContinuity();
        tests.ViewCubeNavigation();
        tests.Shortcuts();
        tests.Documents();
        tests.AssetClicks();
        tests.AssetBrowserMenus();
        tests.AssetBrowserFilters();
        tests.AssetBrowserReferences();
        tests.MaterialAssignmentRegression();
        tests.AssetDeleteShortcutRegression();
        tests.MaterialTrashCacheRegression();
        tests.AssetGridDrag();
        tests.Inspector();
        tests.InspectorAxisDefaults();
        tests.InspectorBatchAndValidation();
        tests.TypedSkeletonAndFailedOpen();
        tests.PackagingWriteLock();
        tests.Gizmo();
        tests.HierarchyCameraFocus();
        tests.CombinedGizmo();
        tests.ViewportRegression();
        tests.CompactWorkspace();
        tests.SceneOpenRegression();
        tests.ShaderDocuments();
        tests.WindowClose();
        tests.MaterialDocuments();
        tests.MaterialTexturePortability();
        tests.MaterialExternalReload();
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
