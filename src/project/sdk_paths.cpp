#include "tinyengine/project/sdk_paths.h"

namespace tinyengine
{
std::filesystem::path findProjectSdk(std::filesystem::path directory)
{
    namespace fs = std::filesystem;
    std::error_code error;
    directory = fs::absolute(directory, error).lexically_normal();
    while (!error && !directory.empty())
    {
        if (fs::is_regular_file(directory / "lib/cmake/TinyEngine/TinyEngineConfig.cmake", error))
            return directory;
        error.clear();
        const auto parent = directory.parent_path();
        if (parent == directory)
            break;
        directory = parent;
    }
    return {};
}
} // namespace tinyengine
