#include "tinyengine/project/project_scaffolder.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/ecs/ecs.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/project/project.h"
#include "tinyengine/project/sdk_paths.h"

#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>

namespace tinyengine
{
namespace
{
namespace fs = std::filesystem;
bool validName(const std::string& name)
{
    const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    if (name.empty() || name.size() > 100 || !letter(name.front()))
        return false;
    for (char c : name)
        if (!letter(c) && (c < '0' || c > '9') && c != '_')
            return false;
    return true;
}
bool writeText(const fs::path& path, const std::string& contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return stream.good();
}
std::string newProjectId()
{
    std::random_device random;
    std::ostringstream result;
    result << "project." << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i)
        result << std::setw(8) << static_cast<std::uint32_t>(random());
    return result.str();
}
std::string gameSource()
{
    return R"SOURCE(#include <tinyengine/project_sdk/project.h>
#include "project_module_build_info.h"
#include <stdexcept>

using namespace tinyengine::project_sdk;

// 点击编辑器“运行”，就会创建这个项目会话。
class ProjectSession final : public Session
{
public:
    void onBeginPlay(Context &context) override
    {
        m_player = context.findEntity("Player");
        if (!context.isAlive(m_player)) throw std::runtime_error("Player is missing");
        context.log("PROJECT_BEGIN");
    }

    void onUpdate(Context &context, float deltaTime) override
    {
        TinyProjectTransform transform{};
        if (!context.getTransform(m_player, transform)) return;
        // 修改这里，加入自己的游戏规则。默认每秒旋转 90 度。
        transform.rotation.y += 90.0f * deltaTime;
        transform.position.x += context.getAxis("MoveX") * 3.0f * deltaTime;
        transform.position.z -= context.getAxis("MoveZ") * 3.0f * deltaTime;
        context.setTransform(m_player, transform);
    }

    void onEndPlay(Context &context) override { context.log("PROJECT_END"); }

private:
    TinyProjectEntity m_player{};
};

extern "C" TINY_PROJECT_EXPORT const TinyProjectAPI *TINY_PROJECT_CALL TinyProject_GetAPI(uint32_t abi)
{
    return ExportProject<ProjectSession>(abi, tinyengine::project_module::getModuleInfo());
}
)SOURCE";
}
std::string cmakeSource(const std::string& name, const std::string& id, const std::string& module)
{
    return "cmake_minimum_required(VERSION 3.21)\nproject(" + name +
           " LANGUAGES CXX)\n"
           "find_package(TinyEngine CONFIG REQUIRED)\n"
           "tinyengine_add_project_module(\n    TARGET " +
           name + "\n    PROJECT_ID " + id + "\n    MODULE_ID " + module + "\n    PROJECT_FILE " + name +
           ".tinyproject\n    SOURCES source/game.cpp)\n";
}
} // namespace

std::filesystem::path ProjectScaffolder::findSdkRoot(fs::path directory)
{
    return findProjectSdk(std::move(directory));
}

ProjectOperationResult ProjectScaffolder::validateExisting(const fs::path& projectFile)
{
    if (projectFile.extension() != ".tinyproject" || !fs::is_regular_file(projectFile))
        return {false, {}, "请选择存在的 .tinyproject 文件。"};
    ProjectSettings settings;
    if (!settings.load(projectFile))
        return {false, {}, "项目配置读取失败。"};
    if (!fs::is_directory(settings.resolve(settings.assetDirectory)))
        return {false, {}, "项目素材目录不存在。"};
    if (!fs::is_regular_file(settings.resolve(settings.startupScene)))
        return {false, {}, "项目启动场景不存在。"};
    World startupWorld;
    if (!SceneSerializer::load(startupWorld, settings.resolve(settings.startupScene)))
        return {false, {}, "项目启动场景无法读取。"};
    if (!settings.inputBindings.empty() && !fs::is_regular_file(settings.resolve(settings.inputBindings)))
        return {false, {}, "项目输入配置不存在。"};
    return {true, fs::absolute(projectFile).lexically_normal(), {}};
}

ProjectOperationResult ProjectScaffolder::Create(const ProjectCreateRequest& request)
{
    if (!validName(request.name))
        return {false, {}, "项目名须以英文字母开头，只能包含英文字母、数字和下划线，最多 100 个字符。"};
    std::error_code error;
    const auto parent = fs::absolute(request.parentDirectory, error).lexically_normal();
    if (error || !fs::is_directory(parent))
        return {false, {}, "项目父目录不存在。"};
    const auto root = parent / request.name;
    if (fs::symlink_status(root, error).type() != fs::file_type::not_found)
        return {false, {}, "目标目录已存在，请换一个项目名或位置。"};
    error.clear();
    const auto sdk = fs::absolute(request.sdkRoot, error).lexically_normal();
    if (request.sdkRoot.empty() || error || !fs::is_regular_file(sdk / "lib/cmake/TinyEngine/TinyEngineConfig.cmake"))
        return {false, {}, "未找到项目开发 SDK，请使用完整安装目录中的 Aster-Engine 编辑器。"};
    const auto templateRoot = sdk / "share/tinyengine/templates/ProjectModule";
    std::vector<fs::path> assets = {"assets/player.mesh",          "assets/default.mat",
                                    "assets/default.ppm",          "assets/default.input",
                                    "assets/shaders/default.vert", "assets/shaders/default.frag"};
    for (const auto& asset : assets)
        if (!fs::is_regular_file(templateRoot / asset))
            return {false, {}, "SDK 基础素材缺失：" + asset.generic_string()};
    // 默认着色器还引用 material/ 下的公共代码，必须保留目录结构。
    for (fs::recursive_directory_iterator it(templateRoot / "assets/shaders", error), end; it != end && !error;
         it.increment(error))
    {
        if (!it->is_regular_file() || it->path().extension() == ".meta")
            continue;
        const auto relative = it->path().lexically_relative(templateRoot);
        if (relative != "assets/shaders/default.vert" && relative != "assets/shaders/default.frag")
            assets.push_back(relative);
    }
    if (error)
        return {false, {}, "无法读取 SDK 着色器素材。"};
    if (!fs::create_directory(root, error) || error)
        return {false, {}, "无法创建项目目录，请检查写入权限。"};

    bool complete = false;
    do
    {
        fs::create_directories(root / "assets/shaders", error);
        if (error)
            break;
        fs::create_directory(root / "Source", error);
        if (error)
            break;
        for (const auto& asset : assets)
        {
            fs::create_directories((root / asset).parent_path(), error);
            if (error || !fs::copy_file(templateRoot / asset, root / asset, fs::copy_options::none, error))
                break;
        }
        if (error)
            break;
        World world;
        const auto player = world.createEntity("Player");
        world.GetComponent<TransformComponent>(player)->position = {0.0f, 0.5f, 0.0f};
        world.AddComponent(player, MeshComponent{"assets/player.mesh", "assets/default.mat"});
        const auto camera = world.createEntity("MainCamera");
        world.GetComponent<TransformComponent>(camera)->position = {0.0f, 2.0f, 6.0f};
        world.AddComponent(camera, CameraComponent{});
        world.AddComponent(world.createEntity("Sun"), LightComponent{});
        if (!SceneSerializer::Save(world, root / "assets/Startup.scene"))
            break;
        ProjectSettings settings;
        settings.name = request.name;
        settings.projectId = newProjectId();
        settings.codeModule = request.name + ".game";
        settings.codeRequired = true;
        settings.codeManifest = "Binaries/Windows-x64/Project.modules";
        settings.assetDirectory = "assets";
        settings.startupScene = "assets/Startup.scene";
        settings.inputBindings = "assets/default.input";
        const std::string readme = "# " + request.name + "\n\n在 Aster-Engine 编辑器中打开 " + request.name +
                                   ".tinyproject，点击运行，Player 会旋转；停止后恢复编辑场景。\n\n"
                                   "项目代码在 source/game.cpp。修改代码后保存场景、关闭编辑器，在 build/" +
                                   request.name + ".sln 中重新生成项目，再打开编辑器。当前不支持运行中换库。\n";
        if (!settings.Save(root / (request.name + ".tinyproject")) ||
            !writeText(root / "source/game.cpp", gameSource()) ||
            !writeText(root / "CMakeLists.txt", cmakeSource(request.name, settings.projectId, settings.codeModule)) ||
            !writeText(root / "README.md", readme) ||
            !writeText(root / ".gitignore", "/build/\n/Binaries/\n/Library/\n/saved/\n/Logs/\n/Dist/\n"))
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
} // namespace tinyengine
