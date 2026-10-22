#pragma once
#include "tinyengine/resource/material_definition_resource.h"
namespace tinyengine
{
class MaterialCompiler
{
  public:
    MaterialCompileResult compile(const MaterialDefinitionData& definition) const;
    GeneratedShaderSources generatePass(const CompiledMaterial& material, MaterialPass pass, MaterialGeometry geometry,
                                        const MaterialTemplateSources& templates) const;
    bool writeCache(const CompiledMaterial&, const MaterialTemplateSources&, const std::filesystem::path& directory,
                    std::string* cacheKey = nullptr) const;
};
} // namespace tinyengine
