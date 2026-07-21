// 文件：src/main.cpp
// 作用：启动最小演示程序，用于验证引擎运行时主循环。
// 约定：命令行工具以非交互方式运行，失败时返回非零退出码并输出可定位的错误。
// 类型：实现文件。

#include "MiniEngine/Core/CrashReporter.hpp"
#include "MiniEngine/Runtime/Engine.hpp"

#include <cstdint>
#include <cstdlib>
#include <limits>

int main(int argc, char **argv)
{
    MiniEngine::CrashReporter::Install("TinyEngineDemo");
    std::uint32_t maxFrames = 0;
    if (argc > 1)
    {
        const auto value = std::strtoul(argv[1], nullptr, 10);
        if (value <= static_cast<unsigned long>(std::numeric_limits<std::uint32_t>::max()))
        {
            maxFrames = static_cast<std::uint32_t>(value);
        }
    }

    MiniEngine::Engine engine;
    if (!engine.Initialize())
    {
        return 1;
    }

    engine.Run(maxFrames);
    engine.Shutdown();
    return 0;
}
