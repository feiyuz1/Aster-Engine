// 文件：src/GameMain.cpp
// 作用：启动面向发布包的独立游戏运行时。
// 约定：命令行工具以非交互方式运行，失败时返回非零退出码并输出可定位的错误。
// 类型：实现文件。

#include "MiniEngine/Core/CrashReporter.hpp"
#include "MiniEngine/Runtime/Engine.hpp"
#include "MiniEngine/Platform/UTF8Arguments.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace
{
std::filesystem::path FindProject(const std::filesystem::path &executableDirectory)
{
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(executableDirectory, error))
        if (entry.is_regular_file() && entry.path().extension() == ".tinyproject")
            return entry.path();
    return {};
}
} // 结束匿名命名空间

static int RunMain(int argc, char **argv)
{
    MiniEngine::CrashReporter::Install("Aster-Engine-Game");
    const auto launchDirectory = std::filesystem::current_path();
    const auto executableDirectory = std::filesystem::absolute(std::filesystem::u8path(argv[0])).parent_path().lexically_normal();
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
            projectFile = std::filesystem::u8path(argument).is_absolute() ? std::filesystem::u8path(argument) : launchDirectory / std::filesystem::u8path(argument);
    }
    if (projectFile.empty())
        projectFile = FindProject(executableDirectory);
    if (projectFile.empty() || !std::filesystem::is_regular_file(projectFile))
    {
        std::cerr << "Usage: tinyengine_game <project.tinyproject> [--frames=N]\n";
        return 2;
    }

    MiniEngine::Engine engine;
    if (!engine.Initialize(std::filesystem::absolute(projectFile).lexically_normal(), true))
        return 1;
    engine.Run(maxFrames);
    engine.Shutdown();
    return 0;
}
#ifdef _WIN32
int wmain(int argc,wchar_t **argv) { return MiniEngine::RunWideMain(argc,argv,RunMain); }
#else
int main(int argc,char **argv) { return RunMain(argc,argv); }
#endif
