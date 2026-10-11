#include "MiniEngine/Render/MaterialSystem.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include <algorithm>
#include <sstream>

namespace MiniEngine
{
namespace
{
void Error(std::vector<MaterialDiagnostic> &diagnostics, const std::string &path, const std::string &message)
{ diagnostics.push_back({MaterialDiagnosticSeverity::Error, path, 0, -1, message}); }
std::string Identity(std::string path)
{
#ifdef _WIN32
    for (char &c : path) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
#endif
    return path;
}
std::string ChainText(const std::vector<std::string> &chain, const std::string &next)
{
    std::string text;
    for (const auto &path : chain) text += path + " -> ";
    return text + next;
}
}
MaterialSystem::MaterialSystem(ResourceManager &resources) : m_resources(resources) {}
std::shared_ptr<const MaterialDefinitionData> MaterialSystem::LoadDefinition(const std::string &path, std::vector<MaterialDiagnostic> &diagnostics)
{
    const auto found = m_definitions.find(path);
    if (found != m_definitions.end()) return found->second;
    std::shared_ptr<MaterialDefinitionData> definition;
    if (path == "builtin:PBR") definition = std::make_shared<MaterialDefinitionData>(MakeBuiltinPbrDefinition());
    else
    {
        MaterialDefinitionResource resource;
        if (!resource.Load(path)) { Error(diagnostics, path, resource.GetError()); return {}; }
        definition = std::make_shared<MaterialDefinitionData>(resource.GetData());
    }
    m_definitions[path] = definition;
    return definition;
}
std::shared_ptr<ResolvedMaterial> MaterialSystem::ResolveRecursive(const std::string &path, std::vector<std::string> &chain,
                                                                 std::vector<MaterialDiagnostic> &diagnostics)
{
    if (path == "builtin:PBR" || std::filesystem::u8path(path).extension() == ".matgraph")
    {
        auto definition = LoadDefinition(path, diagnostics);
        if (!definition) return {};
        auto resolved = std::make_shared<ResolvedMaterial>();
        resolved->definition = definition;
        resolved->dependencies.push_back(path);
        for (const auto &p : definition->parameters)
        {
            auto value = p.defaultValue;
            if (auto *texture = std::get_if<std::string>(&value)) *texture = m_resources.ResolveAssetPath(*texture, path);
            resolved->values.push_back(std::move(value));
        }
        return resolved;
    }
    const auto identity = Identity(path);
    const bool cycle = std::any_of(chain.begin(), chain.end(), [&](const auto &p) { return Identity(p) == identity; });
    if (cycle || chain.size() >= 32)
    {
        Error(diagnostics, path, std::string(cycle ? "Material parent cycle: " : "Material parent exceeds 32 instances: ") + ChainText(chain, path));
        return {};
    }
    chain.push_back(path);
    struct Pop { std::vector<std::string> &chain; ~Pop() { chain.pop_back(); } } pop{chain};
    const auto source = PlatformFile::ReadAllText(std::filesystem::u8path(path));
    if (source.empty()) { Error(diagnostics, path, "Material file not found: " + path); return {}; }
    std::istringstream headerStream(source);
    std::string header;
    headerStream >> header;
    if (header != "TINY_MAT")
    {
        MaterialResource legacy;
        if (!legacy.LoadWithResolver(path, [this](const auto &reference, const auto &owner) { return m_resources.ResolveAssetPath(reference, owner); }, 33 - chain.size()))
        { Error(diagnostics, path, legacy.GetError()); return {}; }
        auto definition = std::make_shared<MaterialDefinitionData>(*LoadDefinition("builtin:PBR", diagnostics));
        const auto &old = legacy.GetMaterialData().legacyData;
        definition->renderState.blendMode = old.transparent ? MaterialBlendMode::Alpha : MaterialBlendMode::Opaque;
        definition->graph.GetRenderState() = definition->renderState;
        auto resolved = std::make_shared<ResolvedMaterial>();
        resolved->definition = definition;
        std::map<std::string, MaterialParameterValue> values = {
            {"BaseColor", old.baseColor}, {"Metallic", old.metallic}, {"Roughness", old.roughness}, {"Emissive", old.emissive},
            {"BaseMap", old.texturePath}, {"NormalMap", old.normalTexturePath}, {"MetallicMap", old.metallicTexturePath},
            {"RoughnessMap", old.roughnessTexturePath}, {"AOMap", old.aoTexturePath}, {"EmissiveMap", old.emissiveTexturePath},
            {"PackedMap", old.packedTexturePath}, {"PackedEnabled", old.packedTexturePath.empty() ? 0.f : 1.f},
            {"PackedLayout", old.packedTextureLayout == "RMA" ? 1.f : 0.f}};
        for (const auto &p : definition->parameters)
        {
            const auto found = values.find(p.name);
            resolved->values.push_back(found == values.end() ? p.defaultValue : found->second);
        }
        if (!legacy.GetParentPath().empty()) resolved->dependencies.push_back(m_resources.ResolveAssetPath(legacy.GetParentPath(), path));
        resolved->legacyShaderPath = old.shaderPath;
        resolved->legacyParameters = old.parameters;
        return resolved;
    }
    MaterialAssetData data;
    std::string error;
    if (!ParseMaterialAsset(source, data, error)) { Error(diagnostics, path, error); return {}; }
    const auto reference = !data.parentPath.empty() ? data.parentPath : data.definitionPath;
    const auto parentPath = m_resources.ResolveAssetPath(reference, path);
    auto parent = ResolveRecursive(parentPath, chain, diagnostics);
    if (!parent) return {};
    auto resolved = std::make_shared<ResolvedMaterial>(*parent);
    resolved->dependencies.push_back(parentPath);
    for (const auto &entry : data.overrides)
    {
        const auto &parameters = resolved->definition->parameters;
        const auto declaration = std::find_if(parameters.begin(), parameters.end(), [&](const auto &p) { return p.name == entry.first; });
        if (declaration == parameters.end() || declaration->type != GetMaterialParameterType(entry.second))
        { Error(diagnostics, path, "Unknown or mismatched material parameter: " + entry.first); return {}; }
        auto value = entry.second;
        if (auto *texture = std::get_if<std::string>(&value)) *texture = m_resources.ResolveAssetPath(*texture, path);
        resolved->values[static_cast<std::size_t>(declaration - parameters.begin())] = std::move(value);
    }
    return resolved;
}
std::shared_ptr<const ResolvedMaterial> MaterialSystem::Resolve(const std::string &assetPath)
{
    const auto path = m_resources.ResolveAssetPath(assetPath);
    const auto found = m_resolved.find(path);
    if (found != m_resolved.end()) return found->second;
    std::vector<std::string> chain;
    auto &diagnostics = m_diagnostics[path];
    diagnostics.clear();
    auto resolved = ResolveRecursive(path, chain, diagnostics);
    if (!resolved) return {};
    resolved->generation = m_nextGeneration++;
    resolved->diagnostics = diagnostics;
    m_resolved[path] = resolved;
    return resolved;
}
const std::vector<MaterialDiagnostic> &MaterialSystem::GetDiagnostics(const std::string &assetPath) const
{
    const auto found = m_diagnostics.find(m_resources.ResolveAssetPath(assetPath));
    static const std::vector<MaterialDiagnostic> empty;
    return found == m_diagnostics.end() ? empty : found->second;
}
} // namespace MiniEngine
