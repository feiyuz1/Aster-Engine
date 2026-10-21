#include "MiniEngine/Project/ProjectScaffolder.hpp"
#include "MiniEngine/Project/SDKPaths.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Project/Project.hpp"

#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace MiniEngine
{
namespace
{
namespace fs = std::filesystem;
bool ValidName(const std::string &name)
{
    const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    if (name.empty() || name.size() > 100 || !letter(name.front())) return false;
    for (char c : name) if (!letter(c) && (c < '0' || c > '9') && c != '_') return false;
    return true;
}
bool WriteText(const fs::path &path, const std::string &contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return stream.good();
}
std::string NewProjectId()
{
    std::random_device random;
    std::ostringstream result;
    result << "project." << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) result << std::setw(8) << static_cast<std::uint32_t>(random());
    return result.str();
}
std::string GameSource()
{
    return R"SOURCE(#include <MiniEngine/ProjectSDK/Project.hpp>
#include "ProjectModuleBuildInfo.h"
#include <stdexcept>

using namespace MiniEngine::ProjectSDK;

// 点击编辑器“运行”，就会创建这个项目会话。
class ProjectSession final : public Session
{
public:
    void OnBeginPlay(Context &context) override
    {
        m_player = context.FindEntity("Player");
        if (!context.IsAlive(m_player)) throw std::runtime_error("Player is missing");
        context.Log("PROJECT_BEGIN");
    }

    void OnUpdate(Context &context, float deltaTime) override
    {
        TinyProjectTransform transform{};
        if (!context.GetTransform(m_player, transform)) return;
        // 修改这里，加入自己的游戏规则。默认每秒旋转 90 度。
        transform.rotation.y += 90.0f * deltaTime;
        transform.position.x += context.GetAxis("MoveX") * 3.0f * deltaTime;
        transform.position.z -= context.GetAxis("MoveZ") * 3.0f * deltaTime;
        context.SetTransform(m_player, transform);
    }

    void OnEndPlay(Context &context) override { context.Log("PROJECT_END"); }

private:
    TinyProjectEntity m_player{};
};

extern "C" TINY_PROJECT_EXPORT const TinyProjectAPI *TINY_PROJECT_CALL TinyProject_GetAPI(uint32_t abi)
{
    return ExportProject<ProjectSession>(abi, TinyEngineProject::GetModuleInfo());
}
)SOURCE";
}
std::string CmakeSource(const std::string &name, const std::string &id, const std::string &module)
{
    return "cmake_minimum_required(VERSION 3.21)\nproject(" + name + " LANGUAGES CXX)\n"
           "find_package(TinyEngine CONFIG REQUIRED)\n"
           "tinyengine_add_project_module(\n    TARGET " + name + "\n    PROJECT_ID " + id +
           "\n    MODULE_ID " + module + "\n    PROJECT_FILE " + name + ".tinyproject\n    SOURCES Source/Game.cpp)\n";
}
} // namespace

std::filesystem::path ProjectScaffolder::FindSDKRoot(fs::path directory)
{
    return FindProjectSDK(std::move(directory));
}

ProjectOperationResult ProjectScaffolder::ValidateExisting(const fs::path &projectFile)
{
    if (projectFile.extension() != ".tinyproject" || !fs::is_regular_file(projectFile))
        return {false, {}, "请选择存在的 .tinyproject 文件。"};
    ProjectSettings settings;
    if (!settings.Load(projectFile)) return {false, {}, "项目配置读取失败。"};
    if (!fs::is_directory(settings.Resolve(settings.assetDirectory))) return {false, {}, "项目素材目录不存在。"};
    if (!fs::is_regular_file(settings.Resolve(settings.startupScene))) return {false, {}, "项目启动场景不存在。"};
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
        return {false, {}, "项目名须以英文字母开头，只能包含英文字母、数字和下划线，最多 100 个字符。"};
    std::error_code error;
    const auto parent = fs::absolute(request.parentDirectory, error).lexically_normal();
    if (error || !fs::is_directory(parent)) return {false, {}, "项目父目录不存在。"};
    const auto root = parent / request.name;
    if (fs::symlink_status(root, error).type() != fs::file_type::not_found)
        return {false, {}, "目标目录已存在，请换一个项目名或位置。"};
    error.clear();
    const auto sdk = fs::absolute(request.sdkRoot, error).lexically_normal();
    if (request.sdkRoot.empty() || error || !fs::is_regular_file(sdk / "lib/cmake/TinyEngine/TinyEngineConfig.cmake"))
        return {false, {}, "未找到项目开发 SDK，请使用完整安装目录中的 TinyEngine 编辑器。"};
    const auto templateRoot = sdk / "share/tinyengine/templates/ProjectModule";
    std::vector<fs::path> assets = {"assets/player.mesh", "assets/default.mat", "assets/default.ppm",
        "assets/default.input", "assets/shaders/default.vert", "assets/shaders/default.frag"};
    for (const auto &asset : assets)
        if (!fs::is_regular_file(templateRoot / asset)) return {false, {}, "SDK 基础素材缺失：" + asset.generic_string()};
    // 默认着色器还引用 material/ 下的公共代码，必须保留目录结构。
    for (fs::recursive_directory_iterator it(templateRoot/"assets/shaders", error), end; it != end && !error; it.increment(error))
    {
        if (!it->is_regular_file() || it->path().extension() == ".meta") continue;
        const auto relative = it->path().lexically_relative(templateRoot);
        if (relative != "assets/shaders/default.vert" && relative != "assets/shaders/default.frag") assets.push_back(relative);
    }
    if (error) return {false, {}, "无法读取 SDK 着色器素材。"};
    if (!fs::create_directory(root, error) || error) return {false, {}, "无法创建项目目录，请检查写入权限。"};

    bool complete = false;
    do
    {
        fs::create_directories(root / "assets/shaders", error);
        if (error) break;
        fs::create_directory(root / "Source", error);
        if (error) break;
        for (const auto &asset : assets)
        {
            fs::create_directories((root/asset).parent_path(), error);
            if (error || !fs::copy_file(templateRoot / asset, root / asset, fs::copy_options::none, error)) break;
        }
        if (error) break;
        World world;
        const auto player = world.CreateEntity("Player");
        world.GetComponent<TransformComponent>(player)->position = {0.0f, 0.5f, 0.0f};
        world.AddComponent(player, MeshComponent{"assets/player.mesh", "assets/default.mat"});
        const auto camera = world.CreateEntity("MainCamera");
        world.GetComponent<TransformComponent>(camera)->position = {0.0f, 2.0f, 6.0f};
        world.AddComponent(camera, CameraComponent{});
        world.AddComponent(world.CreateEntity("Sun"), LightComponent{});
        if (!SceneSerializer::Save(world, root / "assets/Startup.scene")) break;
        ProjectSettings settings;
        settings.name = request.name;
        settings.projectId = NewProjectId();
        settings.codeModule = request.name + ".game";
        settings.codeRequired = true;
        settings.codeManifest = "Binaries/Windows-x64/Project.modules";
        settings.assetDirectory = "assets";
        settings.startupScene = "assets/Startup.scene";
        settings.inputBindings = "assets/default.input";
        const std::string readme = "# " + request.name + "\n\n在 TinyEngine 编辑器中打开 " + request.name +
            ".tinyproject，点击运行，Player 会旋转；停止后恢复编辑场景。\n\n"
            "项目代码在 Source/Game.cpp。修改代码后保存场景、关闭编辑器，在 build/" + request.name +
            ".sln 中重新生成项目，再打开编辑器。当前不支持运行中换库。\n";
        if (!settings.Save(root/(request.name+".tinyproject")) ||
            !WriteText(root/"Source/Game.cpp", GameSource()) ||
            !WriteText(root/"CMakeLists.txt", CmakeSource(request.name, settings.projectId, settings.codeModule)) ||
            !WriteText(root/"README.md", readme) ||
            !WriteText(root/".gitignore", "/build/\n/Binaries/\n/Library/\n/Saved/\n/Logs/\n/Dist/\n")) break;
        complete = true;
    } while (false);
    if (!complete)
    {
        fs::remove_all(root, error);
        return {false, {}, "项目文件生成失败，请检查磁盘空间或目录权限。"};
    }
    return {true, root/(request.name+".tinyproject"), {}};
}
} // namespace MiniEngine
