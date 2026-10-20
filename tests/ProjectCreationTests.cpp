#include "MiniEngine/Project/ProjectScaffolder.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/Project/SolutionGenerator.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
#ifdef NDEBUG
constexpr auto Configuration = "Release";
#else
constexpr auto Configuration = "Debug";
#endif
void Check(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS: " << message << '\n';
}
std::string Read(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
int Run(const fs::path &sdk, const fs::path &parent)
{
    try
    {
        fs::create_directories(parent);
        const auto created = MiniEngine::ProjectScaffolder::Create({"FirstGame", parent, sdk});
        Check(created.success, "create a C++ project using only an installed SDK: " + created.error);
        MiniEngine::ProjectSettings settings;
        Check(settings.Load(created.projectFile), "load the generated project settings");
        Check(!settings.projectId.empty() && !settings.codeModule.empty() && settings.codeRequired,
              "new projects require their own registered code module");
        Check(Read(parent/"FirstGame/CMakeLists.txt").find("add_subdirectory") == std::string::npos,
              "the project does not compile or depend on the engine source tree");
        Check(Read(parent/"FirstGame/Source/Game.cpp").find("TinyProject_GetAPI") != std::string::npos,
              "the starter code exports a project DLL entry point");
        Check(MiniEngine::ProjectScaffolder::ValidateExisting(created.projectFile).success,
              "the generated project opens through the real editor validation");
        MiniEngine::World world;
        Check(MiniEngine::SceneSerializer::Load(world, settings.Resolve(settings.startupScene)), "load starter scene");
        bool visiblePlayer = false;
        for (auto entity : world.GetEntityManager().GetAliveEntities())
        {
            const auto *name = world.GetComponent<MiniEngine::NameComponent>(entity);
            if (name && name->name == "Player") visiblePlayer = world.GetComponent<MiniEngine::MeshComponent>(entity) != nullptr;
        }
        Check(visiblePlayer, "the new project contains a visible Player model");
        Check(fs::is_regular_file(parent/"FirstGame/assets/shaders/material/VertexTransform.glsl") &&
              fs::is_regular_file(parent/"FirstGame/assets/shaders/material/Lighting.glsl"),
              "starter shaders include their material dependencies");
        const auto other = MiniEngine::ProjectScaffolder::Create({"OtherGame", parent, sdk});
        MiniEngine::ProjectSettings otherSettings;
        Check(other.success && otherSettings.Load(other.projectFile) && otherSettings.projectId != settings.projectId,
              "separate projects get separate persistent identities");
        std::ofstream(parent/"FirstGame/keep.txt") << "user data";
        Check(!MiniEngine::ProjectScaffolder::Create({"FirstGame", parent, sdk}).success &&
              Read(parent/"FirstGame/keep.txt") == "user data", "creation never overwrites an existing project");
        Check(!MiniEngine::ProjectScaffolder::Create({"InvalidSDK", parent, parent}).success &&
              !fs::exists(parent/"InvalidSDK"), "missing SDK is diagnosed before creating files");
        Check(MiniEngine::ProjectScaffolder::FindSDKRoot(sdk/"bin"/Configuration) == fs::absolute(sdk).lexically_normal(),
              "the editor discovers its SDK without a user-entered source path");
        const auto built = MiniEngine::SolutionGenerator::Build(parent/"FirstGame", sdk, Configuration);
        Check(built.success, "automatically configure and compile the new project: " + built.output);
        Check(fs::is_regular_file(built.solutionFile), "a ready-to-open Visual Studio solution is generated");
        Check(Read(settings.Resolve(settings.codeManifest)).find(settings.projectId) != std::string::npos,
              "successful creation publishes the matching project DLL manifest");
        const auto manifestBefore = Read(settings.Resolve(settings.codeManifest));
        const auto source = Read(parent/"FirstGame/Source/Game.cpp");
        std::ofstream(parent/"FirstGame/Source/Game.cpp", std::ios::app) << "\n#error deliberate_creation_build_failure\n";
        const auto failed = MiniEngine::SolutionGenerator::Build(parent/"FirstGame", sdk, Configuration);
        Check(!failed.success && !failed.output.empty() && fs::is_regular_file(parent/"FirstGame/Saved/ProjectBuild.log"),
              "compile failure retains the project and reports a saved diagnostic");
        Check(Read(settings.Resolve(settings.codeManifest)) == manifestBefore, "failed compilation preserves the last working code");
        std::ofstream(parent/"FirstGame/Source/Game.cpp", std::ios::trunc) << source;
        Check(MiniEngine::SolutionGenerator::Build(parent/"FirstGame", sdk, Configuration).success,
              "retrying after a source fix compiles the same project successfully");
        return 0;
    }
    catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
    return argc == 3 ? Run(argv[1], argv[2]) : 2;
}
