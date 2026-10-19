#include "MiniEngine/Runtime/Engine.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ProjectSDK/SDKInfo.hpp"
#include "MiniEngine/Platform/UTF8Arguments.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace MiniEngine;
namespace fs=std::filesystem;
namespace
{
void Check(bool value,const char *name) { if (!value) throw std::runtime_error(name); std::cout<<"PASS "<<name<<'\n'; }
struct Fixture
{
    fs::path root=fs::temp_directory_path()/("TinyEngineRuntimeTests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path project=root/"Runtime.tinyproject";
    Fixture()
    {
        fs::create_directories(root/"Binaries"); fs::create_directories(root/"assets");
        World world; world.CreateEntity("Player");
        Check(SceneSerializer::Save(world,root/"Start.scene"),"save real runtime scene");
        ProjectSettings settings; settings.projectId="p0-project"; settings.codeModule="game.test";
        settings.codeRequired=true; settings.codeManifest="Binaries/Project.modules"; settings.startupScene="Start.scene";
        Check(settings.Save(project),"save real runtime project"); Publish(TINY_TEST_PROJECT_VALID);
    }
    void Publish(const char *binary)
    {
        fs::copy_file(fs::u8path(binary),root/"Binaries/Game.dll",fs::copy_options::overwrite_existing);
        const std::string text=std::string("module.id=game.test\nproject.id=p0-project\nmodule.abi=1\nmodule.platform=Windows-x64\nDevelopment.sdkBuildId=")+
            ProjectSDK::GetSDKBuildId()+"\nDevelopment.library=Binaries/Game.dll\nDevelopment.buildId=build-1\nDevelopment.sourceFingerprint=source-1\nDevelopment.schemaHash=none\nDevelopment.dependencies.count=0\n";
        Check(PlatformFile::WriteAllText(root/"Binaries/Project.modules",text),"publish runtime manifest");
    }
    ~Fixture()
    {
        std::error_code e; const auto path=fs::absolute(root,e).lexically_normal();
        if (!e && path.parent_path()==fs::absolute(fs::temp_directory_path()).lexically_normal() &&
            path.filename().string().find("TinyEngineRuntimeTests-")==0) fs::remove_all(path,e);
    }
};
struct Legacy : GameInstance { int &updates; explicit Legacy(int &v):updates(v){} void OnUpdate(float) override { ++updates; } };
Entity Player(Engine &engine)
{
    for (auto e:engine.GetContext().world->GetEntityManager().GetAliveEntities())
        if (engine.GetContext().world->GetComponent<NameComponent>(e)->name=="Player") return e;
    return InvalidEntity;
}
float Rotation(Engine &engine) { return engine.GetContext().world->GetComponent<TransformComponent>(Player(engine))->rotation.y; }
void RuntimeTests()
{
    Fixture f; Engine engine; int legacyUpdates=0; engine.SetGameInstance(std::make_unique<Legacy>(legacyUpdates));
    Check(engine.Initialize(f.project,false),"editor initializes registered project module");
    const auto snapshot=SceneSerializer::Serialize(*engine.GetContext().world);
    engine.Run(2); Check(Rotation(engine)==0,"edit mode skips project updates");
    for (int i=0;i<2;++i)
    {
        Check(engine.EnterPlayMode(),"editor_play_calls_project_code"); engine.Run(3);
        Check(Rotation(engine)>0 && legacyUpdates==0,"project updates transform without double updating legacy GameInstance");
        engine.SetPaused(true); const auto paused=Rotation(engine); engine.Run(2);
        Check(Rotation(engine)==paused,"pause_skips_project_updates");
        Check(engine.ExitPlayMode() && SceneSerializer::Serialize(*engine.GetContext().world)==snapshot,"stop_restores_snapshot_and_invalidates_handles");
    }
    {
        Check(engine.EnterPlayMode(),"automatic world epoch test begins");
        DynamicLibrary probe; std::string error;
        Check(probe.Open(engine.GetProjectModuleHost().GetLoadedLibraryPath(),{},&error),"open real module diagnostic symbol");
        const auto alive=reinterpret_cast<uint32_t (TINY_PROJECT_CALL *)()>(probe.FindSymbol("TinyTest_PreviousPlayerAlive"));
        const auto findOld=reinterpret_cast<TinyProjectResult (TINY_PROJECT_CALL *)()>(probe.FindSymbol("TinyTest_FindWithPreviousWorld"));
        Check(alive && alive()==1,"project holds a live handle");
        auto &world=*engine.GetContext().world;
        world.DestroyEntity(Player(engine)); world.Clear(); world.CreateEntity("Player");
        Check(alive()==0,"empty world reset invalidates project handles automatically");
        Check(findOld && findOld()==TINY_PROJECT_NOT_FOUND,"empty world reset invalidates world token automatically");
        Check(engine.ExitPlayMode(),"world epoch test restores scene");
        Check(alive()==0,"stop invalidates old project handles automatically");
    }
    engine.Shutdown();
    {
        Engine game; Check(game.Initialize(f.project,true) && game.IsPlaying(),"standalone_start_calls_same_project_code");
        game.Run(3); Check(Rotation(game)>0,"standalone rotates same scene object"); game.Shutdown(); game.Shutdown();
    }
    fs::remove(f.root/"Binaries/Game.dll");
    {
        Engine missing; Check(missing.Initialize(f.project,false),"required missing code keeps editor scene open");
        Check(Player(missing)!=InvalidEntity && !missing.EnterPlayMode() && !missing.GetProjectCodeError().empty(),"required_missing_code_blocks_play_but_keeps_editor_scene"); missing.Shutdown();
        Engine game; Check(!game.Initialize(f.project,true),"required missing code rejects standalone startup"); game.Shutdown();
    }
    for (const auto *binary:{TINY_TEST_PROJECT_BEGINFAIL,TINY_TEST_PROJECT_UPDATEFAIL})
    {
        f.Publish(binary); Engine failing; Check(failing.Initialize(f.project,false),"fault fixture initializes editor");
        const auto before=SceneSerializer::Serialize(*failing.GetContext().world);
        if (failing.EnterPlayMode()) failing.Run(3);
        Check(!failing.IsPlaying() && SceneSerializer::Serialize(*failing.GetContext().world)==before && !failing.GetProjectCodeError().empty(),
              "project callback failure stops and restores edit snapshot"); failing.Shutdown();
    }
    Engine absent; Check(!absent.Initialize(f.root/"DoesNotExist.tinyproject",false),"explicit invalid project never opens demo"); absent.Shutdown();
    f.Publish(TINY_TEST_PROJECT_VALID); fs::remove(f.root/"Start.scene");
    Engine badScene; Check(!badScene.Initialize(f.project,false),"explicit invalid startup scene never opens demo"); badScene.Shutdown();
}
}
static int RunMain(int argc, char **argv)
{
    try
    {
        if (argc==3 && std::string(argv[1])=="--write-scene")
        { World world; world.CreateEntity("Player"); Check(SceneSerializer::Save(world,fs::u8path(argv[2])),"write QA TINYSCENE 11"); return 0; }
        if (argc==3 && std::string(argv[1])=="--external-project")
        {
            Engine engine; Check(engine.Initialize(fs::u8path(argv[2]),false),"PIE uses installed external project DLL");
            const auto snapshot=SceneSerializer::Serialize(*engine.GetContext().world);
            const auto &host=engine.GetProjectModuleHost();
            Check(fs::is_regular_file(host.GetLoadedPdbPath()) && host.GetLoadedLibraryPath().stem()==host.GetLoadedPdbPath().stem(),"shadow DLL and matching PDB are present");
            for(int i=0;i<2;++i)
            {
                Check(engine.EnterPlayMode(),"external SDK project enters PIE"); engine.Run(3);
                Check(Rotation(engine)>0,"external SDK project rotates in PIE");
                Check(engine.ExitPlayMode() && SceneSerializer::Serialize(*engine.GetContext().world)==snapshot,"external SDK project Stop restores scene");
            }
            engine.Shutdown(); return 0;
        }
        RuntimeTests(); return 0;
    }
    catch(const std::exception &e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) { return MiniEngine::RunWideMain(argc,argv,RunMain); }
#else
int main(int argc,char **argv) { return RunMain(argc,argv); }
#endif
