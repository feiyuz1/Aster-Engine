#pragma once
#include "material_document.h"
namespace tinyengine
{
class MaterialAssetEditor : public MaterialDocument<MaterialAssetData>
{
  public:
    bool Open(const std::string& path, const MaterialAssetData& data,
              const std::vector<MaterialParameterDecl>& parameters);
    bool setOverride(const std::string& name, const MaterialParameterValue& value);
    bool resetOverride(const std::string& name);
    std::vector<std::string> GetRetainedAssetReferences() const;
    void setSchema(std::vector<MaterialParameterDecl> parameters)
    {
        m_parameters = std::move(parameters);
    }
    static bool createInstance(const std::string& path, const std::string& parent, std::string& error);
    void draw();

  private:
    std::vector<MaterialParameterDecl> m_parameters;
};
} // namespace tinyengine
