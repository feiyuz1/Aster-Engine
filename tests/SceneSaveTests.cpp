// 文件：tests/SceneSaveTests.cpp
// 作用：快速验证场景首次保存所依赖的原子写入、备份和失败保护。
// 约定：只读写 Saved/SceneSaveRegression 下的固定测试文件，不触碰正式 assets。
// 类型：测试实现。

#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace
{
int failures = 0;

void Check(const bool condition, const char *expression, const int line)
{
    if (!condition)
    {
        std::cerr << "FAILED line " << line << ": " << expression << '\n';
        ++failures;
    }
}
} // namespace

#define CHECK(value) Check((value), #value, __LINE__)

int main()
{
    using namespace MiniEngine;
    const std::filesystem::path root = "Saved/SceneSaveRegression";
    std::filesystem::create_directories(root);

    const auto atomicPath = root / "AtomicText.txt";
    std::filesystem::remove(atomicPath);
    std::filesystem::remove(atomicPath.string() + ".bak");
    std::filesystem::remove(atomicPath.string() + ".tmp");
    CHECK(PlatformFile::WriteAllTextAtomic(atomicPath, "before", true));
    CHECK(PlatformFile::WriteAllTextAtomic(atomicPath, "after", true));
    CHECK(PlatformFile::ReadAllText(atomicPath) == "after");
    CHECK(PlatformFile::ReadAllText(atomicPath.string() + ".bak") == "before");
    CHECK(!std::filesystem::exists(atomicPath.string() + ".tmp"));

    World world;
    world.CreateEntity("Untitled content");
    const auto scenePath = root / "Untitled.scene";
    CHECK(!SceneSerializer::Save(world, {}));
    CHECK(SceneSerializer::Save(world, scenePath));
    World loaded;
    CHECK(SceneSerializer::Load(loaded, scenePath));
    CHECK(loaded.GetEntityManager().GetAliveEntities().size() == 1);

    const auto invalidTarget = root / "Directory.scene";
    std::filesystem::create_directories(invalidTarget);
    CHECK(!SceneSerializer::Save(world, invalidTarget));
    CHECK(std::filesystem::is_directory(invalidTarget));
    CHECK(!std::filesystem::exists(invalidTarget.string() + ".tmp"));

    if (failures == 0)
        std::cout << "All scene save regression tests passed\n";
    return failures == 0 ? 0 : 1;
}
