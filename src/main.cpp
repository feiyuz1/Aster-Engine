#include "MiniEngine/Runtime/Engine.hpp"

#include <cstdint>
#include <limits>
#include <cstdlib>

int main(int argc, char** argv)
{
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
