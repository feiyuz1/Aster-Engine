#pragma once
#include "MaterialDocument.hpp"
namespace MiniEngine
{
class MaterialAssetEditor : public MaterialDocument<MaterialAssetData>
{
  public:
    bool Open(const std::string &path, const MaterialAssetData &data, const std::vector<MaterialParameterDecl> &parameters);
    bool SetOverride(const std::string &name, const MaterialParameterValue &value);
    bool ResetOverride(const std::string &name);
    std::vector<std::string> GetRetainedAssetReferences() const;
    void SetSchema(std::vector<MaterialParameterDecl> parameters)
    {
        m_parameters = std::move(parameters);
    }
    static bool CreateInstance(const std::string &path, const std::string &parent, std::string &error);
    void Draw();

  private:
    std::vector<MaterialParameterDecl> m_parameters;
};
} // namespace MiniEngine
