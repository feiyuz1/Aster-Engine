#pragma once
#ifdef _WIN32
#include <filesystem>
#include <string>
#include <vector>
namespace MiniEngine
{
// Windows console argv uses the active code page; wmain preserves project paths.
template<class Run> int RunWideMain(int argc, wchar_t **wide, Run run)
{
    std::vector<std::string> values; values.reserve(static_cast<size_t>(argc));
    for (int i=0;i<argc;++i) values.push_back(std::filesystem::path(wide[i]).u8string());
    std::vector<char *> arguments; arguments.reserve(values.size());
    for (auto &value:values) arguments.push_back(value.data());
    return run(argc,arguments.data());
}
}
#endif
