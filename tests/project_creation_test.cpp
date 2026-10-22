#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/project/project.h"
#include "tinyengine/project/project_scaffolder.h"
#include "tinyengine/project/solution_generator.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
#ifdef NDEBUG
constexpr auto kConfiguration = "Release";
#else
constexpr auto kConfiguration = "Debug";
#endif
void check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
    std::cout << "PASS: " << message << '\n';
}
std::string read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int Run(const fs::path& sdk, const fs::path& parent)
{
    try
    {
        fs::create_directories(parent);
        const auto created = tinyengine::ProjectScaffolder::Create({"FirstGame", parent, sdk});
        check(created.success, "create a C++ project using only an installed SDK: " + created.error);
        tinyengine::ProjectSettings settings;
        check(settings.load(created.projectFile), "load the generated project settings");
        check(!settings.projectId.empty() && !settings.codeModule.empty() && settings.codeRequired,
              "new projects require their own registered code module");
        check(read(parent / "FirstGame/CMakeLists.txt").find("add_subdirectory") == std::string::npos,
              "the project does not compile or depend on the engine source tree");
        check(read(parent / "FirstGame/source/game.cpp").find("TinyProject_GetAPI") != std::string::npos,
              "the starter code exports a project DLL entry point");
        check(tinyengine::ProjectScaffolder::validateExisting(created.projectFile).success,
              "the generated project opens through the real editor validation");
        tinyengine::World world;
        check(tinyengine::SceneSerializer::load(world, settings.resolve(settings.startupScene)), "load starter scene");
        bool visiblePlayer = false;
        for (auto entity : world.GetEntityManager().GetAliveEntities())
        {
            const auto* name = world.GetComponent<tinyengine::NameComponent>(entity);
            if (name && name->name == "Player")
                visiblePlayer = world.GetComponent<tinyengine::MeshComponent>(entity) != nullptr;
        }
        check(visiblePlayer, "the new project contains a visible Player model");
        check(fs::is_regular_file(parent / "FirstGame/assets/shaders/material/VertexTransform.glsl") &&
                  fs::is_regular_file(parent / "FirstGame/assets/shaders/material/Lighting.glsl"),
              "starter shaders include their material dependencies");
        const auto other = tinyengine::ProjectScaffolder::Create({"OtherGame", parent, sdk});
        tinyengine::ProjectSettings otherSettings;
        check(other.success && otherSettings.load(other.projectFile) && otherSettings.projectId != settings.projectId,
              "separate projects get separate persistent identities");
        std::ofstream(parent / "FirstGame/keep.txt") << "user data";
        check(!tinyengine::ProjectScaffolder::Create({"FirstGame", parent, sdk}).success &&
                  read(parent / "FirstGame/keep.txt") == "user data",
              "creation never overwrites an existing project");
        check(!tinyengine::ProjectScaffolder::Create({"InvalidSDK", parent, parent}).success &&
                  !fs::exists(parent / "InvalidSDK"),
              "missing SDK is diagnosed before creating files");
        check(tinyengine::ProjectScaffolder::findSdkRoot(sdk / "bin" / kConfiguration) ==
                  fs::absolute(sdk).lexically_normal(),
              "the editor discovers its SDK without a user-entered source path");
        const auto built = tinyengine::SolutionGenerator::build(parent / "FirstGame", sdk, kConfiguration);
        check(built.success, "automatically configure and compile the new project: " + built.output);
        check(fs::is_regular_file(built.solutionFile), "a ready-to-open Visual Studio solution is generated");
        check(read(settings.resolve(settings.codeManifest)).find(settings.projectId) != std::string::npos,
              "successful creation publishes the matching project DLL manifest");
        const auto manifestBefore = read(settings.resolve(settings.codeManifest));
        const auto source = read(parent / "FirstGame/source/game.cpp");
        std::ofstream(parent / "FirstGame/source/game.cpp", std::ios::app)
            << "\n#error deliberate_creation_build_failure\n";
        const auto failed = tinyengine::SolutionGenerator::build(parent / "FirstGame", sdk, kConfiguration);
        check(!failed.success && !failed.output.empty() &&
                  fs::is_regular_file(parent / "FirstGame/saved/ProjectBuild.log"),
              "compile failure retains the project and reports a saved diagnostic");
        check(read(settings.resolve(settings.codeManifest)) == manifestBefore,
              "failed compilation preserves the last working code");
        std::ofstream(parent / "FirstGame/source/game.cpp", std::ios::trunc) << source;
        check(tinyengine::SolutionGenerator::build(parent / "FirstGame", sdk, kConfiguration).success,
              "retrying after a source fix compiles the same project successfully");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    return argc == 3 ? Run(argv[1], argv[2]) : 2;
}
