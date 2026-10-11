#pragma once
#include "MiniEngine/Render/MaterialGraph.hpp"
#include "MiniEngine/Resource/Resource.hpp"

namespace MiniEngine
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
    bool Load(const std::string &path) override;
    void Unload() override;
    std::size_t GetByteSize() const override;
    const MaterialDefinitionData &GetData() const { return m_data; }
private:
    MaterialDefinitionData m_data;
};
MaterialDefinitionData MakeBuiltinPbrDefinition();
} // namespace MiniEngine
