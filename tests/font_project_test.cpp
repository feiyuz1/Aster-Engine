#include "tinyengine/build/build_compatibility.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/project/project.h"
#include "tinyengine/project/project_packager.h"
#include "tinyengine/resource/pak_archive.h"
#include "tinyengine/text/runtime_dependencies.h"

#include <chrono>
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

void settingsRoundTripAndRejectMalformedCounts(const std::filesystem::path& root)
{
    ProjectSettings settings;
    settings.name = "Font project";
    settings.defaultFont = fromUtf8Path("assets/fonts/默认.ttf");
    settings.defaultFontBold = "assets/fonts/bold.ttf";
    settings.defaultFontItalic = "assets/fonts/italic.ttf";
    settings.defaultFontBoldItalic = "assets/fonts/bold-italic.ttf";
    settings.fallbackFonts = {"assets/fonts/arabic.ttf", "assets/fonts/fallback.otf"};
    const auto path = root / "roundtrip.tinyproject";
    check(settings.Save(path), "save project font settings");
    ProjectSettings loaded;
    check(loaded.load(path), "load project font settings");
    check(loaded.defaultFont == settings.defaultFont && loaded.defaultFontBold == settings.defaultFontBold &&
              loaded.defaultFontItalic == settings.defaultFontItalic &&
              loaded.defaultFontBoldItalic == settings.defaultFontBoldItalic &&
              loaded.fallbackFonts == settings.fallbackFonts,
          "all font paths and ordered fallbacks round trip, including Unicode paths");
    for (const std::string count : {"", "-1", "65", "1junk", "9999999999999999999999"})
    {
        const auto malformed = root / "malformed.tinyproject";
        check(PlatformFile::writeAllText(malformed, "project.name=changed\ntext.font=changed.ttf\n"
                                                    "text.fallbackFontCount=" +
                                                        count + "\n"),
              "write malformed fallback count fixture");
        check(!loaded.load(malformed), "malformed or unbounded fallback count fails loading");
        check(loaded.name == settings.name && loaded.projectFile == std::filesystem::absolute(path) &&
                  loaded.defaultFont == settings.defaultFont && loaded.fallbackFonts == settings.fallbackFonts,
              "malformed fallback count leaves previous project settings untouched");
    }
    check(PlatformFile::writeAllText(root / "missing-entry.tinyproject", "text.fallbackFontCount=1\n"),
          "write missing fallback fixture");
    check(!loaded.load(root / "missing-entry.tinyproject"), "declared fallbacks must have nonempty paths");
    check(PlatformFile::writeAllText(root / "legacy.tinyproject", "project.name=legacy\n"), "write legacy fixture");
    check(loaded.load(root / "legacy.tinyproject") && loaded.defaultFont.empty() && loaded.defaultFontBold.empty() &&
              loaded.defaultFontItalic.empty() && loaded.defaultFontBoldItalic.empty() && loaded.fallbackFonts.empty(),
          "legacy projects clear configured fonts and retain system discovery defaults");
}

void packageIncludesFontsAndRuntimeFiles(const std::filesystem::path& root)
{
    ProjectSettings settings;
    settings.name = "PortableFonts";
    settings.defaultFont = "assets/fonts/regular.ttf";
    settings.defaultFontBold = "assets/fonts/bold.ttf";
    settings.defaultFontItalic = "assets/fonts/italic.ttf";
    settings.defaultFontBoldItalic = "assets/fonts/bold-italic.ttf";
    settings.fallbackFonts = {"assets/fonts/arabic.ttf"};
    const auto source = root / "project";
    settings.projectFile = source / "Fonts.tinyproject";
    const auto fontBytes = PlatformFile::ReadAllBytes("tests/fixtures/fonts/noto_sans.ttf");
    const auto arabicBytes = PlatformFile::ReadAllBytes("tests/fixtures/fonts/noto_sans_arabic.ttf");
    check(!fontBytes.empty() && !arabicBytes.empty(), "bundled font fixtures exist");
    for (const auto& path :
         {settings.defaultFont, settings.defaultFontBold, settings.defaultFontItalic, settings.defaultFontBoldItalic})
        check(PlatformFile::writeAllBytes(source / path, fontBytes), "write configured font fixture");
    check(PlatformFile::writeAllBytes(source / settings.fallbackFonts.front(), arabicBytes), "write fallback font");
    check(PlatformFile::writeAllText(source / settings.startupScene, "font package scene fixture"), "write scene");
    check(settings.Save(settings.projectFile), "save font package project");
    const auto executable = root / "runtime/Game.exe";
    check(PlatformFile::writeAllText(executable, "portable game fixture"), "write game executable fixture");
    check(BuildCompatibility::Save(BuildCompatibility::sidecarPath(executable), BuildCompatibility::current()),
          "write compatible game build metadata");
    for (const auto name : text_runtime::fileNames)
        check(PlatformFile::writeAllText(executable.parent_path() / fromUtf8Path(std::string(name)),
                                         "runtime dependency " + std::string(name)),
              "write runtime dependency fixture");
    std::string runtimeManifest = "tinyengine-text-runtime-v1\n";
    for (const auto name : text_runtime::fileNames)
        runtimeManifest += std::string(name) + "\n";
    runtimeManifest += "modeled-freetype.dll\n";
    const auto sourceRuntimeManifest = fromUtf8Path(toUtf8Path(executable) + ".text-runtime.txt");
    check(PlatformFile::writeAllText(sourceRuntimeManifest, runtimeManifest), "write text runtime manifest");
    check(PlatformFile::writeAllText(executable.parent_path() / "modeled-freetype.dll", "modeled shared font library"),
          "write modeled shared font dependency");
    const auto package = ProjectPackager::package({settings.projectFile, root / "package", executable});
    if (!package.success)
        for (const auto& error : package.errors)
            std::cerr << error << '\n';
    check(package.success, "package project with configured fonts and runtime dependencies");
    for (const auto& path :
         {settings.defaultFont, settings.defaultFontBold, settings.defaultFontItalic, settings.defaultFontBoldItalic})
    {
        const auto packed = PakArchive::ReadFile(package.archive, toUtf8GenericPath(path));
        check(packed && *packed == fontBytes, "configured variant is included in package archive");
    }
    const auto packedFallback =
        PakArchive::ReadFile(package.archive, toUtf8GenericPath(settings.fallbackFonts.front()));
    check(packedFallback && *packedFallback == arabicBytes, "fallback font is included in package archive");
    ProjectSettings shipping;
    check(shipping.load(package.projectFile) && shipping.defaultFont == settings.defaultFont &&
              shipping.defaultFontBold == settings.defaultFontBold &&
              shipping.defaultFontItalic == settings.defaultFontItalic &&
              shipping.defaultFontBoldItalic == settings.defaultFontBoldItalic &&
              shipping.fallbackFonts == settings.fallbackFonts,
          "shipping project retains portable font paths");
    for (const auto name : text_runtime::fileNames)
        check(PlatformFile::readAllText(package.executable.parent_path() / fromUtf8Path(std::string(name))) ==
                  "runtime dependency " + std::string(name),
              "runtime dependency is copied beside shipping executable");
    check(PlatformFile::readAllText(package.executable.parent_path() / "modeled-freetype.dll") ==
              "modeled shared font library",
          "modeled shared font DLL from generated runtime manifest is packaged automatically");
    check(PlatformFile::readAllText(fromUtf8Path(toUtf8Path(package.executable) + ".text-runtime.txt")) ==
              runtimeManifest,
          "runtime manifest is copied and renamed with shipping executable");
    std::string windowsManifest;
    for (const auto value : runtimeManifest)
        windowsManifest += value == '\n' ? "\r\n" : std::string(1, value);
    check(PlatformFile::writeAllText(sourceRuntimeManifest, windowsManifest), "write Windows CRLF manifest");
    const auto windowsPackage = ProjectPackager::package({settings.projectFile, root / "package-crlf", executable});
    check(windowsPackage.success, "Windows-generated CRLF runtime manifest must package successfully");
    check(PlatformFile::writeAllText(sourceRuntimeManifest, runtimeManifest), "restore LF manifest");

    const auto unchanged = root / "unchanged-runtime-output";
    check(PlatformFile::writeAllText(unchanged / "sentinel.txt", "preserve existing output"), "write output sentinel");
    const auto rejectsWithoutMutation = [&]()
    {
        const auto result = ProjectPackager::package({settings.projectFile, unchanged, executable});
        check(!result.success && !result.errors.empty(), "invalid runtime manifest must fail packaging");
        check(PlatformFile::readAllText(unchanged / "sentinel.txt") == "preserve existing output" &&
                  std::distance(std::filesystem::directory_iterator(unchanged),
                                std::filesystem::directory_iterator{}) == 1,
              "runtime validation failure leaves existing output untouched");
    };
    std::error_code runtimeError;
    std::filesystem::remove(executable.parent_path() / "modeled-freetype.dll", runtimeError);
    check(!runtimeError, "remove modeled DLL");
    rejectsWithoutMutation();
    check(PlatformFile::writeAllText(executable.parent_path() / "modeled-freetype.dll", "modeled shared font library"),
          "restore modeled shared dependency");
    std::vector<std::string> malformedManifests{"",
                                                runtimeManifest + "../outside.dll\n",
                                                runtimeManifest + "folder/inside.dll\n",
                                                runtimeManifest + "C:\\outside.dll\n",
                                                runtimeManifest + "modeled-freetype.dll\n",
                                                runtimeManifest + "MODELED-FREETYPE.DLL\n",
                                                runtimeManifest + "\n",
                                                "modeled-freetype.dll\n",
                                                std::string(65537, 'x')};
    check(PlatformFile::writeAllText(executable.parent_path() / "Package.manifest", "reserved package file"),
          "write package filename collision fixture");
    malformedManifests.push_back(runtimeManifest + "Package.manifest\n");
    std::string tooManyEntries = runtimeManifest;
    for (int entry = 0; entry < 257; ++entry)
    {
        const auto name = "modeled-" + std::to_string(entry) + ".dll";
        check(PlatformFile::writeAllText(executable.parent_path() / name, "bounded manifest entry"),
              "write bounded manifest file fixture");
        tooManyEntries += name + "\n";
    }
    malformedManifests.push_back(std::move(tooManyEntries));
    if (!text_runtime::fileNames.empty())
        malformedManifests.push_back("tinyengine-text-runtime-v1\nmodeled-freetype.dll\n");
    for (const auto& malformed : malformedManifests)
    {
        check(PlatformFile::writeAllText(sourceRuntimeManifest, malformed), "write malformed runtime manifest");
        rejectsWithoutMutation();
    }
    std::filesystem::remove(sourceRuntimeManifest, runtimeError);
    check(!runtimeError, "remove required runtime manifest");
    rejectsWithoutMutation();
    check(PlatformFile::writeAllText(sourceRuntimeManifest, runtimeManifest), "restore valid runtime manifest");

    check(PlatformFile::writeAllBytes(source / "outside.ttf", fontBytes), "write outside-assets font");
    check(PlatformFile::writeAllText(source / "assets/invalid.ttf", "not a font"), "write invalid font");
    const std::filesystem::path rejected[] = {"outside.ttf", "../project/outside.ttf", "assets/missing.ttf",
                                              "assets/invalid.ttf", source / settings.defaultFont};
    int index = 0;
    for (const auto& path : rejected)
    {
        auto invalid = settings;
        invalid.defaultFont = path;
        check(invalid.Save(settings.projectFile), "save invalid font path fixture");
        const auto output = root / ("rejected-" + std::to_string(index++));
        const auto result = ProjectPackager::package({settings.projectFile, output, executable});
        check(!result.success && !result.errors.empty() && !std::filesystem::exists(output),
              "external, traversing, missing or invalid fonts fail before package side effects");
    }
    auto invalidFallback = settings;
    invalidFallback.fallbackFonts = {"assets/invalid.ttf"};
    check(invalidFallback.Save(settings.projectFile), "save invalid fallback fixture");
    const auto fallbackOutput = root / "rejected-fallback";
    check(!ProjectPackager::package({settings.projectFile, fallbackOutput, executable}).success &&
              !std::filesystem::exists(fallbackOutput),
          "fallback fonts receive the same validation as the primary font");
    check(settings.Save(settings.projectFile), "restore valid font settings");
    if (!text_runtime::fileNames.empty())
    {
        std::error_code error;
        std::filesystem::remove(executable.parent_path() / fromUtf8Path(std::string(text_runtime::fileNames.front())),
                                error);
        check(!error, "remove a required runtime dependency fixture");
        const auto output = root / "rejected-runtime";
        check(!ProjectPackager::package({settings.projectFile, output, executable}).success &&
                  !std::filesystem::exists(output),
              "missing required runtime files fail before package side effects");
    }
}
} // namespace

int main()
{
    try
    {
        const auto root = std::filesystem::absolute(".codex_tmp/FontProjectRegression") /
                          std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        settingsRoundTripAndRejectMalformedCounts(root);
        packageIncludesFontsAndRuntimeFiles(root);
        std::cout << "Font project regressions passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
