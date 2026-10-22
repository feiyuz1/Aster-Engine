#include "tinyengine/project/project_module_manifest.h"
#include "tinyengine/project/project.h"
#include "tinyengine/project_sdk/build_info.h"
#include "tinyengine/project_sdk/sdk_info.h"

#include "tinyengine/platform/platform.h"
#include <algorithm>
#include <charconv>
#include <cwctype>
#include <string_view>
#include <system_error>

namespace tinyengine
{
namespace
{
bool Fail(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
    return false;
}

bool identifier(const std::string& value)
{
    if (value.empty() || value.size() > 256 || value == "." || value == "..")
        return false;
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char c)
                       {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                  c == '.' || c == '-' || c == '_';
                       });
}

bool Unsigned(const std::string& text, uint32_t& out)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool sameComponent(const std::filesystem::path& left, const std::filesystem::path& right)
{
#ifdef _WIN32
    auto a = left.wstring(), b = right.wstring();
    std::transform(a.begin(), a.end(), a.begin(), [](wchar_t c) { return std::towlower(c); });
    std::transform(b.begin(), b.end(), b.begin(), [](wchar_t c) { return std::towlower(c); });
    return a == b;
#else
    return left == right;
#endif
}

bool resolveInside(const std::filesystem::path& root, const std::filesystem::path& relative, std::filesystem::path& out,
                   std::string* error, const std::string& key)
{
    if (relative.empty() || relative.has_root_path())
        return Fail(error, key + " must be project-relative");
    for (const auto& part : relative)
        if (part == "..")
            return Fail(error, key + " cannot contain '..'");
    std::error_code ec;
    out = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec)
        return Fail(error, key + ": " + ec.message());
    auto child = out.begin();
    for (auto parent = root.begin(); parent != root.end(); ++parent, ++child)
        if (child == out.end() || !sameComponent(*parent, *child))
            return Fail(error, key + " resolves outside project root: " + toUtf8Path(out));
    if (child == out.end())
        return Fail(error, key + " must identify a file");
    return true;
}

bool spanEquals(TinyProjectString value, std::string_view expected)
{
    return value.data && value.size == expected.size() && value.size <= 4096 &&
           std::string_view(value.data, static_cast<std::size_t>(value.size)) == expected;
}
} // namespace

const char* projectCodeProfileName(const ProjectCodeProfile profile) noexcept
{
    return profile == ProjectCodeProfile::Development ? "Development" : "Shipping";
}

bool loadProjectModuleSelection(const ProjectSettings& settings, const ProjectCodeProfile profile,
                                ProjectModuleSelection& out, std::string* error)
{
    out = {};
    if (error)
        error->clear();
    if (settings.projectFile.empty() || !identifier(settings.projectId) || !identifier(settings.codeModule))
        return Fail(error, "Project code requires project.id, code.module and an absolute project file");
    if (profile != ProjectCodeProfile::Development && profile != ProjectCodeProfile::Shipping)
        return Fail(error, "Unsupported project code profile");
    std::error_code ec;
    ProjectModuleSelection selected;
    selected.projectRoot = std::filesystem::weakly_canonical(settings.projectFile.parent_path(), ec);
    if (ec || !selected.projectRoot.is_absolute())
        return Fail(error, "Cannot resolve project root");
    std::filesystem::path manifestPath;
    if (!resolveInside(selected.projectRoot, settings.codeManifest, manifestPath, error, "code.manifest"))
        return false;
    Config config;
    if (!config.load(toUtf8Path(manifestPath)))
        return Fail(error, "Cannot read module manifest: " + toUtf8Path(manifestPath));
    selected.moduleId = config.getString("module.id");
    selected.projectId = config.getString("project.id");
    selected.profile = profile;
    if (selected.moduleId != settings.codeModule)
        return Fail(error, "module.id mismatch in " + toUtf8Path(manifestPath));
    if (selected.projectId != settings.projectId)
        return Fail(error, "project.id mismatch in " + toUtf8Path(manifestPath));
    uint32_t abi = 0;
    if (!Unsigned(config.getString("module.abi"), abi) || abi != TINY_PROJECT_ABI_VERSION)
        return Fail(error, "module.abi mismatch");
    if (config.getString("module.platform") != TINYENGINE_PROJECT_SDK_PLATFORM)
        return Fail(error, "module.platform mismatch");
    const std::string prefix = std::string(projectCodeProfileName(profile)) + '.';
    selected.sdkBuildId = config.getString(prefix + "sdkBuildId", config.getString("sdk.buildId"));
    if (selected.sdkBuildId != project_sdk::getSdkBuildId())
        return Fail(error, "sdk.buildId mismatch for " + prefix);
    selected.projectBuildId = config.getString(prefix + "buildId");
    selected.sourceFingerprint = config.getString(prefix + "sourceFingerprint");
    if (!identifier(selected.projectBuildId))
        return Fail(error, prefix + "buildId is missing or invalid");
    if (!identifier(selected.sourceFingerprint))
        return Fail(error, prefix + "sourceFingerprint is missing or invalid");
    if (config.getString(prefix + "schemaHash") != "none")
        return Fail(error, prefix + "schemaHash must be none for P0");
    if (!resolveInside(selected.projectRoot, fromUtf8Path(config.getString(prefix + "library")), selected.libraryPath,
                       error, prefix + "library"))
        return false;
    const auto pdb = config.getString(prefix + "pdb");
    if (!pdb.empty() &&
        !resolveInside(selected.projectRoot, fromUtf8Path(pdb), selected.pdbPath, error, prefix + "pdb"))
        return false;
    uint32_t count = 0;
    if (!Unsigned(config.getString(prefix + "dependencies.count"), count) || count > 256)
        return Fail(error, prefix + "dependencies.count is invalid");
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto key = prefix + "dependencies." + std::to_string(i);
        std::filesystem::path dependency;
        if (!resolveInside(selected.projectRoot, fromUtf8Path(config.getString(key)), dependency, error, key))
            return false;
        selected.dependencies.push_back(std::move(dependency));
    }
    out = std::move(selected);
    return true;
}

bool validateProjectModuleInfo(const TinyProjectModuleInfo& info, const ProjectModuleSelection& selection,
                               std::string* error)
{
    if (error)
        error->clear();
    if (info.struct_size < sizeof(TinyProjectModuleInfo))
        return Fail(error, "Module metadata struct_size is too small");
    if (!spanEquals(info.module_id, selection.moduleId))
        return Fail(error, "Binary module.id mismatch");
    if (!spanEquals(info.project_id, selection.projectId))
        return Fail(error, "Binary project.id mismatch");
    if (!spanEquals(info.sdk_build_id, selection.sdkBuildId))
        return Fail(error, "Binary sdk.buildId mismatch");
    if (!spanEquals(info.project_build_id, selection.projectBuildId))
        return Fail(error, "Binary buildId mismatch");
    if (!spanEquals(info.source_fingerprint, selection.sourceFingerprint))
        return Fail(error, "Binary sourceFingerprint mismatch");
    if (!spanEquals(info.profile, projectCodeProfileName(selection.profile)))
        return Fail(error, "Binary profile mismatch");
    return true;
}
} // namespace tinyengine
