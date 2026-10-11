#pragma once
#include "MiniEngine/Resource/MaterialDefinitionResource.hpp"
namespace MiniEngine
{
class MaterialCompiler
{
public:
    MaterialCompileResult Compile(const MaterialDefinitionData &definition) const;
    GeneratedShaderSources GeneratePass(const CompiledMaterial &material, MaterialPass pass, MaterialGeometry geometry,
                                        const MaterialTemplateSources &templates) const;
    bool WriteCache(const CompiledMaterial &,const MaterialTemplateSources &,const std::filesystem::path &directory,std::string *cacheKey=nullptr) const;
};
} // namespace MiniEngine
