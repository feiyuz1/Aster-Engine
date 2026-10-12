#pragma once
#include "MiniEngine/Resource/MaterialDefinitionResource.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"
#include <memory>
#include <unordered_map>
#include <functional>

namespace MiniEngine
{
struct ResolvedMaterial
{
    std::shared_ptr<const MaterialDefinitionData> definition;
    std::vector<MaterialParameterValue> values;
    std::vector<MaterialDiagnostic> diagnostics;
    std::uint64_t generation = 0;
    std::vector<std::string> dependencies;
    std::string legacyShaderPath;
    std::map<std::string, Vec4> legacyParameters;
};
class DynamicMaterialInstance
{
public:
    bool SetParameter(const std::string &name, const MaterialParameterValue &value);
    bool ClearOverride(const std::string &name);
    std::uint64_t GetRevision() const { return m_revision; }
    const std::string &GetAssetPath() const { return m_path; }
private:
    friend class MaterialSystem;
    DynamicMaterialInstance(std::string path, std::vector<MaterialParameterDecl> schema) : m_path(std::move(path)), m_schema(std::move(schema)) {}
    std::string m_path;
    std::vector<MaterialParameterDecl> m_schema;
    std::map<std::string, MaterialParameterValue> m_overrides;
    std::uint64_t m_revision = 0, m_cachedRevision = ~std::uint64_t{0}, m_cachedGeneration = 0;
    std::shared_ptr<const MaterialRenderProxy> m_proxy;
};
using MaterialValidator=std::function<bool(const CompiledMaterial &,std::vector<MaterialDiagnostic> &)>;
struct MaterialReloadResult
{
    bool success=false;
    std::uint64_t effectiveGeneration=0;
    std::vector<MaterialDiagnostic> diagnostics;
};
class MaterialSystem
{
public:
    explicit MaterialSystem(ResourceManager &resources);
    std::shared_ptr<const ResolvedMaterial> Resolve(const std::string &assetPath);
    std::shared_ptr<const MaterialRenderProxy> BuildProxy(const std::string &assetPath, DynamicMaterialInstance *dynamic = nullptr);
    std::shared_ptr<DynamicMaterialInstance> CreateDynamicInstance(const std::string &assetPath);
    MaterialReloadResult Reload(const std::string &assetPath,MaterialValidator validator={});
    MaterialReloadResult ApplyDefinitionDraft(const std::string &assetPath,const MaterialDefinitionData &,MaterialValidator validator={});
    MaterialReloadResult ApplyInstanceDraft(const std::string &assetPath,const MaterialAssetData &,MaterialValidator validator={});
    const std::vector<MaterialDiagnostic> &GetDiagnostics(const std::string &assetPath) const;
private:
    std::shared_ptr<ResolvedMaterial> ResolveRecursive(const std::string &path, std::vector<std::string> &chain,
                                                     std::vector<MaterialDiagnostic> &diagnostics);
    std::shared_ptr<const MaterialDefinitionData> LoadDefinition(const std::string &path, std::vector<MaterialDiagnostic> &diagnostics);
    MaterialReloadResult PublishCandidate(const std::string &path,const std::optional<std::string> &source,MaterialValidator validator);
    ResourceManager &m_resources;
    std::unordered_map<std::string, std::shared_ptr<const ResolvedMaterial>> m_resolved;
    std::unordered_map<std::string, std::shared_ptr<const MaterialDefinitionData>> m_definitions;
    std::unordered_map<std::string, std::vector<MaterialDiagnostic>> m_diagnostics;
    std::unordered_map<std::string,std::vector<std::string>> m_dependencyAttempts;
    std::vector<std::string> m_resolutionTrace;
    std::unordered_map<std::string, std::shared_ptr<const MaterialRenderProxy>> m_proxies;
    std::unordered_map<const MaterialDefinitionData *, std::shared_ptr<const CompiledMaterial>> m_compiledDefinitions;
    std::unordered_map<std::string, std::weak_ptr<const CompiledMaterial>> m_compiled;
    std::unordered_map<std::string,std::string> m_candidateSources;
    std::unordered_map<std::string,std::map<std::string,MaterialParameterType>> m_activeOverrideTypes;
    std::vector<std::weak_ptr<DynamicMaterialInstance>> m_dynamicInstances;
    bool m_allowInactive=false;
    struct TextureSnapshot { std::shared_ptr<TextureResource> resource; std::vector<std::uint8_t> source; std::uint64_t version=0; };
    std::unordered_map<std::string,TextureSnapshot> m_textureSnapshots;
    std::uint64_t m_nextTextureVersion=1;
    bool m_refreshTextures=false;
    std::uint64_t m_nextGeneration = 1;
};
} // namespace MiniEngine
