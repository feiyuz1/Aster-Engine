#include "MiniEngine/Project/ProjectModuleManifest.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/ProjectSDK/SDKInfo.hpp"
#include "MiniEngine/ProjectSDK/BuildInfo.h"

#include <algorithm>
#include <charconv>
#include <cwctype>
#include <string_view>
#include <system_error>

namespace MiniEngine
{
namespace
{
bool Fail(std::string *error, const std::string &message)
{
    if (error) *error = message;
    return false;
}

bool Identifier(const std::string &value)
{
    if (value.empty() || value.size() > 256 || value == "." || value == "..") return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '.' || c == '-' || c == '_';
    });
}

bool Unsigned(const std::string &text, uint32_t &out)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool SameComponent(const std::filesystem::path &left, const std::filesystem::path &right)
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

bool ResolveInside(const std::filesystem::path &root, const std::filesystem::path &relative,
                   std::filesystem::path &out, std::string *error, const std::string &key)
{
    if (relative.empty() || relative.has_root_path()) return Fail(error, key + " must be project-relative");
    for (const auto &part : relative)
        if (part == "..") return Fail(error, key + " cannot contain '..'");
    std::error_code ec;
    out = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec) return Fail(error, key + ": " + ec.message());
    auto child = out.begin();
    for (auto parent = root.begin(); parent != root.end(); ++parent, ++child)
        if (child == out.end() || !SameComponent(*parent, *child))
            return Fail(error, key + " resolves outside project root: " + out.u8string());
    if (child == out.end()) return Fail(error, key + " must identify a file");
    return true;
}

bool SpanEquals(TinyProjectString value, std::string_view expected)
{
    return value.data && value.size == expected.size() && value.size <= 4096 &&
           std::string_view(value.data, static_cast<std::size_t>(value.size)) == expected;
}
}

const char *ProjectCodeProfileName(const ProjectCodeProfile profile) noexcept
{
    return profile == ProjectCodeProfile::Development ? "Development" : "Shipping";
}

bool LoadProjectModuleSelection(const ProjectSettings &settings, const ProjectCodeProfile profile,
                                ProjectModuleSelection &out, std::string *error)
{
    out = {};
    if (error) error->clear();
    if (settings.projectFile.empty() || !Identifier(settings.projectId) || !Identifier(settings.codeModule))
        return Fail(error, "Project code requires project.id, code.module and an absolute project file");
    if (profile != ProjectCodeProfile::Development && profile != ProjectCodeProfile::Shipping)
        return Fail(error, "Unsupported project code profile");
    std::error_code ec;
    ProjectModuleSelection selected;
    selected.projectRoot = std::filesystem::weakly_canonical(settings.projectFile.parent_path(), ec);
    if (ec || !selected.projectRoot.is_absolute()) return Fail(error, "Cannot resolve project root");
    std::filesystem::path manifestPath;
    if (!ResolveInside(selected.projectRoot, settings.codeManifest, manifestPath, error, "code.manifest")) return false;
    Config config;
    if (!config.Load(manifestPath.u8string())) return Fail(error, "Cannot read module manifest: " + manifestPath.u8string());
    selected.moduleId = config.GetString("module.id");
    selected.projectId = config.GetString("project.id");
    selected.profile = profile;
    if (selected.moduleId != settings.codeModule) return Fail(error, "module.id mismatch in " + manifestPath.u8string());
    if (selected.projectId != settings.projectId) return Fail(error, "project.id mismatch in " + manifestPath.u8string());
    uint32_t abi = 0;
    if (!Unsigned(config.GetString("module.abi"), abi) || abi != TINY_PROJECT_ABI_VERSION)
        return Fail(error, "module.abi mismatch");
    if (config.GetString("module.platform") != TINYENGINE_PROJECT_SDK_PLATFORM)
        return Fail(error, "module.platform mismatch");
    const std::string prefix = std::string(ProjectCodeProfileName(profile)) + '.';
    selected.sdkBuildId = config.GetString(prefix + "sdkBuildId", config.GetString("sdk.buildId"));
    if (selected.sdkBuildId != ProjectSDK::GetSDKBuildId()) return Fail(error, "sdk.buildId mismatch for " + prefix);
    selected.projectBuildId = config.GetString(prefix + "buildId");
    selected.sourceFingerprint = config.GetString(prefix + "sourceFingerprint");
    if (!Identifier(selected.projectBuildId)) return Fail(error, prefix + "buildId is missing or invalid");
    if (!Identifier(selected.sourceFingerprint)) return Fail(error, prefix + "sourceFingerprint is missing or invalid");
    if (config.GetString(prefix + "schemaHash") != "none") return Fail(error, prefix + "schemaHash must be none for P0");
    if (!ResolveInside(selected.projectRoot, std::filesystem::u8path(config.GetString(prefix + "library")),
                       selected.libraryPath, error, prefix + "library")) return false;
    const auto pdb = config.GetString(prefix + "pdb");
    if (!pdb.empty() && !ResolveInside(selected.projectRoot, std::filesystem::u8path(pdb), selected.pdbPath, error, prefix + "pdb"))
        return false;
    uint32_t count = 0;
    if (!Unsigned(config.GetString(prefix + "dependencies.count"), count) || count > 256)
        return Fail(error, prefix + "dependencies.count is invalid");
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto key = prefix + "dependencies." + std::to_string(i);
        std::filesystem::path dependency;
        if (!ResolveInside(selected.projectRoot, std::filesystem::u8path(config.GetString(key)), dependency, error, key)) return false;
        selected.dependencies.push_back(std::move(dependency));
    }
    out = std::move(selected);
    return true;
}

bool ValidateProjectModuleInfo(const TinyProjectModuleInfo &info, const ProjectModuleSelection &selection, std::string *error)
{
    if (error) error->clear();
    if (info.struct_size < sizeof(TinyProjectModuleInfo)) return Fail(error, "Module metadata struct_size is too small");
    if (!SpanEquals(info.module_id, selection.moduleId)) return Fail(error, "Binary module.id mismatch");
    if (!SpanEquals(info.project_id, selection.projectId)) return Fail(error, "Binary project.id mismatch");
    if (!SpanEquals(info.sdk_build_id, selection.sdkBuildId)) return Fail(error, "Binary sdk.buildId mismatch");
    if (!SpanEquals(info.project_build_id, selection.projectBuildId)) return Fail(error, "Binary buildId mismatch");
    if (!SpanEquals(info.source_fingerprint, selection.sourceFingerprint)) return Fail(error, "Binary sourceFingerprint mismatch");
    if (!SpanEquals(info.profile, ProjectCodeProfileName(selection.profile))) return Fail(error, "Binary profile mismatch");
    return true;
}
}
