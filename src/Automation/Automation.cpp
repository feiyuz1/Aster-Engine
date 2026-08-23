// 文件：src/Automation/Automation.cpp
// 作用：提供命令式自动化测试、编辑器 UI 驱动钩子和结果记录。
// 约定：测试钩子只观测或驱动公开行为，不应成为产品逻辑依赖。
// 类型：实现文件。

#include "MiniEngine/Automation/Automation.hpp"

#include "MiniEngine/Build/TargetPlatform.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/AssetRegistry.hpp"

#include <algorithm>
#include <sstream>
#include <system_error>

namespace MiniEngine
{
bool CommandletRegistry::Register(std::string name, std::string description, Commandlet commandlet)
{
    if (name.empty() || !commandlet)
        return false;
    return m_commandlets.emplace(std::move(name), Record{std::move(description), std::move(commandlet)}).second;
}

CommandletResult CommandletRegistry::Run(const std::string &name, const std::vector<std::string> &arguments) const
{
    const auto found = m_commandlets.find(name);
    if (found == m_commandlets.end())
        return {2, {}, {"Unknown commandlet: " + name}};
    return found->second.callback(arguments);
}

std::string CommandletRegistry::GetHelp() const
{
    std::vector<std::string> names;
    names.reserve(m_commandlets.size());
    for (const auto &pair : m_commandlets)
        names.push_back(pair.first);
    std::sort(names.begin(), names.end());
    std::string result = "TinyEngine commandlets:\n";
    for (const auto &name : names)
        result += "  " + name + " - " + m_commandlets.at(name).description + "\n";
    return result;
}

void RegisterBuiltInCommandlets(CommandletRegistry &registry)
{
    registry.Register("ValidateAssets", "Validate asset metadata and dependency references", [](const std::vector<std::string> &arguments) {
        const std::filesystem::path root = arguments.empty() ? "assets" : arguments.front();
        CommandletResult result;
        if (!std::filesystem::is_directory(root))
            return CommandletResult{1, {}, {"Asset directory does not exist: " + root.generic_string()}};
        AssetRegistry assets;
        assets.Scan(root, false);
        const auto entries = assets.GetAssets();
        for (const auto &asset : entries)
            for (const auto &dependency : asset.dependencies)
                if (!assets.Find(dependency) && !assets.FindById(dependency) && !PlatformFile::Exists(dependency))
                    result.errors.push_back(asset.path + " -> missing " + dependency);
        result.exitCode = result.errors.empty() ? 0 : 1;
        result.output = "Validated " + std::to_string(entries.size()) + " assets";
        return result;
    });

    registry.Register("ValidateScenes", "Load every scene below a directory without starting the renderer",
                      [](const std::vector<std::string> &arguments) {
                          const std::filesystem::path root = arguments.empty() ? "Saved" : arguments.front();
                          CommandletResult result;
                          std::size_t count = 0;
                          std::error_code error;
                          for (std::filesystem::recursive_directory_iterator iterator(root, error), end; iterator != end && !error;
                               iterator.increment(error))
                          {
                              if (!iterator->is_regular_file() || iterator->path().extension() != ".scene")
                                  continue;
                              World world;
                              if (!SceneSerializer::Load(world, iterator->path()))
                                  result.errors.push_back("Invalid scene: " + iterator->path().generic_string());
                              ++count;
                          }
                          if (error)
                              result.errors.push_back(error.message());
                          result.exitCode = result.errors.empty() ? 0 : 1;
                          result.output = "Validated " + std::to_string(count) + " scenes";
                          return result;
                      });

    registry.Register(
        "MigrateScenes", "Back up and rewrite scenes using the current schema", [](const std::vector<std::string> &arguments) {
            const std::filesystem::path root = arguments.empty() ? "Saved" : arguments.front();
            CommandletResult result;
            std::size_t migrated = 0;
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(root, error), end; iterator != end && !error;
                 iterator.increment(error))
            {
                if (!iterator->is_regular_file() || iterator->path().extension() != ".scene")
                    continue;
                World world;
                if (!SceneSerializer::Load(world, iterator->path()))
                {
                    result.errors.push_back("Cannot load scene: " + iterator->path().generic_string());
                    continue;
                }
                const auto backup = iterator->path().string() + ".bak";
                std::filesystem::copy_file(iterator->path(), backup, std::filesystem::copy_options::overwrite_existing, error);
                if (error || !SceneSerializer::Save(world, iterator->path()))
                {
                    result.errors.push_back("Cannot migrate scene: " + iterator->path().generic_string());
                    error.clear();
                    continue;
                }
                ++migrated;
            }
            if (error)
                result.errors.push_back(error.message());
            result.exitCode = result.errors.empty() ? 0 : 1;
            result.output = "Migrated " + std::to_string(migrated) + " scenes";
            return result;
        });

    registry.Register("CookProject", "Cook a project for a target platform", [](const std::vector<std::string> &arguments) {
        if (arguments.size() < 2)
            return CommandletResult{2, {}, {"Usage: CookProject <project> <output> [platform] [configuration]"}};
        PlatformCookOptions options;
        options.projectFile = arguments[0];
        options.outputDirectory = arguments[1];
        if (arguments.size() >= 3)
            options.targetPlatform = arguments[2];
        if (arguments.size() >= 4)
        {
            const auto configuration = TargetPlatformCooker::ParseConfiguration(arguments[3]);
            if (!configuration)
                return CommandletResult{2, {}, {"Unknown configuration: " + arguments[3]}};
            options.configuration = *configuration;
        }
        const auto cooked = TargetPlatformCooker::Cook(options);
        CommandletResult result;
        result.exitCode = cooked.success ? 0 : 1;
        result.output = cooked.success ? "Cooked " + cooked.archive.generic_string() : std::string{};
        result.errors = cooked.errors;
        return result;
    });
}
} // namespace MiniEngine
