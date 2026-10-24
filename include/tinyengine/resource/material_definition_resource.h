#pragma once
#include "tinyengine/render/material_graph.h"
#include "tinyengine/resource/resource.h"

namespace tinyengine
{
struct MaterialDefinitionData
{
    MaterialGraph graph;
    std::vector<MaterialParameterDecl> parameters;
    MaterialRenderState renderState;
    std::string sourcePath;
};
class MaterialDefinitionResource final : public Resource
{
  public:
    bool load(const std::string& path) override;
    void Unload() override;
    std::size_t getByteSize() const override;
    const MaterialDefinitionData& GetData() const
    {
        return m_data;
    }

  private:
    MaterialDefinitionData m_data;
};
MaterialDefinitionData makeBuiltinPbrDefinition();
MaterialDefinitionData makeEditablePbrDefinition();
} // namespace tinyengine
