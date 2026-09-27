#include "MiniEngine/Project/ProjectScaffolder.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Project/Project.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace MiniEngine
{
namespace
{
namespace fs = std::filesystem;

bool ValidName(const std::string &name)
{
    const auto letter = [](const char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
    };
    if (name.empty() || !letter(name.front()))
        return false;
    for (const char character : name)
        if (!letter(character) && (character < '0' || character > '9') && character != '_')
            return false;
    return true;
}

bool WriteText(const fs::path &path, const std::string &contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return stream.good();
}

std::string ReplaceAll(std::string text, const std::string &from, const std::string &to)
{
    std::size_t offset = 0;
    while ((offset = text.find(from, offset)) != std::string::npos)
    {
        text.replace(offset, from.size(), to);
        offset += to.size();
    }
    return text;
}

std::string GameHeader()
{
    return R"SOURCE(#pragma once

#include "MiniEngine/Runtime/Engine.hpp"

// 从这里开始写项目游戏规则；编辑器 Play 暂不加载项目 C++。
class ProjectGame final : public MiniEngine::GameInstance
{
  public:
    explicit ProjectGame(MiniEngine::Engine &engine) : m_engine(engine) {}
    void OnInitialize() override;
    void OnUpdate(float deltaTime) override;

  private:
    MiniEngine::Engine &m_engine;
    MiniEngine::Entity m_player = MiniEngine::InvalidEntity;
};
)SOURCE";
}

std::string GameSource()
{
    return R"SOURCE(#include "Game.hpp"

#include "MiniEngine/ECS/Components.hpp"

void ProjectGame::OnInitialize()
{
    auto &world = *m_engine.GetContext().world;
    for (const auto entity : world.GetEntityManager().GetAliveEntities())
    {
        const auto *name = world.GetComponent<MiniEngine::NameComponent>(entity);
        if (name && name->name == "Player")
        {
            m_player = entity;
            break;
        }
    }
}

void ProjectGame::OnUpdate(const float deltaTime)
{
    auto &context = m_engine.GetContext();
    if (!context.world->IsAlive(m_player))
        return;
    auto *transform = context.world->GetComponent<MiniEngine::TransformComponent>(m_player);
    if (!transform)
        return;
    // 修改这里即可加入自己的移动、交互和胜利条件。
    transform->position.x += context.inputSystem->GetAxis("MoveX") * 3.0f * deltaTime;
    transform->position.z -= context.inputSystem->GetAxis("MoveZ") * 3.0f * deltaTime;
}
)SOURCE";
}

std::string MainSource()
{
    return R"SOURCE(#include "Game.hpp"

#include "MiniEngine/Core/CrashReporter.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>

int main(int argc, char **argv)
{
    MiniEngine::CrashReporter::Install("TinyEngineProject");
    std::filesystem::path projectFile;
    std::uint32_t maximumFrames = 0;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument.rfind("--frames=", 0) == 0)
        {
            const auto value = std::strtoul(argument.c_str() + 9, nullptr, 10);
            if (value <= static_cast<unsigned long>(std::numeric_limits<std::uint32_t>::max()))
                maximumFrames = static_cast<std::uint32_t>(value);
        }
        else if (projectFile.empty())
            projectFile = argument;
    }
    if (projectFile.empty())
    {
        std::cerr << "Pass the .tinyproject path as the first argument.\n";
        return 2;
    }
    MiniEngine::Engine engine;
    engine.SetGameInstance(std::make_unique<ProjectGame>(engine));
    if (!engine.Initialize(std::filesystem::absolute(projectFile), true))
        return 1;
    engine.Run(maximumFrames);
    engine.Shutdown();
    return 0;
}
)SOURCE";
}

std::string CmakeSource(const std::string &name, const fs::path &engineRoot)
{
    std::string source = R"SOURCE(cmake_minimum_required(VERSION 3.20)
project(@NAME@ LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(TINYENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINYENGINE_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(TINYENGINE_BUILD_GAME OFF CACHE BOOL "" FORCE)
set(TINYENGINE_BUILD_EDITOR OFF CACHE BOOL "" FORCE)
set(TINYENGINE_BUILD_IMPORTERS OFF CACHE BOOL "" FORCE)
set(TINYENGINE_ROOT "@ENGINE_ROOT@" CACHE PATH "TinyEngine source tree")
if(NOT EXISTS "${TINYENGINE_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "Set TINYENGINE_ROOT to the TinyEngine source directory")
endif()
add_subdirectory("${TINYENGINE_ROOT}" "${CMAKE_BINARY_DIR}/engine" EXCLUDE_FROM_ALL)
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(@NAME@ ${GAME_SOURCES})
target_link_libraries(@NAME@ PRIVATE tinyengine)
set_target_properties(@NAME@ PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "\"${CMAKE_CURRENT_SOURCE_DIR}/@NAME@.tinyproject\""
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(@NAME@ PRIVATE /utf-8 /FS)
endif()
)SOURCE";
    source = ReplaceAll(source, "@NAME@", name);
    return ReplaceAll(source, "@ENGINE_ROOT@", engineRoot.generic_u8string());
}

std::string ReadmeSource(const std::string &name)
{
    return "# " + name + "\n\n"
           "在 TinyEngine 编辑器中打开 `" + name + ".tinyproject` 编辑场景。\n\n"
           "项目代码在 `src/Game.cpp`：`OnInitialize` 找到 Player，`OnUpdate` 读取输入并移动它。\n\n"
           "修改代码后，在生成的 Visual Studio 解决方案中构建并运行 `" + name + "`。"
           "当前编辑器的 Play 不加载项目 C++ 代码。\n\n"
           "命令行运行：`build/Release/" + name + ".exe " + name + ".tinyproject`。\n";
}

bool CopyStarterAsset(const fs::path &engineRoot, const fs::path &projectRoot, const fs::path &relative)
{
    std::error_code error;
    const auto target = projectRoot / relative;
    fs::create_directories(target.parent_path(), error);
    return !error && fs::copy_file(engineRoot / relative, target, fs::copy_options::none, error) && !error;
}
} // 结束匿名命名空间

ProjectOperationResult ProjectScaffolder::ValidateExisting(const fs::path &projectFile)
{
    if (projectFile.extension() != ".tinyproject" || !fs::is_regular_file(projectFile))
        return {false, {}, "请选择存在的 .tinyproject 文件。"};
    ProjectSettings settings;
    if (!settings.Load(projectFile))
        return {false, {}, "项目配置读取失败。"};
    if (!fs::is_directory(settings.Resolve(settings.assetDirectory)))
        return {false, {}, "项目素材目录不存在。"};
    if (!fs::is_regular_file(settings.Resolve(settings.startupScene)))
        return {false, {}, "项目启动场景不存在。"};
    World startupWorld;
    if (!SceneSerializer::Load(startupWorld, settings.Resolve(settings.startupScene)))
        return {false, {}, "项目启动场景无法读取。"};
    if (!settings.inputBindings.empty() && !fs::is_regular_file(settings.Resolve(settings.inputBindings)))
        return {false, {}, "项目输入配置不存在。"};
    return {true, fs::absolute(projectFile).lexically_normal(), {}};
}

ProjectOperationResult ProjectScaffolder::Create(const ProjectCreateRequest &request)
{
    if (!ValidName(request.name))
        return {false, {}, "项目名须以英文字母开头，只能包含英文字母、数字和下划线。"};
    std::error_code error;
    const auto parent = fs::absolute(request.parentDirectory, error).lexically_normal();
    if (error || !fs::is_directory(parent))
        return {false, {}, "项目父目录不存在。"};
    const auto root = parent / request.name;
    if (fs::symlink_status(root, error).type() != fs::file_type::not_found)
        return {false, {}, "目标目录已存在，请换一个项目名或位置。"};
    error.clear();
    const auto engineRoot = fs::absolute(request.engineRoot, error).lexically_normal();
    if (error || !fs::is_regular_file(engineRoot / "CMakeLists.txt"))
        return {false, {}, "引擎源码目录无效。"};
    const std::vector<fs::path> starterAssets = {
        "assets/player.mesh", "assets/default.mat", "assets/default.ppm", "assets/default.input",
        "assets/shaders/default.vert", "assets/shaders/default.frag"};
    for (const auto &asset : starterAssets)
        if (!fs::is_regular_file(engineRoot / asset))
            return {false, {}, "引擎基础素材缺失：" + asset.generic_string()};
    if (!fs::create_directory(root, error) || error)
        return {false, {}, "无法创建项目目录，请检查写入权限。"};

    bool complete = false;
    do
    {
        fs::create_directories(root / "assets" / "shaders", error);
        if (error)
            break;
        fs::create_directory(root / "src", error);
        if (error)
            break;
        for (const auto &asset : starterAssets)
            if (!CopyStarterAsset(engineRoot, root, asset))
            {
                error = std::make_error_code(std::errc::io_error);
                break;
            }
        if (error)
            break;

        World world;
        const auto player = world.CreateEntity("Player");
        world.GetComponent<TransformComponent>(player)->position = {0.0f, 0.5f, 0.0f};
        world.AddComponent(player, MeshComponent{"assets/player.mesh", "assets/default.mat"});
        const auto camera = world.CreateEntity("MainCamera");
        world.GetComponent<TransformComponent>(camera)->position = {0.0f, 2.0f, 6.0f};
        world.AddComponent(camera, CameraComponent{});
        const auto sun = world.CreateEntity("Sun");
        world.AddComponent(sun, LightComponent{});
        if (!SceneSerializer::Save(world, root / "assets" / "Startup.scene"))
            break;

        ProjectSettings settings;
        settings.name = request.name;
        settings.assetDirectory = "assets";
        settings.startupScene = "assets/Startup.scene";
        settings.inputBindings = "assets/default.input";
        const auto projectFile = root / (request.name + ".tinyproject");
        if (!settings.Save(projectFile) || !WriteText(root / "src" / "Game.hpp", GameHeader()) ||
            !WriteText(root / "src" / "Game.cpp", GameSource()) ||
            !WriteText(root / "src" / "main.cpp", MainSource()) ||
            !WriteText(root / "CMakeLists.txt", CmakeSource(request.name, engineRoot)) ||
            !WriteText(root / "README.md", ReadmeSource(request.name)) ||
            !WriteText(root / ".gitignore", "/build/\n/Saved/\n/Logs/\n/Dist/\n"))
            break;
        complete = true;
    } while (false);

    if (!complete)
    {
        fs::remove_all(root, error);
        return {false, {}, "项目文件生成失败，请检查磁盘空间或目录权限。"};
    }
    return {true, root / (request.name + ".tinyproject"), {}};
}
} // 结束 MiniEngine 命名空间
