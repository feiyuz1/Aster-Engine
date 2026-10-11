#include "MiniEngine/Render/MaterialSystem.hpp"
#include "MiniEngine/Render/MaterialCompiler.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <limits>
#include <unordered_set>

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
bool DynamicMaterialInstance::SetParameter(const std::string &name, const MaterialParameterValue &value)
{
    const auto p=std::find_if(m_schema.begin(),m_schema.end(),[&](const auto &p) { return p.name==name; });
    if (p==m_schema.end() || p->type!=GetMaterialParameterType(value) || !IsFiniteMaterialValue(value)) return false;
    m_overrides[name]=value; ++m_revision; return true;
}
bool DynamicMaterialInstance::ClearOverride(const std::string &name)
{ if (!m_overrides.erase(name)) return false; ++m_revision; return true; }
std::shared_ptr<DynamicMaterialInstance> MaterialSystem::CreateDynamicInstance(const std::string &path)
{
    const auto resolved=Resolve(path);
    if (!resolved) return {};
    auto instance=std::shared_ptr<DynamicMaterialInstance>(new DynamicMaterialInstance(m_resources.ResolveAssetPath(path),resolved->definition->parameters));
    m_dynamicInstances.push_back(instance); return instance;
}
std::shared_ptr<const MaterialRenderProxy> MaterialSystem::BuildProxy(const std::string &assetPath, DynamicMaterialInstance *dynamic)
{
    const auto path=m_resources.ResolveAssetPath(assetPath);
    if (dynamic && dynamic->m_path!=path) return {};
    if (!dynamic) { const auto found=m_proxies.find(path); if (found!=m_proxies.end()) return found->second; }
    auto resolved=Resolve(path);
    if (dynamic && resolved && dynamic->m_cachedGeneration==resolved->generation && dynamic->m_cachedRevision==dynamic->m_revision) return dynamic->m_proxy;
    std::shared_ptr<const CompiledMaterial> compiled;
    if (resolved)
    {
        const auto found=m_compiledDefinitions.find(resolved->definition.get());
        if (found!=m_compiledDefinitions.end()) compiled=found->second;
        else
        {
            const auto result=MaterialCompiler{}.Compile(*resolved->definition);
            auto &diagnostics=m_diagnostics[path]; diagnostics.insert(diagnostics.end(),result.diagnostics.begin(),result.diagnostics.end());
            if (result.Success())
            {
                compiled=m_compiled.emplace(result.material->canonicalProgramContent,result.material).first->second;
                m_compiledDefinitions.emplace(resolved->definition.get(),compiled);
            }
        }
    }
    if (!compiled)
    {
        auto fallback=MakeBuiltinPbrDefinition(); fallback.renderState.shadingModel=MaterialShadingModel::Unlit;
        fallback.graph.GetRenderState()=fallback.renderState;
        auto effective=std::make_shared<ResolvedMaterial>(); effective->definition=std::make_shared<MaterialDefinitionData>(fallback);
        for (const auto &p:fallback.parameters) effective->values.push_back(p.name=="BaseColor" ? MaterialParameterValue(Vec4{1,0,1,1}) : p.defaultValue);
        effective->generation=m_nextGeneration++;
        resolved=effective; compiled=MaterialCompiler{}.Compile(fallback).material;
    }
    auto proxy=std::make_shared<MaterialRenderProxy>(); proxy->compiled=compiled; proxy->state=resolved->definition->renderState;
    proxy->generation=resolved->generation; proxy->legacyShaderPath=resolved->legacyShaderPath; proxy->legacyParameters=resolved->legacyParameters;
    std::map<std::string,MaterialParameterValue> values;
    const auto &parameters=resolved->definition->parameters;
    for (std::size_t i=0;i<parameters.size();++i) values.emplace(parameters[i].name,resolved->values[i]);
    if (dynamic)
    {
        dynamic->m_schema=parameters;
        for (const auto &entry:dynamic->m_overrides)
        {
            const auto declaration=std::find_if(parameters.begin(),parameters.end(),[&](const auto &p) { return p.name==entry.first; });
            if (declaration!=parameters.end() && declaration->type==GetMaterialParameterType(entry.second)) values[entry.first]=entry.second;
            else
            {
                const auto message="Inactive dynamic override after schema change: "+entry.first;
                auto &diagnostics=m_diagnostics[path];
                if (std::none_of(diagnostics.begin(),diagnostics.end(),[&](const auto &d) { return d.message==message; })) diagnostics.push_back({MaterialDiagnosticSeverity::Warning,path,0,-1,message});
            }
        }
    }
    if (resolved->definition->sourcePath=="builtin:PBR")
        for (const auto &entry:std::map<std::string,std::string>{{"PackedEnabled","PackedMap"},{"MetallicMapEnabled","MetallicMap"},{"RoughnessMapEnabled","RoughnessMap"},{"AOMapEnabled","AOMap"}})
            values[entry.first]=std::get<std::string>(values.at(entry.second)).empty() ? 0.f : 1.f;
    std::ostringstream identity; identity << std::setprecision(std::numeric_limits<float>::max_digits10);
    identity << compiled->canonicalProgramContent << '\n' << proxy->state.twoSided << ' ' << proxy->state.maskCutoff << '\n' << proxy->legacyShaderPath << '\n';
    for (const auto &slot:compiled->layout.numericSlots)
    { const auto &value=values.at(slot.name); proxy->numericSlots.push_back(MaterialNumericSlot(value)); WriteMaterialValue(identity,value); identity << '\n'; }
    for (const auto &slot:compiled->layout.textureSlots)
    {
        const auto texturePath=m_resources.ResolveAssetPath(std::get<std::string>(values.at(slot.name)),path);
        TextureSnapshot snapshot;
        if (!texturePath.empty())
        {
            auto found=m_textureSnapshots.find(texturePath);
            if (found==m_textureSnapshots.end() || m_refreshTextures)
            {
                auto bytes=PlatformFile::ReadAllBytes(std::filesystem::u8path(texturePath));
                if (found==m_textureSnapshots.end() || found->second.source!=bytes)
                {
                    auto resource=std::make_shared<TextureResource>(); if (!resource->Load(texturePath)) resource.reset();
                    m_textureSnapshots[texturePath]={resource,std::move(bytes),m_nextTextureVersion++};
                }
            }
            const auto &cached=m_textureSnapshots.at(texturePath); snapshot.resource=cached.resource; snapshot.version=cached.version;
        }
        proxy->textures.push_back({slot.name,slot.slot,snapshot.resource,slot.sampling,snapshot.version,texturePath}); identity << std::quoted(texturePath) << ':' << snapshot.version << '\n';
    }
    for (const auto &p:proxy->legacyParameters) identity << p.first << ' ' << p.second.x << ' ' << p.second.y << ' ' << p.second.z << ' ' << p.second.w << '\n';
    proxy->canonicalValueContent=identity.str(); proxy->valueHash=std::hash<std::string>{}(proxy->canonicalValueContent);
    if (dynamic) { dynamic->m_cachedGeneration=resolved->generation; dynamic->m_cachedRevision=dynamic->m_revision; dynamic->m_proxy=proxy; }
    else m_proxies[path]=proxy;
    return proxy;
}
std::shared_ptr<const MaterialDefinitionData> MaterialSystem::LoadDefinition(const std::string &path, std::vector<MaterialDiagnostic> &diagnostics)
{
    const auto found = m_definitions.find(path);
    if (found != m_definitions.end()) return found->second;
    std::shared_ptr<MaterialDefinitionData> definition;
    if (path == "builtin:PBR") definition = std::make_shared<MaterialDefinitionData>(MakeBuiltinPbrDefinition());
    else
    {
        const auto source=m_candidateSources.find(path);
        if (source!=m_candidateSources.end())
        {
            definition=std::make_shared<MaterialDefinitionData>(); std::string error;
            if (!definition->graph.Parse(source->second,&error) || !definition->graph.UpgradeLegacy()) { Error(diagnostics,path,error); return {}; }
            definition->parameters=definition->graph.GetParameters(); definition->renderState=definition->graph.GetRenderState(); definition->sourcePath=path;
        }
        else
        {
            MaterialDefinitionResource resource;
            if (!resource.Load(path)) { Error(diagnostics, path, resource.GetError()); return {}; }
            definition = std::make_shared<MaterialDefinitionData>(resource.GetData());
        }
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
    const auto draft=m_candidateSources.find(path);
    const auto source = draft==m_candidateSources.end() ? PlatformFile::ReadAllText(std::filesystem::u8path(path)) : draft->second;
    if (source.empty()) { Error(diagnostics, path, "Material file not found: " + path); return {}; }
    std::istringstream headerStream(source);
    std::string header;
    headerStream >> header;
    if (header != "TINY_MAT")
    {
        MaterialResource legacy;
        if (!legacy.LoadWithResolver(path, [this](const auto &reference, const auto &owner) { return m_resources.ResolveAssetPath(reference, owner); }, 33 - chain.size(),[this](const auto &p) {
            const auto found=m_candidateSources.find(p); return found==m_candidateSources.end() ? PlatformFile::ReadAllText(std::filesystem::u8path(p)) : found->second;
        }))
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
            {"MetallicMapEnabled", old.metallicTexturePath.empty() ? 0.f : 1.f},
            {"RoughnessMapEnabled", old.roughnessTexturePath.empty() ? 0.f : 1.f}, {"AOMapEnabled", old.aoTexturePath.empty() ? 0.f : 1.f},
            {"PackedLayout", old.packedTextureLayout == "RMA" ? 1.f : 0.f}};
        for (const auto &p : definition->parameters)
        {
            const auto found = values.find(p.name);
            resolved->values.push_back(found == values.end() ? p.defaultValue : found->second);
        }
        if (!legacy.GetParentPath().empty())
        {
            const auto parentPath=m_resources.ResolveAssetPath(legacy.GetParentPath(),path);
            auto parent=ResolveRecursive(parentPath,chain,diagnostics); if (!parent) return {};
            resolved->dependencies=parent->dependencies; resolved->dependencies.push_back(parentPath);
        }
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
        {
            const auto old=m_activeOverrideTypes.find(path);
            if (m_allowInactive && old!=m_activeOverrideTypes.end() && old->second.count(entry.first) && old->second.at(entry.first)==GetMaterialParameterType(entry.second))
            { diagnostics.push_back({MaterialDiagnosticSeverity::Warning,path,0,-1,"Inactive override after schema change: "+entry.first}); continue; }
            Error(diagnostics, path, "Unknown or mismatched material parameter: " + entry.first); return {};
        }
        m_activeOverrideTypes[path][entry.first]=declaration->type;
        auto value = entry.second;
        if (auto *texture = std::get_if<std::string>(&value)) *texture = m_resources.ResolveAssetPath(*texture, path);
        resolved->values[static_cast<std::size_t>(declaration - parameters.begin())] = std::move(value);
    }
    return resolved;
}
MaterialReloadResult MaterialSystem::Reload(const std::string &assetPath,MaterialValidator validator)
{ return PublishCandidate(m_resources.ResolveAssetPath(assetPath),{},std::move(validator)); }
MaterialReloadResult MaterialSystem::ApplyDefinitionDraft(const std::string &assetPath,const MaterialDefinitionData &data,MaterialValidator validator)
{
    auto graph=data.graph; graph.GetParameters()=data.parameters; graph.GetRenderState()=data.renderState;
    return PublishCandidate(m_resources.ResolveAssetPath(assetPath),graph.Serialize(),std::move(validator));
}
MaterialReloadResult MaterialSystem::ApplyInstanceDraft(const std::string &assetPath,const MaterialAssetData &data,MaterialValidator validator)
{
    return PublishCandidate(m_resources.ResolveAssetPath(assetPath),SerializeMaterialAsset(data),std::move(validator));
}
MaterialReloadResult MaterialSystem::PublishCandidate(const std::string &path,const std::optional<std::string> &source,MaterialValidator validator)
{
    MaterialReloadResult result;
    const auto old=m_resolved.find(path); if (old!=m_resolved.end()) result.effectiveGeneration=old->second->generation;
    if (path=="builtin:PBR") { Error(result.diagnostics,path,"Builtin definitions cannot be saved or reloaded"); return result; }
    std::vector<std::string> affected{path};
    for (const auto &entry:m_resolved)
        if (entry.first!=path && std::any_of(entry.second->dependencies.begin(),entry.second->dependencies.end(),[&](const auto &p) { return Identity(p)==Identity(path); })) affected.push_back(entry.first);
    auto candidate=*this;
    candidate.m_definitions.clear(); candidate.m_compiledDefinitions.clear(); candidate.m_allowInactive=true;
    candidate.m_refreshTextures=true;
    for (const auto &p:affected) { candidate.m_resolved.erase(p); candidate.m_proxies.erase(p); candidate.m_diagnostics.erase(p); }
    if (source) candidate.m_candidateSources[path]=*source;
    std::unordered_set<std::string> validated;
    for (const auto &p:affected)
    {
        const auto resolved=candidate.Resolve(p);
        if (!resolved)
        { const auto &errors=candidate.GetDiagnostics(p); result.diagnostics.insert(result.diagnostics.end(),errors.begin(),errors.end()); break; }
        const auto compiled=MaterialCompiler{}.Compile(*resolved->definition);
        result.diagnostics.insert(result.diagnostics.end(),resolved->diagnostics.begin(),resolved->diagnostics.end());
        result.diagnostics.insert(result.diagnostics.end(),compiled.diagnostics.begin(),compiled.diagnostics.end());
        if (!compiled.Success()) break;
        if (validated.insert(compiled.material->canonicalProgramContent).second && validator && !validator(*compiled.material,result.diagnostics))
        { if (result.diagnostics.empty()) Error(result.diagnostics,p,"Material validator rejected candidate"); break; }
        candidate.BuildProxy(p);
    }
    const bool allBuilt=std::all_of(affected.begin(),affected.end(),[&](const auto &p) { return candidate.m_proxies.count(p)!=0; });
    const bool errors=std::any_of(result.diagnostics.begin(),result.diagnostics.end(),[](const auto &d) { return d.severity==MaterialDiagnosticSeverity::Error; });
    if (!allBuilt || errors) { m_diagnostics[path]=result.diagnostics; return result; }
    if (source && !PlatformFile::WriteAllTextAtomic(std::filesystem::u8path(path),*source))
    { Error(result.diagnostics,path,"Unable to atomically save material candidate"); m_diagnostics[path]=result.diagnostics; return result; }
    m_resolved.swap(candidate.m_resolved); m_definitions.swap(candidate.m_definitions); m_proxies.swap(candidate.m_proxies);
    m_compiledDefinitions.swap(candidate.m_compiledDefinitions); m_compiled.swap(candidate.m_compiled); m_diagnostics.swap(candidate.m_diagnostics);
    m_activeOverrideTypes.swap(candidate.m_activeOverrideTypes); m_nextGeneration=candidate.m_nextGeneration;
    m_textureSnapshots.swap(candidate.m_textureSnapshots); m_nextTextureVersion=candidate.m_nextTextureVersion;
    m_allowInactive=true;
    for (auto it=m_dynamicInstances.begin();it!=m_dynamicInstances.end();)
    {
        if (const auto dynamic=it->lock())
        { const auto resolved=Resolve(dynamic->m_path); if (resolved) dynamic->m_schema=resolved->definition->parameters; ++it; }
        else it=m_dynamicInstances.erase(it);
    }
    for (const auto &p:affected) m_resources.Reload(p);
    const auto &registry=m_resources.GetAssetRegistry();
    if (!registry.GetRootPath().empty()) m_resources.GetAssetRegistry().Scan(registry.GetRootPath());
    result.success=true; result.effectiveGeneration=m_resolved.at(path)->generation; return result;
}
std::shared_ptr<const ResolvedMaterial> MaterialSystem::Resolve(const std::string &assetPath)
{
    const auto path = m_resources.ResolveAssetPath(assetPath);
    const auto found = m_resolved.find(path);
    if (found != m_resolved.end()) return found->second;
    std::vector<std::string> chain;
    auto &diagnostics = m_diagnostics[path];
    diagnostics.clear();
    const auto previousOverrideTypes=m_activeOverrideTypes;
    auto resolved = ResolveRecursive(path, chain, diagnostics);
    if (!resolved) { m_activeOverrideTypes=previousOverrideTypes; return {}; }
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
