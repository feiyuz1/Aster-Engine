#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/PakArchive.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace MiniEngine;

namespace
{
void Check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
struct RestoreEnvironment
{
    std::filesystem::path cwd = std::filesystem::current_path();
    ~RestoreEnvironment()
    {
        PlatformFile::ClearMounts();
        std::filesystem::current_path(cwd);
    }
};
} // namespace

int main()
{
    RestoreEnvironment restore;
    try
    {
        const auto root = std::filesystem::absolute("Saved/PlatformMountRegression");
        const auto source = root / "source";
        const auto foreign = root / "foreign";
        const auto package = root / "package";
        const auto archive = root / "game.tpak";
        PlatformFile::ClearMounts();
        Check(PlatformFile::WriteAllText(source / "assets/value.txt", "PACKAGED"), "write packaged text");
        Check(PlatformFile::WriteAllText(source / "assets/empty.txt", ""), "write empty packaged file");
        Check(PlatformFile::WriteAllBytes(source / "assets/value.bin", {0, 42, 255}), "write packaged bytes");
        Check(PlatformFile::WriteAllText(source / "assets/color.mat", "color 1 0 0 1\n"), "write packaged material");
        Check(PakArchive::PackDirectory(source, archive), "pack fixture");
        Check(PlatformFile::WriteAllText(foreign / "assets/value.txt", "FOREIGN"), "write shadow text");
        Check(PlatformFile::WriteAllText(foreign / "assets/empty.txt", "NOT EMPTY"), "write shadow empty");
        Check(PlatformFile::WriteAllBytes(foreign / "assets/value.bin", {1, 2, 3}), "write shadow bytes");
        Check(PlatformFile::WriteAllText(foreign / "assets/missing.txt", "FOREIGN ONLY"), "write unmounted file");
        Check(PlatformFile::WriteAllText(package / "assets/color.mat", "color 0 1 0 1\n"), "write loose project material");
        std::filesystem::current_path(foreign);
        Check(PlatformFile::MountPak(archive), "mount package");
        Check(PlatformFile::ReadAllText("assets/value.txt") == "FOREIGN", "editor retains loose-first reads");
        PlatformFile::SetReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
        Check(PlatformFile::ReadAllText("assets/value.txt") == "PACKAGED", "package text wins over cwd");
        Check(PlatformFile::ReadAllBytes("assets/value.bin") == std::vector<std::uint8_t>({0, 42, 255}), "package bytes win over cwd");
        Check(PlatformFile::Exists("assets/empty.txt") && PlatformFile::ReadAllText("assets/empty.txt").empty(),
              "empty package file does not fall back to cwd");
        Check(!PlatformFile::Exists("assets/missing.txt") && PlatformFile::ReadAllText("assets/missing.txt").empty() &&
                  PlatformFile::ReadAllBytes("assets/missing.txt").empty(),
              "missing package resource does not fall back to cwd");
        Check(PlatformFile::ReadAllText(foreign / "assets/value.txt") == "FOREIGN", "explicit physical reads remain supported");
        ResourceManager resources;
        resources.Initialize(nullptr, package / "assets");
        auto material = resources.Load<MaterialResource>("assets/color.mat");
        Check(material && material->GetBaseColor().x == 1.0f && material->GetBaseColor().y == 0.0f,
              "resource manager must not bypass package policy via loose absolute candidate");
        resources.Shutdown();
        std::filesystem::current_path(package);
        Check(PlatformFile::ReadAllText("assets/value.txt") == "PACKAGED", "changing cwd preserves mounted resource");
        PlatformFile::ClearMounts();
        std::filesystem::current_path(foreign);
        Check(PlatformFile::ReadAllText("assets/value.txt") == "FOREIGN", "clear mounts restores editor read policy");
        std::cout << "Platform mount regressions passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
