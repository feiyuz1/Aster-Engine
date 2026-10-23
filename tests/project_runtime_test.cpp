#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/platform/utf8_arguments.h"
#include "tinyengine/project_sdk/sdk_info.h"
#include "tinyengine/runtime/engine.h"
#include "tinyengine/render/sdl_gl_render_device.h"
#include "tinyengine/ui/ui_system.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace tinyengine;
namespace fs = std::filesystem;
namespace
{
void check(bool value, const char* name)
{
    if (!value)
        throw std::runtime_error(name);
    std::cout << "PASS " << name << '\n';
}
struct Fixture
{
    fs::path root =
        fs::temp_directory_path() /
        ("TinyEngineRuntimeTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path project = root / "Runtime.tinyproject";
    Fixture()
    {
        fs::create_directories(root / "Binaries");
        fs::create_directories(root / "assets");
        World world;
        world.createEntity("Player");
        check(SceneSerializer::Save(world, root / "Start.scene"), "save real runtime scene");
        ProjectSettings settings;
        settings.projectId = "p0-project";
        settings.codeModule = "game.test";
        settings.codeRequired = true;
        settings.codeManifest = "Binaries/Project.modules";
        settings.startupScene = "Start.scene";
        check(settings.Save(project), "save real runtime project");
        publish(TINY_TEST_PROJECT_VALID);
    }
    void publish(const char* binary)
    {
        fs::copy_file(fromUtf8Path(binary), root / "Binaries/Game.dll", fs::copy_options::overwrite_existing);
        const std::string text = std::string("module.id=game.test\nproject.id=p0-project\nmodule.abi=1\nmodule."
                                             "platform=Windows-x64\nDevelopment.sdkBuildId=") +
                                 project_sdk::getSdkBuildId() +
                                 "\nDevelopment.library=Binaries/"
                                 "Game.dll\nDevelopment.buildId=build-1\nDevelopment.sourceFingerprint=source-"
                                 "1\nDevelopment.schemaHash=none\nDevelopment.dependencies.count=0\n";
        check(PlatformFile::writeAllText(root / "Binaries/Project.modules", text), "publish runtime manifest");
    }
    ~Fixture()
    {
        std::error_code e;
        const auto path = fs::absolute(root, e).lexically_normal();
        if (!e && path.parent_path() == fs::absolute(fs::temp_directory_path()).lexically_normal() &&
            path.filename().string().find("TinyEngineRuntimeTests-") == 0)
            fs::remove_all(path, e);
    }
};
struct Legacy : GameInstance
{
    int& updates;
    explicit Legacy(int& v) : updates(v) {}
    void onUpdate(float) override
    {
        ++updates;
    }
};
Entity player(Engine& engine)
{
    for (auto e : engine.GetContext().world->GetEntityManager().GetAliveEntities())
        if (engine.GetContext().world->GetComponent<NameComponent>(e)->name == "Player")
            return e;
    return kInvalidEntity;
}
float rotation(Engine& engine)
{
    return engine.GetContext().world->GetComponent<TransformComponent>(player(engine))->rotation.y;
}
std::uint64_t addHostResources(Engine& engine)
{
    auto& context = engine.GetContext();
    context.uiSystem->AddLabel("fixture.host", "Host", {0, 24, 60, 20});
    ParticleEmitterSettings settings;
    settings.emissionRate = 0;
    settings.lifetime = 100;
    const auto emitter = context.vfxSystem->createEmitter(settings);
    context.vfxSystem->findEmitter(emitter)->burst(1);
    context.renderer->setRetroPostProcess(true, 5.0f, 24.0f);
    return emitter;
}
void checkSessionCleanup(Engine& engine, std::uint64_t hostEmitter)
{
    const auto& context = engine.GetContext();
    check(context.vfxSystem->findEmitter(hostEmitter) && context.vfxSystem->BuildParticleList().size() == 1,
          "play cleanup removes project particles and preserves the host emitter");
    check(!context.uiSystem->Find("fixture.runtime") && context.uiSystem->Find("fixture.host"),
          "play cleanup removes project UI and preserves the host widget");
    check(context.renderer->isRetroPostProcessEnabled() && context.renderer->getRetroBlockSize() == 5.0f &&
              context.renderer->getRetroColorLevels() == 24.0f,
          "play cleanup restores the complete pre-project retro settings");
    check(!context.renderer->isMouseCaptured(), "play cleanup releases project mouse capture");
}
void runtimeTests()
{
    Fixture f;
    Engine engine;
    int legacyUpdates = 0;
    engine.setGameInstance(std::make_unique<Legacy>(legacyUpdates));
    check(engine.initialize(f.project, false), "editor initializes registered project module");
    const auto hostEmitter = addHostResources(engine);
    const auto snapshot = SceneSerializer::serialize(*engine.GetContext().world);
    engine.Run(2);
    check(rotation(engine) == 0, "edit mode skips project updates");
    for (int i = 0; i < 2; ++i)
    {
        check(engine.enterPlayMode(), "editor_play_calls_project_code");
        check(engine.GetContext().vfxSystem->BuildParticleList().size() == 7 &&
                  engine.GetContext().uiSystem->Find("fixture.runtime"),
              "project begin creates particles and UI in the current session");
        engine.Run(3);
        check(rotation(engine) > 0 && legacyUpdates == 0,
              "project updates transform without double updating legacy GameInstance");
        engine.setPaused(true);
        const auto paused = rotation(engine);
        engine.Run(2);
        check(rotation(engine) == paused, "pause_skips_project_updates");
        check(engine.exitPlayMode() && SceneSerializer::serialize(*engine.GetContext().world) == snapshot,
              "stop_restores_snapshot_and_invalidates_handles");
        checkSessionCleanup(engine, hostEmitter);
    }
    {
        check(engine.enterPlayMode(), "automatic world epoch test begins");
        DynamicLibrary probe;
        std::string error;
        check(probe.Open(engine.GetProjectModuleHost().GetLoadedLibraryPath(), {}, &error),
              "open real module diagnostic symbol");
        const auto alive =
            reinterpret_cast<uint32_t(TINY_PROJECT_CALL*)()>(probe.findSymbol("TinyTest_PreviousPlayerAlive"));
        const auto findOld = reinterpret_cast<TinyProjectResult(TINY_PROJECT_CALL*)()>(
            probe.findSymbol("TinyTest_FindWithPreviousWorld"));
        check(alive && alive() == 1, "project holds a live handle");
        auto& world = *engine.GetContext().world;
        world.destroyEntity(player(engine));
        world.clear();
        world.createEntity("Player");
        check(alive() == 0, "empty world reset invalidates project handles automatically");
        check(findOld && findOld() == TINY_PROJECT_NOT_FOUND,
              "empty world reset invalidates world token automatically");
        check(engine.exitPlayMode(), "world epoch test restores scene");
        checkSessionCleanup(engine, hostEmitter);
        check(alive() == 0, "stop invalidates old project handles automatically");
    }
    engine.shutdown();
    {
        Engine game;
        check(game.initialize(f.project, true) && game.isPlaying(), "standalone_start_calls_same_project_code");
        SDL_Event escape{};
        escape.type = SDL_KEYDOWN;
        escape.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
        escape.key.keysym.sym = SDLK_ESCAPE;
        check(SDL_PushEvent(&escape) == 1 && game.GetContext().renderer->pumpEvents(),
              "standalone project receives Escape without ending the platform loop");
        check(game.GetContext().renderer->GetInputState().keysPressed[SDL_SCANCODE_ESCAPE],
              "standalone project retains the Escape press edge");
        check(game.GetContext().renderer->pumpEvents() &&
                  !game.GetContext().renderer->GetInputState().keysPressed[SDL_SCANCODE_ESCAPE],
              "Escape press edge clears on the following frame");
        game.Run(3);
        check(rotation(game) > 0, "standalone rotates same scene object");
        auto* standaloneUi = game.GetContext().uiSystem;
        SDL_Event close{};
        close.type = SDL_QUIT;
        check(SDL_PushEvent(&close) == 1 && !game.GetContext().renderer->pumpEvents(),
              "standalone project still exits on a window close event");
        game.shutdown();
        check(!standaloneUi->Find("fixture.runtime"), "shutdown removes project-owned UI");
        game.shutdown();
    }
    fs::remove(f.root / "Binaries/Game.dll");
    {
        Engine missing;
        check(missing.initialize(f.project, false), "required missing code keeps editor scene open");
        check(player(missing) != kInvalidEntity && !missing.enterPlayMode() && !missing.GetProjectCodeError().empty(),
              "required_missing_code_blocks_play_but_keeps_editor_scene");
        missing.shutdown();
        Engine game;
        check(!game.initialize(f.project, true), "required missing code rejects standalone startup");
        game.shutdown();
    }
    for (const auto* binary : {TINY_TEST_PROJECT_BEGINFAIL, TINY_TEST_PROJECT_UPDATEFAIL})
    {
        f.publish(binary);
        Engine failing;
        check(failing.initialize(f.project, false), "fault fixture initializes editor");
        const auto failureHostEmitter = addHostResources(failing);
        const auto before = SceneSerializer::serialize(*failing.GetContext().world);
        if (failing.enterPlayMode())
            failing.Run(3);
        check(!failing.isPlaying() && SceneSerializer::serialize(*failing.GetContext().world) == before &&
                  !failing.GetProjectCodeError().empty(),
              "project callback failure stops and restores edit snapshot");
        checkSessionCleanup(failing, failureHostEmitter);
        failing.shutdown();
    }
    Engine absent;
    check(!absent.initialize(f.root / "DoesNotExist.tinyproject", false), "explicit invalid project never opens demo");
    absent.shutdown();
    f.publish(TINY_TEST_PROJECT_VALID);
    fs::remove(f.root / "Start.scene");
    Engine badScene;
    check(!badScene.initialize(f.project, false), "explicit invalid startup scene never opens demo");
    badScene.shutdown();
}
} // namespace
static int RunMain(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--write-scene")
        {
            World world;
            world.createEntity("Player");
            check(SceneSerializer::Save(world, fromUtf8Path(argv[2])), "write QA TINYSCENE 11");
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--external-project")
        {
            Engine engine;
            check(engine.initialize(fromUtf8Path(argv[2]), false), "PIE uses installed external project DLL");
            const auto snapshot = SceneSerializer::serialize(*engine.GetContext().world);
            const auto& host = engine.GetProjectModuleHost();
#ifndef NDEBUG
            check(fs::is_regular_file(host.GetLoadedPdbPath()) &&
                      host.GetLoadedLibraryPath().stem() == host.GetLoadedPdbPath().stem(),
                  "shadow DLL and matching PDB are present");
#endif
            for (int i = 0; i < 2; ++i)
            {
                check(engine.enterPlayMode(), "external SDK project enters PIE");
                engine.Run(3);
                check(rotation(engine) > 0, "external SDK project rotates in PIE");
                check(engine.exitPlayMode() && SceneSerializer::serialize(*engine.GetContext().world) == snapshot,
                      "external SDK project Stop restores scene");
            }
            engine.shutdown();
            return 0;
        }
        runtimeTests();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
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
