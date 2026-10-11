#pragma once
#include "MiniEngine/Resource/MaterialDefinitionResource.hpp"
#include "MiniEngine/Resource/ResourceManager.hpp"
#include <memory>
#include <unordered_map>

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
class MaterialSystem
{
public:
    explicit MaterialSystem(ResourceManager &resources);
    std::shared_ptr<const ResolvedMaterial> Resolve(const std::string &assetPath);
    const std::vector<MaterialDiagnostic> &GetDiagnostics(const std::string &assetPath) const;
private:
    std::shared_ptr<ResolvedMaterial> ResolveRecursive(const std::string &path, std::vector<std::string> &chain,
                                                     std::vector<MaterialDiagnostic> &diagnostics);
    std::shared_ptr<const MaterialDefinitionData> LoadDefinition(const std::string &path, std::vector<MaterialDiagnostic> &diagnostics);
    ResourceManager &m_resources;
    std::unordered_map<std::string, std::shared_ptr<const ResolvedMaterial>> m_resolved;
    std::unordered_map<std::string, std::shared_ptr<const MaterialDefinitionData>> m_definitions;
    std::unordered_map<std::string, std::vector<MaterialDiagnostic>> m_diagnostics;
    std::uint64_t m_nextGeneration = 1;
};
} // namespace MiniEngine
