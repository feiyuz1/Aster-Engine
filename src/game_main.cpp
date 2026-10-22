// 文件：src/game_main.cpp
// 作用：启动面向发布包的独立游戏运行时。
// 约定：命令行工具以非交互方式运行，失败时返回非零退出码并输出可定位的错误。
// 类型：实现文件。

#include "tinyengine/core/crash_reporter.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/platform/utf8_arguments.h"
#include "tinyengine/runtime/engine.h"

#include "tinyengine/platform/platform.h"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace
{
std::filesystem::path findProject(const std::filesystem::path& executableDirectory)
{
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(executableDirectory, error))
        if (entry.is_regular_file() && entry.path().extension() == ".tinyproject")
            return entry.path();
    return {};
}
} // namespace

static int RunMain(int argc, char** argv)
{
    tinyengine::CrashReporter::install("Aster-Engine-Game");
    const auto launchDirectory = std::filesystem::current_path();
    const auto executableDirectory =
        std::filesystem::absolute(tinyengine::fromUtf8Path(argv[0])).parent_path().lexically_normal();
    std::filesystem::path projectFile;
    std::uint32_t maxFrames = 0;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument.rfind("--frames=", 0) == 0)
        {
            const auto value = std::strtoul(argument.c_str() + 9, nullptr, 10);
            if (value <= static_cast<unsigned long>(std::numeric_limits<std::uint32_t>::max()))
                maxFrames = static_cast<std::uint32_t>(value);
        }
        else if (projectFile.empty())
            projectFile = tinyengine::fromUtf8Path(argument).is_absolute()
                              ? tinyengine::fromUtf8Path(argument)
                              : launchDirectory / tinyengine::fromUtf8Path(argument);
    }
    if (projectFile.empty())
        projectFile = findProject(executableDirectory);
    if (projectFile.empty() || !std::filesystem::is_regular_file(projectFile))
    {
        std::cerr << "Usage: tinyengine_game <project.tinyproject> [--frames=N]\n";
        return 2;
    }

    tinyengine::Engine engine;
    if (!engine.initialize(std::filesystem::absolute(projectFile).lexically_normal(), true))
        return 1;
    engine.Run(maxFrames);
    engine.shutdown();
    return 0;
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
