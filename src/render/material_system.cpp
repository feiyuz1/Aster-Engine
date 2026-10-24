#include "tinyengine/render/material_system.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/render/material_compiler.h"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace tinyengine
{
namespace
{
void appendMaterialError(std::vector<MaterialDiagnostic>& diagnostics, const std::string& path,
                         const std::string& message)
{
    diagnostics.push_back({MaterialDiagnosticSeverity::Error, path, 0, -1, message});
}
std::string identity(std::string path)
{
#ifdef _WIN32
    for (char& c : path)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
#endif
    return path;
}
std::string chainText(const std::vector<std::string>& chain, const std::string& next)
{
    std::string text;
    for (const auto& path : chain)
        text += path + " -> ";
    return text + next;
}
} // namespace
MaterialSystem::MaterialSystem(ResourceManager& resources) : m_resources(resources) {}
void MaterialSystem::invalidateAsset(const std::string& assetPath)
{
    const auto path = identity(m_resources.resolveAssetPath(assetPath));
    std::unordered_set<std::string> affected{path};
    const auto references = [&](const std::string& reference)
    { return affected.count(identity(m_resources.resolveAssetPath(reference))) != 0; };
    bool expanded = true;
    while (expanded)
    {
        expanded = false;
        for (const auto& entry : m_resolved)
        {
            if (affected.count(identity(entry.first)))
                continue;
            const bool dependency =
                std::any_of(entry.second->dependencies.begin(), entry.second->dependencies.end(), references);
            const bool texture = std::any_of(entry.second->values.begin(), entry.second->values.end(),
                                             [&](const auto& value)
                                             {
                                                 const auto* reference = std::get_if<std::string>(&value);
                                                 return reference && !reference->empty() && references(*reference);
                                             });
            if (dependency || texture)
            {
                affected.insert(identity(entry.first));
                expanded = true;
            }
        }
        for (const auto& entry : m_dependencyAttempts)
            if (!affected.count(identity(entry.first)) &&
                std::any_of(entry.second.begin(), entry.second.end(), references))
            {
                affected.insert(identity(entry.first));
                expanded = true;
            }
    }
    const auto evict = [&](auto& cache)
    {
        for (auto it = cache.begin(); it != cache.end();)
            if (affected.count(identity(it->first)))
                it = cache.erase(it);
            else
                ++it;
    };
    evict(m_resolved);
    evict(m_proxies);
    evict(m_diagnostics);
    evict(m_dependencyAttempts);
    evict(m_activeOverrideTypes);
    evict(m_candidateSources);
    evict(m_textureSnapshots);
    // Compiled-definition lookup uses pointer keys, so clear it with the definition cache.
    m_compiledDefinitions.clear();
    m_definitions.clear();
    for (auto& instance : m_dynamicInstances)
        if (auto dynamic = instance.lock(); dynamic && affected.count(identity(dynamic->m_path)))
        {
            dynamic->m_proxy.reset();
            dynamic->m_cachedGeneration = 0;
        }
}
bool DynamicMaterialInstance::setParameter(const std::string& name, const MaterialParameterValue& value)
{
    const auto p = std::find_if(m_schema.begin(), m_schema.end(), [&](const auto& p) { return p.name == name; });
    if (p == m_schema.end() || p->type != getMaterialParameterType(value) || !isFiniteMaterialValue(value))
        return false;
    m_overrides[name] = value;
    ++m_revision;
    return true;
}
bool DynamicMaterialInstance::clearOverride(const std::string& name)
{
    if (!m_overrides.erase(name))
        return false;
    ++m_revision;
    return true;
}
std::shared_ptr<DynamicMaterialInstance> MaterialSystem::CreateDynamicInstance(const std::string& path)
{
    const auto resolved = resolve(path);
    if (!resolved)
        return {};
    auto instance = std::shared_ptr<DynamicMaterialInstance>(
        new DynamicMaterialInstance(m_resources.resolveAssetPath(path), resolved->definition->parameters));
    m_dynamicInstances.push_back(instance);
    return instance;
}
std::shared_ptr<const MaterialRenderProxy> MaterialSystem::BuildProxy(const std::string& assetPath,
                                                                      DynamicMaterialInstance* dynamic)
{
    const auto path = m_resources.resolveAssetPath(assetPath);
    if (dynamic && dynamic->m_path != path)
        return {};
    if (!dynamic)
    {
        const auto found = m_proxies.find(path);
        if (found != m_proxies.end())
            return found->second;
    }
    auto resolved = resolve(path);
    if (dynamic && resolved && dynamic->m_cachedGeneration == resolved->generation &&
        dynamic->m_cachedRevision == dynamic->m_revision)
        return dynamic->m_proxy;
    std::shared_ptr<const CompiledMaterial> compiled;
    if (resolved)
    {
        const auto found = m_compiledDefinitions.find(resolved->definition.get());
        if (found != m_compiledDefinitions.end())
            compiled = found->second;
        else
        {
            const auto result = MaterialCompiler{}.compile(*resolved->definition);
            auto& diagnostics = m_diagnostics[path];
            diagnostics.insert(diagnostics.end(), result.diagnostics.begin(), result.diagnostics.end());
            if (result.Success())
            {
                auto& cached = m_compiled[result.material->canonicalProgramContent];
                compiled = cached.lock();
                if (!compiled)
                {
                    compiled = result.material;
                    cached = compiled;
                }
                for (auto it = m_compiled.begin(); it != m_compiled.end();)
                    if (it->second.expired())
                        it = m_compiled.erase(it);
                    else
                        ++it;
                m_compiledDefinitions.emplace(resolved->definition.get(), compiled);
            }
        }
    }
    if (!compiled)
    {
        auto fallback = makeBuiltinPbrDefinition();
        fallback.renderState.shadingModel = MaterialShadingModel::Unlit;
        fallback.graph.GetRenderState() = fallback.renderState;
        auto effective = std::make_shared<ResolvedMaterial>();
        effective->definition = std::make_shared<MaterialDefinitionData>(fallback);
        for (const auto& p : fallback.parameters)
            effective->values.push_back(p.name == "BaseColor" ? MaterialParameterValue(Vec4{1, 0, 1, 1})
                                                              : p.defaultValue);
        effective->generation = m_nextGeneration++;
        resolved = effective;
        compiled = MaterialCompiler{}.compile(fallback).material;
    }
    auto proxy = std::make_shared<MaterialRenderProxy>();
    proxy->compiled = compiled;
    proxy->state = resolved->definition->renderState;
    proxy->generation = resolved->generation;
    proxy->legacyShaderPath = resolved->legacyShaderPath;
    proxy->legacyParameters = resolved->legacyParameters;
    std::map<std::string, MaterialParameterValue> values;
    const auto& parameters = resolved->definition->parameters;
    for (std::size_t i = 0; i < parameters.size(); ++i)
        values.emplace(parameters[i].name, resolved->values[i]);
    if (dynamic)
    {
        dynamic->m_schema = parameters;
        for (const auto& entry : dynamic->m_overrides)
        {
            const auto declaration = std::find_if(parameters.begin(), parameters.end(),
                                                  [&](const auto& p) { return p.name == entry.first; });
            if (declaration != parameters.end() && declaration->type == getMaterialParameterType(entry.second))
                values[entry.first] = entry.second;
            else
            {
                const auto message = "Inactive dynamic override after schema change: " + entry.first;
                auto& diagnostics = m_diagnostics[path];
                if (std::none_of(diagnostics.begin(), diagnostics.end(),
                                 [&](const auto& d) { return d.message == message; }))
                    diagnostics.push_back({MaterialDiagnosticSeverity::Warning, path, 0, -1, message});
            }
        }
    }
    {
        for (const auto& entry : std::map<std::string, std::string>{{"PackedEnabled", "PackedMap"},
                                                                    {"MetallicMapEnabled", "MetallicMap"},
                                                                    {"RoughnessMapEnabled", "RoughnessMap"},
                                                                    {"AOMapEnabled", "AOMap"}})
            if (values.count(entry.first) && values.count(entry.second) &&
                std::holds_alternative<float>(values.at(entry.first)) &&
                std::holds_alternative<std::string>(values.at(entry.second)))
                values[entry.first] = std::get<std::string>(values.at(entry.second)).empty() ? 0.f : 1.f;
    }
    std::ostringstream identity;
    identity << std::setprecision(std::numeric_limits<float>::max_digits10);
    identity << compiled->canonicalProgramContent << '\n'
             << proxy->state.twoSided << ' ' << proxy->state.maskCutoff << '\n'
             << proxy->legacyShaderPath << '\n';
    for (const auto& slot : compiled->layout.numericSlots)
    {
        const auto& value = values.at(slot.name);
        proxy->numericSlots.push_back(materialNumericSlot(value));
        writeMaterialValue(identity, value);
        identity << '\n';
    }
    for (const auto& slot : compiled->layout.textureSlots)
    {
        const auto texturePath = m_resources.resolveAssetPath(std::get<std::string>(values.at(slot.name)), path);
        TextureSnapshot snapshot;
        if (!texturePath.empty())
        {
            auto found = m_textureSnapshots.find(texturePath);
            if (found == m_textureSnapshots.end() || m_refreshTextures)
            {
                auto bytes = PlatformFile::ReadAllBytes(fromUtf8Path(texturePath));
                if (found == m_textureSnapshots.end() || found->second.source != bytes)
                {
                    auto resource = std::make_shared<TextureResource>();
                    if (!resource->load(texturePath))
                        resource.reset();
                    m_textureSnapshots[texturePath] = {resource, std::move(bytes), m_nextTextureVersion++};
                }
            }
            const auto& cached = m_textureSnapshots.at(texturePath);
            snapshot.resource = cached.resource;
            snapshot.version = cached.version;
        }
        proxy->textures.push_back(
            {slot.name, slot.slot, snapshot.resource, slot.sampling, snapshot.version, texturePath});
        identity << std::quoted(texturePath) << ':' << snapshot.version << '\n';
    }
    for (const auto& p : proxy->legacyParameters)
        identity << p.first << ' ' << p.second.x << ' ' << p.second.y << ' ' << p.second.z << ' ' << p.second.w << '\n';
    proxy->canonicalValueContent = identity.str();
    proxy->valueHash = std::hash<std::string>{}(proxy->canonicalValueContent);
    if (dynamic)
    {
        dynamic->m_cachedGeneration = resolved->generation;
        dynamic->m_cachedRevision = dynamic->m_revision;
        dynamic->m_proxy = proxy;
    }
    else
        m_proxies[path] = proxy;
    return proxy;
}
std::shared_ptr<const MaterialDefinitionData>
MaterialSystem::LoadDefinition(const std::string& path, std::vector<MaterialDiagnostic>& diagnostics)
{
    const auto found = m_definitions.find(path);
    if (found != m_definitions.end())
        return found->second;
    std::shared_ptr<MaterialDefinitionData> definition;
    if (path == "builtin:PBR")
        definition = std::make_shared<MaterialDefinitionData>(makeBuiltinPbrDefinition());
    else
    {
        const auto source = m_candidateSources.find(path);
        if (source != m_candidateSources.end())
        {
            definition = std::make_shared<MaterialDefinitionData>();
            std::string error;
            if (!definition->graph.parse(source->second, &error))
            {
                appendMaterialError(diagnostics, path, error);
                return {};
            }
            definition->parameters = definition->graph.GetParameters();
            definition->renderState = definition->graph.GetRenderState();
            definition->sourcePath = path;
        }
        else
        {
            MaterialDefinitionResource resource;
            if (!resource.load(path))
            {
                appendMaterialError(diagnostics, path, resource.getError());
                return {};
            }
            definition = std::make_shared<MaterialDefinitionData>(resource.GetData());
        }
    }
    m_definitions[path] = definition;
    return definition;
}
std::shared_ptr<ResolvedMaterial> MaterialSystem::ResolveRecursive(const std::string& path,
                                                                   std::vector<std::string>& chain,
                                                                   std::vector<MaterialDiagnostic>& diagnostics)
{
    m_resolutionTrace.push_back(path);
    MaterialAssetData data;
    if (path != "builtin:PBR")
    {
        const auto draft = m_candidateSources.find(path);
        const auto source =
            draft == m_candidateSources.end() ? PlatformFile::readAllText(fromUtf8Path(path)) : draft->second;
        std::string error;
        if (fromUtf8Path(path).extension() != ".mat" || !parseMaterialAsset(source, data, error))
        {
            appendMaterialError(diagnostics, path, error.empty() ? "Material requires a .mat file" : error);
            return {};
        }
    }
    if (path == "builtin:PBR" || data.graph)
    {
        auto definition = LoadDefinition(path, diagnostics);
        if (!definition)
            return {};
        auto resolved = std::make_shared<ResolvedMaterial>();
        resolved->definition = definition;
        resolved->dependencies.push_back(path);
        for (const auto& p : definition->parameters)
        {
            auto value = p.defaultValue;
            if (auto* texture = std::get_if<std::string>(&value))
                *texture = m_resources.resolveAssetPath(*texture, path);
            resolved->values.push_back(std::move(value));
        }
        return resolved;
    }
    const auto resolveIdentity = identity(path);
    const bool cycle =
        std::any_of(chain.begin(), chain.end(), [&](const auto& p) { return identity(p) == resolveIdentity; });
    if (cycle || chain.size() >= 32)
    {
        appendMaterialError(diagnostics, path,
                            std::string(cycle ? "Material parent cycle: " : "Material parent exceeds 32 instances: ") +
                                chainText(chain, path));
        return {};
    }
    chain.push_back(path);
    struct Pop
    {
        std::vector<std::string>& chain;
        ~Pop()
        {
            chain.pop_back();
        }
    } pop{chain};
    const auto& reference = data.parentPath;
    const auto parentPath = m_resources.resolveAssetPath(reference, path);
    auto parent = ResolveRecursive(parentPath, chain, diagnostics);
    if (!parent)
        return {};
    auto resolved = std::make_shared<ResolvedMaterial>(*parent);
    resolved->dependencies.push_back(parentPath);
    for (const auto& entry : data.overrides)
    {
        const auto& parameters = resolved->definition->parameters;
        const auto declaration =
            std::find_if(parameters.begin(), parameters.end(), [&](const auto& p) { return p.name == entry.first; });
        if (declaration == parameters.end() || declaration->type != getMaterialParameterType(entry.second))
        {
            const auto old = m_activeOverrideTypes.find(path);
            const auto persisted = data.validatedOverrideTypes.find(entry.first);
            if ((persisted != data.validatedOverrideTypes.end() &&
                 persisted->second == getMaterialParameterType(entry.second)) ||
                (m_allowInactive && old != m_activeOverrideTypes.end() && old->second.count(entry.first) &&
                 old->second.at(entry.first) == getMaterialParameterType(entry.second)))
            {
                diagnostics.push_back({MaterialDiagnosticSeverity::Warning, path, 0, -1,
                                       "Inactive override after schema change: " + entry.first});
                continue;
            }
            appendMaterialError(diagnostics, path, "Unknown or mismatched material parameter: " + entry.first);
            return {};
        }
        m_activeOverrideTypes[path][entry.first] = declaration->type;
        auto value = entry.second;
        if (auto* texture = std::get_if<std::string>(&value))
            *texture = m_resources.resolveAssetPath(*texture, path);
        resolved->values[static_cast<std::size_t>(declaration - parameters.begin())] = std::move(value);
    }
    return resolved;
}
MaterialReloadResult MaterialSystem::reload(const std::string& assetPath, MaterialValidator validator)
{
    return publishCandidate(m_resources.resolveAssetPath(assetPath), {}, std::move(validator));
}
MaterialReloadResult MaterialSystem::applyDefinitionDraft(const std::string& assetPath,
                                                          const MaterialDefinitionData& data,
                                                          MaterialValidator validator)
{
    auto graph = data.graph;
    graph.GetParameters() = data.parameters;
    graph.GetRenderState() = data.renderState;
    return publishCandidate(m_resources.resolveAssetPath(assetPath), graph.serialize(), std::move(validator));
}
MaterialReloadResult MaterialSystem::applyInstanceDraft(const std::string& assetPath, const MaterialAssetData& data,
                                                        MaterialValidator validator)
{
    return publishCandidate(m_resources.resolveAssetPath(assetPath), serializeMaterialAsset(data),
                            std::move(validator));
}
MaterialReloadResult MaterialSystem::publishCandidate(const std::string& path, const std::optional<std::string>& source,
                                                      MaterialValidator validator)
{
    MaterialReloadResult result;
    const auto old = m_resolved.find(path);
    if (old != m_resolved.end())
        result.effectiveGeneration = old->second->generation;
    if (path == "builtin:PBR")
    {
        appendMaterialError(result.diagnostics, path, "Builtin definitions cannot be saved or reloaded");
        return result;
    }
    // Capture previously valid schemas for the whole registered dependency
    // closure, including instances absent from the currently rendered scene.
    resolve(path);
    std::unordered_set<std::string> closure{identity(path)};
    const auto registered = m_resources.getAssetRegistry().GetAssets();
    bool expanded = true;
    while (expanded)
    {
        expanded = false;
        for (const auto& asset : registered)
        {
            if (asset.type != "Material")
                continue;
            const auto dependent = m_resources.resolveAssetPath(asset.path);
            if (closure.count(identity(dependent)))
                continue;
            if (std::any_of(asset.dependencies.begin(), asset.dependencies.end(),
                            [&](const auto& dependency)
                            { return closure.count(identity(m_resources.resolveAssetPath(dependency))) != 0; }))
            {
                closure.insert(identity(dependent));
                resolve(dependent);
                expanded = true;
            }
        }
    }
    std::vector<std::string> affected{path};
    for (const auto& entry : m_resolved)
        if (entry.first != path && std::any_of(entry.second->dependencies.begin(), entry.second->dependencies.end(),
                                               [&](const auto& p) { return identity(p) == identity(path); }))
            affected.push_back(entry.first);
    for (const auto& entry : m_dependencyAttempts)
        if (std::find(affected.begin(), affected.end(), entry.first) == affected.end() &&
            std::any_of(entry.second.begin(), entry.second.end(),
                        [&](const auto& p) { return identity(p) == identity(path); }))
            affected.push_back(entry.first);
    auto candidate = *this;
    candidate.m_definitions.clear();
    candidate.m_compiledDefinitions.clear();
    candidate.m_allowInactive = true;
    candidate.m_refreshTextures = true;
    for (const auto& p : affected)
    {
        candidate.m_resolved.erase(p);
        candidate.m_proxies.erase(p);
        candidate.m_diagnostics.erase(p);
    }
    for (auto it = candidate.m_compiled.begin(); it != candidate.m_compiled.end();)
        if (it->second.expired())
            it = candidate.m_compiled.erase(it);
        else
            ++it;
    if (source)
        candidate.m_candidateSources[path] = *source;
    std::unordered_set<std::string> validated;
    for (const auto& p : affected)
    {
        const auto resolved = candidate.resolve(p);
        if (!resolved)
        {
            if (p != path && !m_resolved.count(p))
            {
                candidate.BuildProxy(p);
                continue;
            }
            const auto& errors = candidate.GetDiagnostics(p);
            result.diagnostics.insert(result.diagnostics.end(), errors.begin(), errors.end());
            break;
        }
        const auto compiled = MaterialCompiler{}.compile(*resolved->definition);
        result.diagnostics.insert(result.diagnostics.end(), resolved->diagnostics.begin(), resolved->diagnostics.end());
        result.diagnostics.insert(result.diagnostics.end(), compiled.diagnostics.begin(), compiled.diagnostics.end());
        if (!compiled.Success())
            break;
        if (validated.insert(compiled.material->canonicalProgramContent).second && validator &&
            !validator(*compiled.material, result.diagnostics))
        {
            if (result.diagnostics.empty())
                appendMaterialError(result.diagnostics, p, "Material validator rejected candidate");
            break;
        }
        candidate.BuildProxy(p);
    }
    const bool allBuilt =
        std::all_of(affected.begin(), affected.end(), [&](const auto& p) { return candidate.m_proxies.count(p) != 0; });
    const bool errors = std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                                    [](const auto& d) { return d.severity == MaterialDiagnosticSeverity::Error; });
    if (!allBuilt || errors)
    {
        m_diagnostics[path] = result.diagnostics;
        return result;
    }
    std::map<std::string, std::string> writes;
    if (source)
        writes[path] = *source;
    for (const auto& entry : candidate.m_activeOverrideTypes)
    {
        const auto pending = writes.find(entry.first);
        const auto text =
            pending == writes.end() ? PlatformFile::readAllText(fromUtf8Path(entry.first)) : pending->second;
        MaterialAssetData data;
        std::string error;
        if (!parseMaterialAsset(text, data, error))
            continue;
        bool changed = false;
        for (const auto& type : entry.second)
            if (data.overrides.count(type.first) &&
                getMaterialParameterType(data.overrides.at(type.first)) == type.second &&
                (!data.validatedOverrideTypes.count(type.first) ||
                 data.validatedOverrideTypes.at(type.first) != type.second))
            {
                data.validatedOverrideTypes[type.first] = type.second;
                changed = true;
            }
        if (changed)
            writes[entry.first] = serializeMaterialAsset(data);
    }
    struct SavedFile
    {
        std::string path, text;
        bool existed;
    };
    std::vector<SavedFile> written;
    for (const auto& entry : writes)
    {
        const auto previous = PlatformFile::readAllText(fromUtf8Path(entry.first));
        const bool existed = PlatformFile::exists(fromUtf8Path(entry.first));
        if (previous == entry.second)
            continue;
        if (!PlatformFile::writeAllTextAtomic(fromUtf8Path(entry.first), entry.second))
        {
            appendMaterialError(result.diagnostics, entry.first,
                                "Unable to atomically save material candidate and override provenance");
            for (auto undo = written.rbegin(); undo != written.rend(); ++undo)
            {
                std::error_code error;
                const bool restored = undo->existed
                                          ? PlatformFile::writeAllTextAtomic(fromUtf8Path(undo->path), undo->text)
                                          : std::filesystem::remove(fromUtf8Path(undo->path), error);
                if (!restored || error)
                    appendMaterialError(result.diagnostics, undo->path, "Unable to restore material save transaction");
            }
            m_diagnostics[path] = result.diagnostics;
            return result;
        }
        written.push_back({entry.first, previous, existed});
    }
    m_resolved.swap(candidate.m_resolved);
    m_definitions.swap(candidate.m_definitions);
    m_proxies.swap(candidate.m_proxies);
    m_compiledDefinitions.swap(candidate.m_compiledDefinitions);
    m_compiled.swap(candidate.m_compiled);
    m_diagnostics.swap(candidate.m_diagnostics);
    m_activeOverrideTypes.swap(candidate.m_activeOverrideTypes);
    m_nextGeneration = candidate.m_nextGeneration;
    m_dependencyAttempts.swap(candidate.m_dependencyAttempts);
    m_textureSnapshots.swap(candidate.m_textureSnapshots);
    m_nextTextureVersion = candidate.m_nextTextureVersion;
    m_allowInactive = true;
    for (auto it = m_dynamicInstances.begin(); it != m_dynamicInstances.end();)
    {
        if (const auto dynamic = it->lock())
        {
            const auto resolved = resolve(dynamic->m_path);
            if (resolved)
                dynamic->m_schema = resolved->definition->parameters;
            ++it;
        }
        else
            it = m_dynamicInstances.erase(it);
    }
    for (const auto& p : affected)
        m_resources.reload(p);
    const auto& registry = m_resources.getAssetRegistry();
    if (!registry.GetRootPath().empty())
        m_resources.getAssetRegistry().scan(registry.GetRootPath());
    result.success = true;
    result.effectiveGeneration = m_resolved.at(path)->generation;
    return result;
}
std::shared_ptr<const ResolvedMaterial> MaterialSystem::resolve(const std::string& assetPath)
{
    const auto path = m_resources.resolveAssetPath(assetPath);
    const auto found = m_resolved.find(path);
    if (found != m_resolved.end())
        return found->second;
    std::vector<std::string> chain;
    auto& diagnostics = m_diagnostics[path];
    diagnostics.clear();
    const auto previousOverrideTypes = m_activeOverrideTypes;
    m_resolutionTrace.clear();
    auto resolved = ResolveRecursive(path, chain, diagnostics);
    m_dependencyAttempts[path] = m_resolutionTrace;
    if (!resolved)
    {
        m_activeOverrideTypes = previousOverrideTypes;
        return {};
    }
    resolved->generation = m_nextGeneration++;
    resolved->diagnostics = diagnostics;
    m_resolved[path] = resolved;
    return resolved;
}
const std::vector<MaterialDiagnostic>& MaterialSystem::GetDiagnostics(const std::string& assetPath) const
{
    const auto found = m_diagnostics.find(m_resources.resolveAssetPath(assetPath));
    static const std::vector<MaterialDiagnostic> empty;
    return found == m_diagnostics.end() ? empty : found->second;
}
} // namespace tinyengine
