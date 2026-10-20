#include "MiniEngine/Project/SolutionGenerator.hpp"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <chrono>
#include <fstream>
#include <iostream>

int main()
{
    // 模拟 SDK 接收者：构建机的 CMake 不存在，VS CMake 没有加入 PATH。
    wchar_t previous[32768]{};
    const auto length = GetEnvironmentVariableW(L"PATH", previous, 32768);
    if (length >= 32768) return 2;
    SetEnvironmentVariableW(L"PATH", L"");
    struct Restore { const wchar_t *value; ~Restore() { SetEnvironmentVariableW(L"PATH", value); } } restore{length ? previous : nullptr};
    const auto directory = std::filesystem::temp_directory_path() /
        ("TinyToolDiscovery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    std::ofstream(directory/"CMakeLists.txt") << "cmake_minimum_required(VERSION 3.21)\nproject(VSDiscovery LANGUAGES NONE)\n";
    const auto result = MiniEngine::SolutionGenerator::Generate(directory);
    if (!result.success)
    {
        std::cerr << "FAIL: discover VS CMake without PATH or the build-machine CMake: " << result.output << '\n';
        return 1;
    }
    std::cout << "PASS: VS-bundled CMake generates a solution without PATH or the build-machine executable\n";
    return 0;
}
