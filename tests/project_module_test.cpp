#include "tinyengine/core/logger.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/input/input_system.h"
#include "tinyengine/platform/dynamic_library.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/platform/utf8_arguments.h"
#include "tinyengine/plugin/plugin_manager.h"
#include "tinyengine/project/project.h"
#include "tinyengine/project/project_host_bridge.h"
#include "tinyengine/project/project_module_host.h"
#include "tinyengine/project/project_module_manifest.h"
#include "tinyengine/project_sdk/project.h"
#include "tinyengine/project_sdk/sdk_info.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace tinyengine;

namespace
{
int checks = 0;
void check(bool condition, const char* name)
{
    if (!condition)
        throw std::runtime_error(name);
    ++checks;
    std::cout << "PASS " << name << '\n';
}

struct Fixture
{
    fs::path root =
        fs::temp_directory_path() /
        ("TinyEngineProjectTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path projectRoot = root / fromUtf8Path("项目 with spaces");
    fs::path projectFile = projectRoot / "Game.tinyproject";
    fs::path manifestFile = projectRoot / "Binaries/Project.modules";
    Fixture()
    {
        fs::create_directories(projectRoot / "Binaries/Development");
        write(projectFile, "project.name=Fixture\nproject.id=p0-project\ncode.module=game.test\ncode.manifest=Binaries/"
                           "Project.modules\n");
        write(projectRoot / "Binaries/Development/Game.dll", "test binary fixture");
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
    static void write(const fs::path& path, const std::string& text)
    {
        check(PlatformFile::writeAllText(path, text), "write controlled project fixture");
    }
    void Manifest(const std::string& extra = {})
    {
        write(manifestFile,
              std::string("module.id=game.test\nproject.id=p0-project\nmodule.abi=1\nmodule.platform=Windows-x64\n") +
                  "Development.sdkBuildId=" + project_sdk::getSdkBuildId() + "\n" +
                  "Development.library=Binaries/Development/Game.dll\nDevelopment.pdb=\n" +
                  "Development.buildId=build-1\nDevelopment.sourceFingerprint=source-1\n" +
                  "Development.schemaHash=none\nDevelopment.dependencies.count=0\n" +
                  "Shipping.sdkBuildId=" + project_sdk::getSdkBuildId() + "\n" +
                  "Shipping.library=Binaries/Development/Game.dll\nShipping.pdb=\n" +
                  "Shipping.buildId=build-1\nShipping.sourceFingerprint=source-1\n" +
                  "Shipping.schemaHash=none\nShipping.dependencies.count=0\n" + extra);
    }
    ProjectSettings settings() const
    {
        ProjectSettings settings;
        check(settings.load(projectFile), "load configured project");
        return settings;
    }
};

TinyProjectString span(const std::string& value)
{
    return {value.data(), static_cast<uint64_t>(value.size())};
}
TinyProjectModuleInfo makeModuleInfo(const ProjectModuleSelection& selection)
{
    static const std::string kProfile = "Development";
    return {sizeof(TinyProjectModuleInfo),
            span(selection.moduleId),
            span(selection.projectId),
            span(selection.sdkBuildId),
            span(selection.projectBuildId),
            span(selection.sourceFingerprint),
            span(kProfile)};
}

void manifestTests()
{
    Fixture f;
    ProjectSettings settings = f.settings();
    check(settings.codeRequired, "code_required_defaults_true");
    check(settings.projectId == "p0-project", "project identity is preserved");
    const auto saved = f.projectRoot / "Saved.tinyproject";
    check(settings.Save(saved), "save code settings");
    ProjectSettings reloaded;
    check(reloaded.load(saved) && reloaded.codeModule == "game.test" && reloaded.codeRequired,
          "project code settings round trip");
    const auto legacy = f.projectRoot / "Legacy.tinyproject";
    Fixture::write(legacy, "project.name=Legacy\n");
    check(reloaded.load(legacy) && reloaded.codeModule.empty() && reloaded.codeManifest.empty() &&
              reloaded.projectId.empty() && !reloaded.codeRequired,
          "project_load_does_not_retain_previous_code_fields");
    Fixture::write(f.projectFile, "project.name=Optional\nproject.id=p0-project\ncode.module=game.test\ncode.required="
                                  "false\ncode.manifest=Binaries/Project.modules\n");
    check(reloaded.load(f.projectFile) && !reloaded.codeRequired, "explicit optional code setting is honored");
    Fixture::write(f.projectFile, "code.module=game.test\n");
    check(!reloaded.load(f.projectFile), "incomplete code settings are rejected");

    std::string error;
    ProjectModuleSelection selection;
    const auto previous = fs::current_path();
    fs::current_path(f.root);
    const bool loaded = loadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error);
    fs::current_path(previous);
    check(loaded && error.empty(), "manifest_resolves_from_project_root");
    check(fs::equivalent(selection.libraryPath, f.projectRoot / "Binaries/Development/Game.dll"),
          "unicode and spaces resolve the intended DLL");
    check(selection.projectBuildId == "build-1" && selection.sdkBuildId == project_sdk::getSdkBuildId(),
          "manifest selected identity matches the host SDK");
    auto info = makeModuleInfo(selection);
    check(validateProjectModuleInfo(info, selection, &error), "binary identity matches selected manifest");
    static const std::string kWrong = "wrong";
    info.sdk_build_id = span(kWrong);
    check(!validateProjectModuleInfo(info, selection, &error) && error.find("sdk.buildId") != std::string::npos,
          "binary SDK mismatch is diagnosed");
    info = makeModuleInfo(selection);
    info.project_build_id = span(kWrong);
    check(!validateProjectModuleInfo(info, selection, &error) && error.find("buildId") != std::string::npos,
          "stale same-name binary is rejected");
    info = makeModuleInfo(selection);
    info.module_id = span(kWrong);
    check(!validateProjectModuleInfo(info, selection, &error), "binary module identity is checked");
    info = makeModuleInfo(selection);
    info.project_id = span(kWrong);
    check(!validateProjectModuleInfo(info, selection, &error), "binary project identity is checked");
    info = makeModuleInfo(selection);
    info.source_fingerprint = span(kWrong);
    check(!validateProjectModuleInfo(info, selection, &error), "binary source fingerprint is checked");
    info = makeModuleInfo(selection);
    info.profile = span(kWrong);
    check(!validateProjectModuleInfo(info, selection, &error), "binary profile is checked");
    info = makeModuleInfo(selection);
    info.struct_size = 4;
    check(!validateProjectModuleInfo(info, selection, &error), "short metadata is rejected before reading fields");
    info = makeModuleInfo(selection);
    info.module_id = {nullptr, 4};
    check(!validateProjectModuleInfo(info, selection, &error), "invalid metadata span is rejected");

    const std::pair<const char*, const char*> invalid[] = {{"module.id", "another.module"},
                                                           {"project.id", "other-project"},
                                                           {"module.abi", "2"},
                                                           {"module.abi", "1invalid"},
                                                           {"module.platform", "Windows-x86"},
                                                           {"Development.sdkBuildId", "wrong-sdk"},
                                                           {"Development.buildId", "../../outside"},
                                                           {"Development.sourceFingerprint", ""},
                                                           {"Development.schemaHash", ""},
                                                           {"Development.dependencies.count", "-1"},
                                                           {"Development.dependencies.count", "invalid"},
                                                           {"Development.library", "../outside.dll"},
                                                           {"Development.library", "C:/outside.dll"},
                                                           {"Development.library", ""},
                                                           {"Development.pdb", "../outside.pdb"},
                                                           {"Development.dependencies.count", "1"}};
    for (const auto& entry : invalid)
    {
        f.Manifest(std::string(entry.first) + '=' + entry.second + '\n');
        check(!loadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error) &&
                  !error.empty(),
              entry.first);
        check(selection.libraryPath.empty(), "failed selection cannot retain a previously usable DLL");
    }
    f.Manifest();
    settings.codeManifest = "../outside.modules";
    check(!loadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error),
          "manifest itself cannot escape project root");
    settings.codeManifest = "Binaries/Project.modules";
    const auto foreign = f.root / "foreign";
    const auto link = f.projectRoot / "Binaries/escaped";
    fs::create_directories(foreign);
    Fixture::write(foreign / "Game.dll", "outside project");
#ifdef _WIN32
    const auto command = L"cmd.exe /d /c mklink /J \"" + link.wstring() + L"\" \"" + foreign.wstring() + L"\" >nul";
    check(_wsystem(command.c_str()) == 0, "prepare project-boundary junction fixture");
#else
    fs::create_directory_symlink(foreign, link);
#endif
    f.Manifest("Development.library=Binaries/escaped/Game.dll\n");
    check(!loadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error),
          "manifest_rejects_path_escape_through_junction");
    fs::remove(link);
    f.Manifest("Shipping.sdkBuildId=different-shipping-sdk\nsdk.buildId=legacy-global-value\n");
    check(loadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error),
          "profiles can retain different SDK identities without invalidating Development");
}

void TINY_PROJECT_CALL RecordEvent(void* context, uint32_t, TinyProjectString text)
{
    static_cast<std::vector<std::string>*>(context)->emplace_back(text.data, static_cast<size_t>(text.size));
}
constexpr ProjectCodeProfile kBinaryProfile =
#ifdef NDEBUG
    ProjectCodeProfile::Shipping;
#else
    ProjectCodeProfile::Development;
#endif

void loaderTests()
{
    Fixture f;
    std::vector<std::string> events;
    TinyProjectHostAPI api{};
    api.abi_version = TINY_PROJECT_ABI_VERSION;
    api.struct_size = sizeof(api);
    api.host_context = &events;
    api.log = &RecordEvent;
    ProjectModuleSelection selection;
    std::string error;
    check(loadProjectModuleSelection(f.settings(), kBinaryProfile, selection, &error), "loader fixture manifest");
    const auto put = [&](const fs::path& source)
    { fs::copy_file(source, selection.libraryPath, fs::copy_options::overwrite_existing); };
    put(fromUtf8Path(TINY_TEST_PROJECT_VALID));
    DynamicLibrary direct;
    check(direct.Open(selection.libraryPath, {}, &error), "RAII opens real unicode DLL path");
    const auto entry = reinterpret_cast<TinyProjectGetAPIFn>(direct.findSymbol(TINY_PROJECT_ENTRY_POINT));
    check(entry && !entry(99), "module refuses unknown ABI");
    DynamicLibrary moved(std::move(direct));
    check(!direct.findSymbol(TINY_PROJECT_ENTRY_POINT) && moved.findSymbol(TINY_PROJECT_ENTRY_POINT),
          "library ownership moves");
    moved.Close();
    moved.Close();
    ProjectModuleHost host;
    check(host.load(selection, api), "real_module_loads");
    check(events == std::vector<std::string>{"load"}, "load does not create gameplay session");
    check(host.GetLoadedBuildId() == selection.projectBuildId && host.getState() == ProjectModuleState::Registered,
          "registered module exposes build identity");
    check(host.GetLoadedLibraryPath().parent_path() == f.projectRoot / "Library/ProjectModules/build-1",
          "unicode_shadow_copy_loads");
    check(!host.load(selection, api), "loaded module rejects repeated Load");
    host.Close();
    host.Close();
    check(events == std::vector<std::string>({"load", "unload"}) && host.GetLoadedBuildId().empty(),
          "close_is_idempotent");
    const char* invalid[] = {TINY_TEST_PROJECT_MISSING, TINY_TEST_PROJECT_ABI, TINY_TEST_PROJECT_SHORT,
                             TINY_TEST_PROJECT_IDENTITY, TINY_TEST_PROJECT_CALLBACK};
    for (const auto* binary : invalid)
    {
        put(fromUtf8Path(binary));
        events.clear();
        ProjectModuleHost rejected;
        check(!rejected.load(selection, api) && !rejected.GetLastError().empty(), "incompatible real binary rejected");
        check(events.empty() && rejected.GetLoadedLibraryPath().empty(), "only_valid_modules_reach_load_callback");
    }
    const char* compatible[] = {TINY_TEST_PROJECT_OPTIONAL, TINY_TEST_PROJECT_LARGE};
    for (const auto* binary : compatible)
    {
        put(fromUtf8Path(binary));
        events.clear();
        ProjectModuleHost compatibleHost;
        check(compatibleHost.load(selection, api), "optional absent tail or larger API accepted");
        compatibleHost.Close();
        check(events == std::vector<std::string>({"load", "unload"}), "compatible table unloads exactly once");
    }
    fs::remove(selection.libraryPath);
    ProjectModuleHost missing;
    check(!missing.load(selection, api) && missing.getState() == ProjectModuleState::Missing,
          "missing DLL state is explicit");
    PluginManager plugins;
    std::vector<std::string> pluginLog;
    plugins.setLogCallback([&](TinyPluginLogLevel, const std::string& s) { pluginLog.push_back(s); });
    check(plugins.load(fromUtf8Path(TINY_TEST_DYNAMIC_PLUGIN)), "existing plugin ABI still loads");
    plugins.Tick(0.016);
    plugins.unloadAll();
    check(std::find(pluginLog.begin(), pluginLog.end(), "dynamic plugin tick") != pluginLog.end(),
          "plugin retained HostAPI remains alive after Startup");
    fs::create_directories(f.projectRoot / "Plugins");
    const auto dependent = f.projectRoot / "Plugins/Dependent.dll";
    fs::copy_file(fromUtf8Path(TINY_TEST_DEPENDENT_PLUGIN), dependent);
    check(plugins.load(dependent), "generic plugin resolves dependencies beside host executable");
    plugins.unloadAll();
}

void sessionTests()
{
    Fixture f;
    ProjectModuleSelection selection;
    std::string error;
    check(loadProjectModuleSelection(f.settings(), kBinaryProfile, selection, &error), "session fixture manifest");
    std::vector<std::string> events;
    TinyProjectHostAPI api{};
    api.abi_version = 1;
    api.struct_size = sizeof(api);
    api.host_context = &events;
    api.log = &RecordEvent;
    const auto put = [&](const char* source)
    { fs::copy_file(fromUtf8Path(source), selection.libraryPath, fs::copy_options::overwrite_existing); };
    put(TINY_TEST_PROJECT_VALID);
    {
        ProjectModuleHost host;
        check(host.load(selection, api), "session module load");
        for (int i = 0; i < 2; ++i)
        {
            check(host.beginPlay({42}) && host.Update(.1f) && host.fixedUpdate(.1f) && host.lateUpdate(.1f),
                  "session callbacks run");
            host.endPlay();
            host.endPlay();
        }
        check(std::count(events.begin(), events.end(), "begin") == 2 &&
                  std::count(events.begin(), events.end(), "destroy") == 2,
              "session_begin_end_twice");
        check(host.beginPlay({43}), "close active session fixture");
        host.Close();
        host.Close();
        check(events[events.size() - 3] == "end" && events[events.size() - 2] == "destroy" && events.back() == "unload",
              "close orders end destroy unload");
    }
    for (const auto* binary : {TINY_TEST_PROJECT_BEGINFAIL, TINY_TEST_PROJECT_CREATEFAIL})
    {
        put(binary);
        events.clear();
        ProjectModuleHost host;
        check(host.load(selection, api) && !host.beginPlay({42}), "create_or_begin_failure_cleans_once");
        host.endPlay();
        host.Close();
        check(std::count(events.begin(), events.end(), "destroy") == 1 &&
                  std::count(events.begin(), events.end(), "end") == 0 && !host.GetLastError().empty(),
              "failed begin is destroyed without EndPlay and preserves diagnostic");
    }
    put(TINY_TEST_PROJECT_UPDATEFAIL);
    events.clear();
    ProjectModuleHost host;
    check(host.load(selection, api) && host.beginPlay({42}) && !host.Update(.1f), "update_failure_faults_session");
    check(host.getState() == ProjectModuleState::Faulted && !host.Update(.1f) && !host.fixedUpdate(.1f),
          "fault prevents further callbacks");
    host.endPlay();
    host.Close();
    check(std::count(events.begin(), events.end(), "update") == 1 &&
              std::count(events.begin(), events.end(), "end") == 1 &&
              std::count(events.begin(), events.end(), "destroy") == 1,
          "faulted active session cleans once");
}

struct ThrowingSession : project_sdk::Session
{
    void onBeginPlay(project_sdk::Context&) override
    {
        throw std::runtime_error("test begin exception");
    }
    void onUpdate(project_sdk::Context&, float) override
    {
        throw std::runtime_error("test update exception");
    }
};
void bridgeTests()
{
    World world;
    InputSystem input;
    Logger logger;
    const auto player = world.createEntity("Player");
    world.AddComponent(player, TransformComponent{});
    const auto second = world.createEntity("Player");
    world.AddComponent(second, TransformComponent{});
    ProjectHostBridge bridge(world, input, logger);
    const auto& api = bridge.GetAPI();
    auto* c = api.host_context;
    TinyProjectEntity entity{999, 999};
    check(api.find_entity(c, bridge.getWorld(), span(std::string("Player")), &entity) == TINY_PROJECT_NOT_READY &&
              !entity.world_id,
          "registration world access is not ready and clears output");
    bridge.setWorldAccess(true, false);
    project_sdk::Context context(api, bridge.getWorld());
    entity = context.findEntity("Player");
    check(entity.value == std::min(player, second) && context.isAlive(entity),
          "duplicate names choose stable first entity");
    TinyProjectTransform transform{};
    check(context.getTransform(entity, transform), "SDK reads POD transform");
    transform.rotation.y = 90;
    check(!context.setTransform(entity, transform) && context.getLastResult() == TINY_PROJECT_READ_ONLY &&
              world.GetComponent<TransformComponent>(player)->rotation.y == 0,
          "registration_and_edit_world_read_only");
    bridge.setWorldAccess(true, true);
    check(context.setTransform(entity, transform) && world.GetComponent<TransformComponent>(player)->rotation.y == 90,
          "find_and_rotate_existing_entity");
    transform.position.x = std::numeric_limits<float>::infinity();
    check(!context.setTransform(entity, transform) && context.getLastResult() == TINY_PROJECT_INVALID_ARGUMENT,
          "nonfinite transforms are rejected");
    InputState state{};
    state.moveX = .75f;
    state.jump = true;
    input.Update(state);
    check(context.getAxis("MoveX") == .75f && context.isDown("Jump"), "input_axis_and_action_bridge");
    const void* extension = &world;
    check(api.query_interface(c, span(std::string("unknown")), 1, &extension) == TINY_PROJECT_UNSUPPORTED && !extension,
          "unknown extension clears output");
    check(api.get_transform(c, entity, nullptr) == TINY_PROJECT_INVALID_ARGUMENT, "null output rejected");
    transform.position.x = 12;
    check(api.get_transform(c, TinyProjectEntity{999, 999}, &transform) != TINY_PROJECT_OK && transform.position.x == 0,
          "invalid_out_parameters_are_cleared");
    TinyProjectResult threaded = TINY_PROJECT_OK;
    std::thread worker(
        [&]
        {
            uint32_t alive = 99;
            threaded = api.is_alive(c, entity, &alive);
            if (alive)
                threaded = -1;
        });
    worker.join();
    check(threaded == TINY_PROJECT_WRONG_THREAD, "wrong_thread_rejected");
    void* memory = &world;
    check(api.allocate(c, 64, 3, &memory) == TINY_PROJECT_INVALID_ARGUMENT && !memory,
          "invalid alignment clears allocation");
    check(api.allocate(c, 64, 32, &memory) == TINY_PROJECT_OK && reinterpret_cast<uintptr_t>(memory) % 32 == 0,
          "host aligned allocation succeeds");
    api.deallocate(c, memory, 32);
    const auto oldWorld = bridge.getWorld();
    world.clear();
    bridge.invalidateWorld();
    world.createEntity("Player");
    uint32_t alive = 99;
    check(bridge.getWorld().id != oldWorld.id && api.is_alive(c, entity, &alive) != TINY_PROJECT_OK && !alive,
          "stale_world_entity_rejected");
    const auto info = TinyProjectModuleInfo{};
    const auto* exported = project_sdk::ExportProject<ThrowingSession>(TINY_PROJECT_ABI_VERSION, info);
    check(exported && !project_sdk::ExportProject<ThrowingSession>(99, info), "SDK adapts only supported ABI");
    void *module = nullptr, *session = nullptr;
    check(exported->load(&api, &module) == TINY_PROJECT_OK &&
              exported->create_session(module, bridge.getWorld(), &session) == TINY_PROJECT_OK,
          "SDK allocates module and session in same library");
    check(exported->begin_play(session) == TINY_PROJECT_FAILED &&
              exported->update(session, .01f) == TINY_PROJECT_FAILED,
          "sdk_exception_does_not_cross_c_callback");
    exported->destroy_session(session);
    exported->unload(module);
}
} // namespace

static int RunMain(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--probe-value")
        {
            DynamicLibrary library;
            std::string error;
            check(library.Open(fromUtf8Path(argv[2]), {}, &error), "open fingerprint probe DLL");
            const auto value = reinterpret_cast<int (*)()>(library.findSymbol("TinyTest_ReviewValue"));
            check(value != nullptr, "fingerprint probe symbol exists");
            std::cout << "REVIEW_VALUE=" << value() << '\n';
            return 0;
        }
        if (argc == 4 && std::string(argv[1]) == "--inspect-project")
        {
            ProjectSettings settings;
            check(settings.load(fromUtf8Path(argv[2])), "load external project");
            ProjectModuleSelection selection;
            std::string error;
            const auto kProfile =
                std::string(argv[3]) == "Shipping" ? ProjectCodeProfile::Shipping : ProjectCodeProfile::Development;
            const bool manifestLoaded = loadProjectModuleSelection(settings, kProfile, selection, &error);
            check(manifestLoaded, error.empty() ? "load external manifest" : error.c_str());
            World world;
            const auto player = world.createEntity("Player");
            InputSystem input;
            Logger logger;
            ProjectHostBridge bridge(world, input, logger);
            ProjectModuleHost host;
            check(host.load(selection, bridge.GetAPI()), "external binary matches generated manifest identity");
            bridge.setWorldAccess(true, true);
            check(host.beginPlay(bridge.getWorld()) && host.Update(1), "external C++ SDK session executes");
            check(world.GetComponent<TransformComponent>(player)->rotation.y == 90,
                  "external project rotates actual engine Player");
            host.endPlay();
            host.Close();
            return 0;
        }
        const std::string group = argc == 3 && std::string(argv[1]) == "--group" ? argv[2] : "manifest";
        if (group == "manifest")
            manifestTests();
        else if (group == "loader")
            loaderTests();
        else if (group == "bridge")
            bridgeTests();
        else if (group == "session")
            sessionTests();
        else
            throw std::runtime_error("unknown test group");
        std::cout << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    return tinyengine::runWideMain(argc, argv, RunMain);
}
#else
int main(int argc, char** argv)
{
    return RunMain(argc, argv);
}
#endif
