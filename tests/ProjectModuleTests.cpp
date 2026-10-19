#include "MiniEngine/Project/ProjectModuleManifest.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/ProjectSDK/SDKInfo.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Platform/UTF8Arguments.hpp"
#include "MiniEngine/Project/ProjectModuleHost.hpp"
#include "MiniEngine/Platform/DynamicLibrary.hpp"
#include "MiniEngine/Plugin/PluginManager.hpp"
#include "MiniEngine/Project/ProjectHostBridge.hpp"
#include "MiniEngine/ProjectSDK/Project.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/Input/InputSystem.hpp"
#include "MiniEngine/Core/Logger.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <limits>

namespace fs = std::filesystem;
using namespace MiniEngine;

namespace
{
int checks = 0;
void Check(bool condition, const char *name)
{
    if (!condition) throw std::runtime_error(name);
    ++checks;
    std::cout << "PASS " << name << '\n';
}

struct Fixture
{
    fs::path root = fs::temp_directory_path() / ("TinyEngineProjectTests-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path projectRoot = root / fs::u8path(u8"项目 with spaces");
    fs::path projectFile = projectRoot / "Game.tinyproject";
    fs::path manifestFile = projectRoot / "Binaries/Project.modules";
    Fixture()
    {
        fs::create_directories(projectRoot / "Binaries/Development");
        Write(projectFile, "project.name=Fixture\nproject.id=p0-project\ncode.module=game.test\ncode.manifest=Binaries/Project.modules\n");
        Write(projectRoot / "Binaries/Development/Game.dll", "test binary fixture");
        Manifest();
    }
    ~Fixture()
    {
        std::error_code ec;
        const auto absoluteRoot = fs::absolute(root, ec).lexically_normal();
        if (!ec && absoluteRoot.parent_path() == fs::absolute(fs::temp_directory_path()).lexically_normal() &&
            absoluteRoot.filename().string().find("TinyEngineProjectTests-") == 0)
            fs::remove_all(absoluteRoot, ec);
    }
    static void Write(const fs::path &path, const std::string &text)
    {
        Check(PlatformFile::WriteAllText(path, text), "write controlled project fixture");
    }
    void Manifest(const std::string &extra = {})
    {
        Write(manifestFile, std::string("module.id=game.test\nproject.id=p0-project\nmodule.abi=1\nmodule.platform=Windows-x64\n") +
            "Development.sdkBuildId=" + ProjectSDK::GetSDKBuildId() + "\n" +
            "Development.library=Binaries/Development/Game.dll\nDevelopment.pdb=\n" +
            "Development.buildId=build-1\nDevelopment.sourceFingerprint=source-1\n" +
            "Development.schemaHash=none\nDevelopment.dependencies.count=0\n" + extra);
    }
    ProjectSettings Settings() const
    {
        ProjectSettings settings;
        Check(settings.Load(projectFile), "load configured project");
        return settings;
    }
};

TinyProjectString Span(const std::string &value) { return {value.data(), static_cast<uint64_t>(value.size())}; }
TinyProjectModuleInfo Info(const ProjectModuleSelection &selection)
{
    static const std::string profile = "Development";
    return {sizeof(TinyProjectModuleInfo), Span(selection.moduleId), Span(selection.projectId), Span(selection.sdkBuildId),
            Span(selection.projectBuildId), Span(selection.sourceFingerprint), Span(profile)};
}

void ManifestTests()
{
    Fixture f;
    ProjectSettings settings = f.Settings();
    Check(settings.codeRequired, "code_required_defaults_true");
    Check(settings.projectId == "p0-project", "project identity is preserved");
    const auto saved = f.projectRoot / "Saved.tinyproject";
    Check(settings.Save(saved), "save code settings");
    ProjectSettings reloaded;
    Check(reloaded.Load(saved) && reloaded.codeModule == "game.test" && reloaded.codeRequired,
          "project code settings round trip");
    const auto legacy = f.projectRoot / "Legacy.tinyproject";
    Fixture::Write(legacy, "project.name=Legacy\n");
    Check(reloaded.Load(legacy) && reloaded.codeModule.empty() && reloaded.codeManifest.empty() &&
          reloaded.projectId.empty() && !reloaded.codeRequired, "project_load_does_not_retain_previous_code_fields");
    Fixture::Write(f.projectFile, "project.name=Optional\nproject.id=p0-project\ncode.module=game.test\ncode.required=false\ncode.manifest=Binaries/Project.modules\n");
    Check(reloaded.Load(f.projectFile) && !reloaded.codeRequired, "explicit optional code setting is honored");
    Fixture::Write(f.projectFile, "code.module=game.test\n");
    Check(!reloaded.Load(f.projectFile), "incomplete code settings are rejected");

    std::string error;
    ProjectModuleSelection selection;
    const auto previous = fs::current_path();
    fs::current_path(f.root);
    const bool loaded = LoadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error);
    fs::current_path(previous);
    Check(loaded && error.empty(), "manifest_resolves_from_project_root");
    Check(fs::equivalent(selection.libraryPath, f.projectRoot / "Binaries/Development/Game.dll"),
          "unicode and spaces resolve the intended DLL");
    Check(selection.projectBuildId == "build-1" && selection.sdkBuildId == ProjectSDK::GetSDKBuildId(),
          "manifest selected identity matches the host SDK");
    auto info = Info(selection);
    Check(ValidateProjectModuleInfo(info, selection, &error), "binary identity matches selected manifest");
    static const std::string wrong = "wrong";
    info.sdk_build_id = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error) && error.find("sdk.buildId") != std::string::npos,
          "binary SDK mismatch is diagnosed");
    info = Info(selection); info.project_build_id = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error) && error.find("buildId") != std::string::npos,
          "stale same-name binary is rejected");
    info = Info(selection); info.module_id = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error), "binary module identity is checked");
    info = Info(selection); info.project_id = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error), "binary project identity is checked");
    info = Info(selection); info.source_fingerprint = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error), "binary source fingerprint is checked");
    info = Info(selection); info.profile = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error), "binary profile is checked");
    info = Info(selection); info.struct_size = 4;
    Check(!ValidateProjectModuleInfo(info, selection, &error), "short metadata is rejected before reading fields");
    info = Info(selection); info.module_id = {nullptr, 4};
    Check(!ValidateProjectModuleInfo(info, selection, &error), "invalid metadata span is rejected");

    const std::pair<const char *, const char *> invalid[] = {
        {"module.id", "another.module"}, {"project.id", "other-project"}, {"module.abi", "2"},
        {"module.abi", "1invalid"}, {"module.platform", "Windows-x86"},
        {"Development.sdkBuildId", "wrong-sdk"}, {"Development.buildId", "../../outside"},
        {"Development.sourceFingerprint", ""}, {"Development.schemaHash", ""},
        {"Development.dependencies.count", "-1"}, {"Development.dependencies.count", "invalid"},
        {"Development.library", "../outside.dll"}, {"Development.library", "C:/outside.dll"},
        {"Development.library", ""}, {"Development.pdb", "../outside.pdb"},
        {"Development.dependencies.count", "1"}
    };
    for (const auto &entry : invalid)
    {
        f.Manifest(std::string(entry.first) + '=' + entry.second + '\n');
        Check(!LoadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error) && !error.empty(),
              entry.first);
        Check(selection.libraryPath.empty(), "failed selection cannot retain a previously usable DLL");
    }
    f.Manifest();
    settings.codeManifest = "../outside.modules";
    Check(!LoadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error),
          "manifest itself cannot escape project root");
    settings.codeManifest = "Binaries/Project.modules";
    const auto foreign = f.root / "foreign";
    const auto link = f.projectRoot / "Binaries/escaped";
    fs::create_directories(foreign);
    Fixture::Write(foreign / "Game.dll", "outside project");
#ifdef _WIN32
    const auto command = L"cmd.exe /d /c mklink /J \"" + link.wstring() + L"\" \"" + foreign.wstring() + L"\" >nul";
    Check(_wsystem(command.c_str()) == 0, "prepare project-boundary junction fixture");
#else
    fs::create_directory_symlink(foreign, link);
#endif
    f.Manifest("Development.library=Binaries/escaped/Game.dll\n");
    Check(!LoadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error),
          "manifest_rejects_path_escape_through_junction");
    fs::remove(link);
    f.Manifest("Shipping.sdkBuildId=different-shipping-sdk\nsdk.buildId=legacy-global-value\n");
    Check(LoadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error),
          "profiles can retain different SDK identities without invalidating Development");
}

void TINY_PROJECT_CALL RecordEvent(void *context, uint32_t, TinyProjectString text)
{
    static_cast<std::vector<std::string> *>(context)->emplace_back(text.data, static_cast<size_t>(text.size));
}
void LoaderTests()
{
    Fixture f;
    std::vector<std::string> events;
    TinyProjectHostAPI api{}; api.abi_version = TINY_PROJECT_ABI_VERSION; api.struct_size = sizeof(api);
    api.host_context = &events; api.log = &RecordEvent;
    ProjectModuleSelection selection; std::string error;
    Check(LoadProjectModuleSelection(f.Settings(), ProjectCodeProfile::Development, selection, &error), "loader fixture manifest");
    const auto put = [&](const fs::path &source) { fs::copy_file(source, selection.libraryPath, fs::copy_options::overwrite_existing); };
    put(fs::u8path(TINY_TEST_PROJECT_VALID));
    DynamicLibrary direct;
    Check(direct.Open(selection.libraryPath, {}, &error), "RAII opens real unicode DLL path");
    const auto entry = reinterpret_cast<TinyProjectGetAPIFn>(direct.FindSymbol(TINY_PROJECT_ENTRY_POINT));
    Check(entry && !entry(99), "module refuses unknown ABI");
    DynamicLibrary moved(std::move(direct));
    Check(!direct.FindSymbol(TINY_PROJECT_ENTRY_POINT) && moved.FindSymbol(TINY_PROJECT_ENTRY_POINT), "library ownership moves");
    moved.Close(); moved.Close();
    ProjectModuleHost host;
    Check(host.Load(selection, api), "real_module_loads");
    Check(events == std::vector<std::string>{"load"}, "load does not create gameplay session");
    Check(host.GetLoadedBuildId() == selection.projectBuildId && host.GetState() == ProjectModuleState::Registered,
          "registered module exposes build identity");
    Check(host.GetLoadedLibraryPath().parent_path() == f.projectRoot / "Library/ProjectModules/build-1",
          "unicode_shadow_copy_loads");
    Check(!host.Load(selection, api), "loaded module rejects repeated Load");
    host.Close(); host.Close();
    Check(events == std::vector<std::string>({"load", "unload"}) && host.GetLoadedBuildId().empty(), "close_is_idempotent");
    const char *invalid[] = {TINY_TEST_PROJECT_MISSING, TINY_TEST_PROJECT_ABI, TINY_TEST_PROJECT_SHORT,
                            TINY_TEST_PROJECT_IDENTITY, TINY_TEST_PROJECT_CALLBACK};
    for (const auto *binary : invalid)
    {
        put(fs::u8path(binary)); events.clear(); ProjectModuleHost rejected;
        Check(!rejected.Load(selection, api) && !rejected.GetLastError().empty(), "incompatible real binary rejected");
        Check(events.empty() && rejected.GetLoadedLibraryPath().empty(), "only_valid_modules_reach_load_callback");
    }
    const char *compatible[] = {TINY_TEST_PROJECT_OPTIONAL, TINY_TEST_PROJECT_LARGE};
    for (const auto *binary : compatible)
    {
        put(fs::u8path(binary)); events.clear(); ProjectModuleHost compatibleHost;
        Check(compatibleHost.Load(selection, api), "optional absent tail or larger API accepted");
        compatibleHost.Close();
        Check(events == std::vector<std::string>({"load", "unload"}), "compatible table unloads exactly once");
    }
    fs::remove(selection.libraryPath);
    ProjectModuleHost missing;
    Check(!missing.Load(selection, api) && missing.GetState() == ProjectModuleState::Missing, "missing DLL state is explicit");
    PluginManager plugins; std::vector<std::string> pluginLog;
    plugins.SetLogCallback([&](TinyPluginLogLevel, const std::string &s) { pluginLog.push_back(s); });
    Check(plugins.Load(fs::u8path(TINY_TEST_DYNAMIC_PLUGIN)), "existing plugin ABI still loads");
    plugins.Tick(0.016); plugins.UnloadAll();
    Check(std::find(pluginLog.begin(), pluginLog.end(), "dynamic plugin tick") != pluginLog.end(),
          "plugin retained HostAPI remains alive after Startup");
}

void SessionTests()
{
    Fixture f; ProjectModuleSelection selection; std::string error;
    Check(LoadProjectModuleSelection(f.Settings(),ProjectCodeProfile::Development,selection,&error), "session fixture manifest");
    std::vector<std::string> events; TinyProjectHostAPI api{};
    api.abi_version=1; api.struct_size=sizeof(api); api.host_context=&events; api.log=&RecordEvent;
    const auto put = [&](const char *source) { fs::copy_file(fs::u8path(source),selection.libraryPath,fs::copy_options::overwrite_existing); };
    put(TINY_TEST_PROJECT_VALID);
    {
        ProjectModuleHost host; Check(host.Load(selection,api), "session module load");
        for (int i=0;i<2;++i)
        {
            Check(host.BeginPlay({42}) && host.Update(.1f) && host.FixedUpdate(.1f) && host.LateUpdate(.1f), "session callbacks run");
            host.EndPlay(); host.EndPlay();
        }
        Check(std::count(events.begin(),events.end(),"begin")==2 && std::count(events.begin(),events.end(),"destroy")==2,
              "session_begin_end_twice");
        Check(host.BeginPlay({43}), "close active session fixture"); host.Close(); host.Close();
        Check(events[events.size()-3]=="end" && events[events.size()-2]=="destroy" && events.back()=="unload",
              "close orders end destroy unload");
    }
    for (const auto *binary : {TINY_TEST_PROJECT_BEGINFAIL,TINY_TEST_PROJECT_CREATEFAIL})
    {
        put(binary); events.clear(); ProjectModuleHost host;
        Check(host.Load(selection,api) && !host.BeginPlay({42}), "create_or_begin_failure_cleans_once");
        host.EndPlay(); host.Close();
        Check(std::count(events.begin(),events.end(),"destroy")==1 && std::count(events.begin(),events.end(),"end")==0 &&
              !host.GetLastError().empty(), "failed begin is destroyed without EndPlay and preserves diagnostic");
    }
    put(TINY_TEST_PROJECT_UPDATEFAIL); events.clear(); ProjectModuleHost host;
    Check(host.Load(selection,api) && host.BeginPlay({42}) && !host.Update(.1f), "update_failure_faults_session");
    Check(host.GetState()==ProjectModuleState::Faulted && !host.Update(.1f) && !host.FixedUpdate(.1f), "fault prevents further callbacks");
    host.EndPlay(); host.Close();
    Check(std::count(events.begin(),events.end(),"update")==1 && std::count(events.begin(),events.end(),"end")==1 &&
          std::count(events.begin(),events.end(),"destroy")==1, "faulted active session cleans once");
}

struct ThrowingSession : ProjectSDK::Session
{
    void OnBeginPlay(ProjectSDK::Context &) override { throw std::runtime_error("test begin exception"); }
    void OnUpdate(ProjectSDK::Context &, float) override { throw std::runtime_error("test update exception"); }
};
void BridgeTests()
{
    World world; InputSystem input; Logger logger;
    const auto player = world.CreateEntity("Player");
    world.AddComponent(player, TransformComponent{});
    const auto second = world.CreateEntity("Player"); world.AddComponent(second, TransformComponent{});
    ProjectHostBridge bridge(world, input, logger);
    const auto &api = bridge.GetAPI(); auto *c = api.host_context;
    TinyProjectEntity entity{999,999};
    Check(api.find_entity(c, bridge.GetWorld(), Span(std::string("Player")), &entity) == TINY_PROJECT_NOT_READY && !entity.world_id,
          "registration world access is not ready and clears output");
    bridge.SetWorldAccess(true, false);
    ProjectSDK::Context context(api, bridge.GetWorld());
    entity = context.FindEntity("Player");
    Check(entity.value == std::min(player, second) && context.IsAlive(entity), "duplicate names choose stable first entity");
    TinyProjectTransform transform{};
    Check(context.GetTransform(entity, transform), "SDK reads POD transform");
    transform.rotation.y = 90;
    Check(!context.SetTransform(entity, transform) && context.GetLastResult() == TINY_PROJECT_READ_ONLY &&
          world.GetComponent<TransformComponent>(player)->rotation.y == 0, "registration_and_edit_world_read_only");
    bridge.SetWorldAccess(true, true);
    Check(context.SetTransform(entity, transform) && world.GetComponent<TransformComponent>(player)->rotation.y == 90,
          "find_and_rotate_existing_entity");
    transform.position.x = std::numeric_limits<float>::infinity();
    Check(!context.SetTransform(entity, transform) && context.GetLastResult() == TINY_PROJECT_INVALID_ARGUMENT,
          "nonfinite transforms are rejected");
    InputState state{}; state.moveX = .75f; state.jump = true; input.Update(state);
    Check(context.GetAxis("MoveX") == .75f && context.IsDown("Jump"), "input_axis_and_action_bridge");
    const void *extension = &world;
    Check(api.query_interface(c, Span(std::string("unknown")), 1, &extension) == TINY_PROJECT_UNSUPPORTED && !extension,
          "unknown extension clears output");
    Check(api.get_transform(c, entity, nullptr) == TINY_PROJECT_INVALID_ARGUMENT, "null output rejected");
    transform.position.x = 12;
    Check(api.get_transform(c, TinyProjectEntity{999,999}, &transform) != TINY_PROJECT_OK && transform.position.x == 0,
          "invalid_out_parameters_are_cleared");
    TinyProjectResult threaded = TINY_PROJECT_OK;
    std::thread worker([&] { uint32_t alive = 99; threaded = api.is_alive(c, entity, &alive); if (alive) threaded = -1; });
    worker.join(); Check(threaded == TINY_PROJECT_WRONG_THREAD, "wrong_thread_rejected");
    void *memory = &world;
    Check(api.allocate(c, 64, 3, &memory) == TINY_PROJECT_INVALID_ARGUMENT && !memory, "invalid alignment clears allocation");
    Check(api.allocate(c, 64, 32, &memory) == TINY_PROJECT_OK && reinterpret_cast<uintptr_t>(memory) % 32 == 0,
          "host aligned allocation succeeds"); api.deallocate(c, memory, 32);
    const auto oldWorld = bridge.GetWorld(); world.Clear(); bridge.InvalidateWorld();
    world.CreateEntity("Player"); uint32_t alive = 99;
    Check(bridge.GetWorld().id != oldWorld.id && api.is_alive(c, entity, &alive) != TINY_PROJECT_OK && !alive,
          "stale_world_entity_rejected");
    const auto info = TinyProjectModuleInfo{};
    const auto *exported = ProjectSDK::ExportProject<ThrowingSession>(TINY_PROJECT_ABI_VERSION, info);
    Check(exported && !ProjectSDK::ExportProject<ThrowingSession>(99, info), "SDK adapts only supported ABI");
    void *module = nullptr, *session = nullptr;
    Check(exported->load(&api, &module) == TINY_PROJECT_OK && exported->create_session(module, bridge.GetWorld(), &session) == TINY_PROJECT_OK,
          "SDK allocates module and session in same library");
    Check(exported->begin_play(session) == TINY_PROJECT_FAILED && exported->update(session, .01f) == TINY_PROJECT_FAILED,
          "sdk_exception_does_not_cross_c_callback");
    exported->destroy_session(session); exported->unload(module);
}
}

static int RunMain(int argc, char **argv)
{
    try
    {
        if (argc==4 && std::string(argv[1])=="--inspect-project")
        {
            ProjectSettings settings; Check(settings.Load(fs::u8path(argv[2])),"load external project");
            ProjectModuleSelection selection; std::string error;
            const auto profile=std::string(argv[3])=="Shipping" ? ProjectCodeProfile::Shipping : ProjectCodeProfile::Development;
            Check(LoadProjectModuleSelection(settings,profile,selection,&error),error.empty()?"load external manifest":error.c_str());
            World world; const auto player=world.CreateEntity("Player"); InputSystem input; Logger logger;
            ProjectHostBridge bridge(world,input,logger); ProjectModuleHost host;
            Check(host.Load(selection,bridge.GetAPI()),"external binary matches generated manifest identity");
            bridge.SetWorldAccess(true,true);
            Check(host.BeginPlay(bridge.GetWorld()) && host.Update(1),"external C++ SDK session executes");
            Check(world.GetComponent<TransformComponent>(player)->rotation.y==90,"external project rotates actual engine Player");
            host.EndPlay(); host.Close(); return 0;
        }
        const std::string group = argc == 3 && std::string(argv[1]) == "--group" ? argv[2] : "manifest";
        if (group == "manifest") ManifestTests();
        else if (group == "loader") LoaderTests();
        else if (group == "bridge") BridgeTests();
        else if (group == "session") SessionTests();
        else throw std::runtime_error("unknown test group");
        std::cout << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) { return MiniEngine::RunWideMain(argc,argv,RunMain); }
#else
int main(int argc,char **argv) { return RunMain(argc,argv); }
#endif
