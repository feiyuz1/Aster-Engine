#include "MiniEngine/Project/ProjectModuleManifest.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/ProjectSDK/SDKInfo.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Project/ProjectModuleHost.hpp"
#include "MiniEngine/Platform/DynamicLibrary.hpp"
#include "MiniEngine/Plugin/PluginManager.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;
using namespace MiniEngine;

namespace
{
int checks = 0;
void Check(bool condition, const char *name)
{
    if (!condition) throw std::runtime_error(name);
    ++checks;
    std::cout << "PASS " << name << '\n';
}

struct Fixture
{
    fs::path root = fs::temp_directory_path() / ("TinyEngineProjectTests-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path projectRoot = root / fs::u8path(u8"项目 with spaces");
    fs::path projectFile = projectRoot / "Game.tinyproject";
    fs::path manifestFile = projectRoot / "Binaries/Project.modules";
    Fixture()
    {
        fs::create_directories(projectRoot / "Binaries/Development");
        Write(projectFile, "project.name=Fixture\nproject.id=p0-project\ncode.module=game.test\ncode.manifest=Binaries/Project.modules\n");
        Write(projectRoot / "Binaries/Development/Game.dll", "test binary fixture");
        Manifest();
    }
    ~Fixture()
    {
        std::error_code ec;
        const auto absoluteRoot = fs::absolute(root, ec).lexically_normal();
        if (!ec && absoluteRoot.parent_path() == fs::absolute(fs::temp_directory_path()).lexically_normal() &&
            absoluteRoot.filename().string().find("TinyEngineProjectTests-") == 0)
            fs::remove_all(absoluteRoot, ec);
    }
    static void Write(const fs::path &path, const std::string &text)
    {
        Check(PlatformFile::WriteAllText(path, text), "write controlled project fixture");
    }
    void Manifest(const std::string &extra = {})
    {
        Write(manifestFile, std::string("module.id=game.test\nproject.id=p0-project\nmodule.abi=1\nmodule.platform=Windows-x64\n") +
            "Development.sdkBuildId=" + ProjectSDK::GetSDKBuildId() + "\n" +
            "Development.library=Binaries/Development/Game.dll\nDevelopment.pdb=\n" +
            "Development.buildId=build-1\nDevelopment.sourceFingerprint=source-1\n" +
            "Development.schemaHash=none\nDevelopment.dependencies.count=0\n" + extra);
    }
    ProjectSettings Settings() const
    {
        ProjectSettings settings;
        Check(settings.Load(projectFile), "load configured project");
        return settings;
    }
};

TinyProjectString Span(const std::string &value) { return {value.data(), static_cast<uint64_t>(value.size())}; }
TinyProjectModuleInfo Info(const ProjectModuleSelection &selection)
{
    static const std::string profile = "Development";
    return {sizeof(TinyProjectModuleInfo), Span(selection.moduleId), Span(selection.projectId), Span(selection.sdkBuildId),
            Span(selection.projectBuildId), Span(selection.sourceFingerprint), Span(profile)};
}

void ManifestTests()
{
    Fixture f;
    ProjectSettings settings = f.Settings();
    Check(settings.codeRequired, "code_required_defaults_true");
    Check(settings.projectId == "p0-project", "project identity is preserved");
    const auto saved = f.projectRoot / "Saved.tinyproject";
    Check(settings.Save(saved), "save code settings");
    ProjectSettings reloaded;
    Check(reloaded.Load(saved) && reloaded.codeModule == "game.test" && reloaded.codeRequired,
          "project code settings round trip");
    const auto legacy = f.projectRoot / "Legacy.tinyproject";
    Fixture::Write(legacy, "project.name=Legacy\n");
    Check(reloaded.Load(legacy) && reloaded.codeModule.empty() && reloaded.codeManifest.empty() &&
          reloaded.projectId.empty() && !reloaded.codeRequired, "project_load_does_not_retain_previous_code_fields");
    Fixture::Write(f.projectFile, "project.name=Optional\nproject.id=p0-project\ncode.module=game.test\ncode.required=false\ncode.manifest=Binaries/Project.modules\n");
    Check(reloaded.Load(f.projectFile) && !reloaded.codeRequired, "explicit optional code setting is honored");
    Fixture::Write(f.projectFile, "code.module=game.test\n");
    Check(!reloaded.Load(f.projectFile), "incomplete code settings are rejected");

    std::string error;
    ProjectModuleSelection selection;
    const auto previous = fs::current_path();
    fs::current_path(f.root);
    const bool loaded = LoadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error);
    fs::current_path(previous);
    Check(loaded && error.empty(), "manifest_resolves_from_project_root");
    Check(fs::equivalent(selection.libraryPath, f.projectRoot / "Binaries/Development/Game.dll"),
          "unicode and spaces resolve the intended DLL");
    Check(selection.projectBuildId == "build-1" && selection.sdkBuildId == ProjectSDK::GetSDKBuildId(),
          "manifest selected identity matches the host SDK");
    auto info = Info(selection);
    Check(ValidateProjectModuleInfo(info, selection, &error), "binary identity matches selected manifest");
    static const std::string wrong = "wrong";
    info.sdk_build_id = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error) && error.find("sdk.buildId") != std::string::npos,
          "binary SDK mismatch is diagnosed");
    info = Info(selection); info.project_build_id = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error) && error.find("buildId") != std::string::npos,
          "stale same-name binary is rejected");
    info = Info(selection); info.module_id = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error), "binary module identity is checked");
    info = Info(selection); info.project_id = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error), "binary project identity is checked");
    info = Info(selection); info.source_fingerprint = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error), "binary source fingerprint is checked");
    info = Info(selection); info.profile = Span(wrong);
    Check(!ValidateProjectModuleInfo(info, selection, &error), "binary profile is checked");
    info = Info(selection); info.struct_size = 4;
    Check(!ValidateProjectModuleInfo(info, selection, &error), "short metadata is rejected before reading fields");
    info = Info(selection); info.module_id = {nullptr, 4};
    Check(!ValidateProjectModuleInfo(info, selection, &error), "invalid metadata span is rejected");

    const std::pair<const char *, const char *> invalid[] = {
        {"module.id", "another.module"}, {"project.id", "other-project"}, {"module.abi", "2"},
        {"module.abi", "1invalid"}, {"module.platform", "Windows-x86"},
        {"Development.sdkBuildId", "wrong-sdk"}, {"Development.buildId", "../../outside"},
        {"Development.sourceFingerprint", ""}, {"Development.schemaHash", ""},
        {"Development.dependencies.count", "-1"}, {"Development.dependencies.count", "invalid"},
        {"Development.library", "../outside.dll"}, {"Development.library", "C:/outside.dll"},
        {"Development.library", ""}, {"Development.pdb", "../outside.pdb"},
        {"Development.dependencies.count", "1"}
    };
    for (const auto &entry : invalid)
    {
        f.Manifest(std::string(entry.first) + '=' + entry.second + '\n');
        Check(!LoadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error) && !error.empty(),
              entry.first);
        Check(selection.libraryPath.empty(), "failed selection cannot retain a previously usable DLL");
    }
    f.Manifest();
    settings.codeManifest = "../outside.modules";
    Check(!LoadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error),
          "manifest itself cannot escape project root");
    settings.codeManifest = "Binaries/Project.modules";
    const auto foreign = f.root / "foreign";
    const auto link = f.projectRoot / "Binaries/escaped";
    fs::create_directories(foreign);
    Fixture::Write(foreign / "Game.dll", "outside project");
#ifdef _WIN32
    const auto command = L"cmd.exe /d /c mklink /J \"" + link.wstring() + L"\" \"" + foreign.wstring() + L"\" >nul";
    Check(_wsystem(command.c_str()) == 0, "prepare project-boundary junction fixture");
#else
    fs::create_directory_symlink(foreign, link);
#endif
    f.Manifest("Development.library=Binaries/escaped/Game.dll\n");
    Check(!LoadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error),
          "manifest_rejects_path_escape_through_junction");
    fs::remove(link);
    f.Manifest("Shipping.sdkBuildId=different-shipping-sdk\nsdk.buildId=legacy-global-value\n");
    Check(LoadProjectModuleSelection(settings, ProjectCodeProfile::Development, selection, &error),
          "profiles can retain different SDK identities without invalidating Development");
}

void TINY_PROJECT_CALL RecordEvent(void *context, uint32_t, TinyProjectString text)
{
    static_cast<std::vector<std::string> *>(context)->emplace_back(text.data, static_cast<size_t>(text.size));
}
void LoaderTests()
{
    Fixture f;
    std::vector<std::string> events;
    TinyProjectHostAPI api{}; api.abi_version = TINY_PROJECT_ABI_VERSION; api.struct_size = sizeof(api);
    api.host_context = &events; api.log = &RecordEvent;
    ProjectModuleSelection selection; std::string error;
    Check(LoadProjectModuleSelection(f.Settings(), ProjectCodeProfile::Development, selection, &error), "loader fixture manifest");
    const auto put = [&](const fs::path &source) { fs::copy_file(source, selection.libraryPath, fs::copy_options::overwrite_existing); };
    put(fs::u8path(TINY_TEST_PROJECT_VALID));
    DynamicLibrary direct;
    Check(direct.Open(selection.libraryPath, {}, &error), "RAII opens real unicode DLL path");
    const auto entry = reinterpret_cast<TinyProjectGetAPIFn>(direct.FindSymbol(TINY_PROJECT_ENTRY_POINT));
    Check(entry && !entry(99), "module refuses unknown ABI");
    DynamicLibrary moved(std::move(direct));
    Check(!direct.FindSymbol(TINY_PROJECT_ENTRY_POINT) && moved.FindSymbol(TINY_PROJECT_ENTRY_POINT), "library ownership moves");
    moved.Close(); moved.Close();
    ProjectModuleHost host;
    Check(host.Load(selection, api), "real_module_loads");
    Check(events == std::vector<std::string>{"load"}, "load does not create gameplay session");
    Check(host.GetLoadedBuildId() == selection.projectBuildId && host.GetState() == ProjectModuleState::Registered,
          "registered module exposes build identity");
    Check(host.GetLoadedLibraryPath().parent_path() == f.projectRoot / "Library/ProjectModules/build-1",
          "unicode_shadow_copy_loads");
    Check(!host.Load(selection, api), "loaded module rejects repeated Load");
    host.Close(); host.Close();
    Check(events == std::vector<std::string>({"load", "unload"}) && host.GetLoadedBuildId().empty(), "close_is_idempotent");
    const char *invalid[] = {TINY_TEST_PROJECT_MISSING, TINY_TEST_PROJECT_ABI, TINY_TEST_PROJECT_SHORT,
                            TINY_TEST_PROJECT_IDENTITY, TINY_TEST_PROJECT_CALLBACK};
    for (const auto *binary : invalid)
    {
        put(fs::u8path(binary)); events.clear(); ProjectModuleHost rejected;
        Check(!rejected.Load(selection, api) && !rejected.GetLastError().empty(), "incompatible real binary rejected");
        Check(events.empty() && rejected.GetLoadedLibraryPath().empty(), "only_valid_modules_reach_load_callback");
    }
    const char *compatible[] = {TINY_TEST_PROJECT_OPTIONAL, TINY_TEST_PROJECT_LARGE};
    for (const auto *binary : compatible)
    {
        put(fs::u8path(binary)); events.clear(); ProjectModuleHost compatibleHost;
        Check(compatibleHost.Load(selection, api), "optional absent tail or larger API accepted");
        compatibleHost.Close();
        Check(events == std::vector<std::string>({"load", "unload"}), "compatible table unloads exactly once");
    }
    fs::remove(selection.libraryPath);
    ProjectModuleHost missing;
    Check(!missing.Load(selection, api) && missing.GetState() == ProjectModuleState::Missing, "missing DLL state is explicit");
    PluginManager plugins; std::vector<std::string> pluginLog;
    plugins.SetLogCallback([&](TinyPluginLogLevel, const std::string &s) { pluginLog.push_back(s); });
    Check(plugins.Load(fs::u8path(TINY_TEST_DYNAMIC_PLUGIN)), "existing plugin ABI still loads");
    plugins.Tick(0.016); plugins.UnloadAll();
    Check(std::find(pluginLog.begin(), pluginLog.end(), "dynamic plugin tick") != pluginLog.end(),
          "plugin retained HostAPI remains alive after Startup");
}
}

int main(int argc, char **argv)
{
    try
    {
        const std::string group = argc == 3 && std::string(argv[1]) == "--group" ? argv[2] : "manifest";
        if (group == "manifest") ManifestTests();
        else if (group == "loader") LoaderTests();
        else throw std::runtime_error("unknown test group");
        std::cout << checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
