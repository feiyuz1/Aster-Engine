#pragma once
#include "MaterialDocument.hpp"
#include "MiniEngine/Resource/MaterialDefinitionResource.hpp"
namespace MiniEngine
{
class MaterialGraphEditor : public MaterialDocument<MaterialDefinitionData>
{
public:
    bool Open(const std::string &path,const MaterialDefinitionData &data) { return OpenDocument(path,data); }
    std::uint32_t AddNode(MaterialNodeType type,const std::string &name);
    bool ConnectPorts(std::uint32_t from,std::uint32_t output,std::uint32_t to,std::uint32_t input);
    bool DisconnectPort(std::uint32_t to,std::uint32_t input);
    bool DeleteNode(std::uint32_t id);
    void Draw();
private:
    std::uint32_t m_selected=0,m_linkFrom=0;
    void DrawCanvas();
    void DrawProperties();
};
}
