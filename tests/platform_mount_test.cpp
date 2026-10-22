#include "tinyengine/platform/platform.h"
#include "tinyengine/resource/pak_archive.h"
#include "tinyengine/resource/resource_manager.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace tinyengine;

namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
struct RestoreEnvironment
{
    std::filesystem::path cwd = std::filesystem::current_path();
    ~RestoreEnvironment()
    {
        PlatformFile::clearMounts();
        std::filesystem::current_path(cwd);
    }
};
} // namespace

int main()
{
    RestoreEnvironment restore;
    try
    {
        const auto root = std::filesystem::absolute("saved/PlatformMountRegression");
        const auto source = root / "source";
        const auto foreign = root / "foreign";
        const auto package = root / "package";
        const auto archive = root / "game.tpak";
        PlatformFile::clearMounts();
        check(PlatformFile::writeAllText(source / "assets/value.txt", "PACKAGED"), "write packaged text");
        check(PlatformFile::writeAllText(source / "assets/empty.txt", ""), "write empty packaged file");
        check(PlatformFile::writeAllBytes(source / "assets/value.bin", {0, 42, 255}), "write packaged bytes");
        check(PlatformFile::writeAllText(source / "assets/color.mat", "color 1 0 0 1\n"), "write packaged material");
        check(PakArchive::packDirectory(source, archive), "pack fixture");
        check(PlatformFile::writeAllText(foreign / "assets/value.txt", "FOREIGN"), "write shadow text");
        check(PlatformFile::writeAllText(foreign / "assets/empty.txt", "NOT EMPTY"), "write shadow empty");
        check(PlatformFile::writeAllBytes(foreign / "assets/value.bin", {1, 2, 3}), "write shadow bytes");
        check(PlatformFile::writeAllText(foreign / "assets/missing.txt", "FOREIGN ONLY"), "write unmounted file");
        check(PlatformFile::writeAllText(package / "assets/color.mat", "color 0 1 0 1\n"),
              "write loose project material");
        std::filesystem::current_path(foreign);
        check(PlatformFile::mountPak(archive), "mount package");
        check(PlatformFile::readAllText("assets/value.txt") == "FOREIGN", "editor retains loose-first reads");
        PlatformFile::setReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
        check(PlatformFile::readAllText("assets/value.txt") == "PACKAGED", "package text wins over cwd");
        check(PlatformFile::ReadAllBytes("assets/value.bin") == std::vector<std::uint8_t>({0, 42, 255}),
              "package bytes win over cwd");
        check(PlatformFile::exists("assets/empty.txt") && PlatformFile::readAllText("assets/empty.txt").empty(),
              "empty package file does not fall back to cwd");
        check(!PlatformFile::exists("assets/missing.txt") && PlatformFile::readAllText("assets/missing.txt").empty() &&
                  PlatformFile::ReadAllBytes("assets/missing.txt").empty(),
              "missing package resource does not fall back to cwd");
        check(PlatformFile::readAllText(foreign / "assets/value.txt") == "FOREIGN",
              "explicit physical reads remain supported");
        ResourceManager resources;
        resources.initialize(nullptr, package / "assets");
        auto material = resources.load<MaterialResource>("assets/color.mat");
        check(material && material->getBaseColor().x == 1.0f && material->getBaseColor().y == 0.0f,
              "resource manager must not bypass package policy via loose absolute candidate");
        resources.shutdown();
        std::filesystem::current_path(package);
        check(PlatformFile::readAllText("assets/value.txt") == "PACKAGED", "changing cwd preserves mounted resource");
        PlatformFile::clearMounts();
        std::filesystem::current_path(foreign);
        check(PlatformFile::readAllText("assets/value.txt") == "FOREIGN", "clear mounts restores editor read policy");
        std::cout << "Platform mount regressions passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
