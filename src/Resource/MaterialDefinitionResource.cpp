#include "MiniEngine/Resource/MaterialDefinitionResource.hpp"
#include "MiniEngine/Platform/Platform.hpp"

namespace MiniEngine
{
bool MaterialDefinitionResource::Load(const std::string &path)
{
    const bool hadValidData = IsReady();
    MaterialDefinitionData candidate;
    std::string error;
    if (!candidate.graph.Parse(PlatformFile::ReadAllText(std::filesystem::u8path(path)), &error))
    { SetState(hadValidData ? ResourceState::Loaded : ResourceState::Failed, error); return false; }
    candidate.parameters = candidate.graph.GetParameters();
    candidate.renderState = candidate.graph.GetRenderState();
    candidate.sourcePath = path;
    MarkLoading(path);
    m_data = std::move(candidate);
    SetState(ResourceState::Loaded);
    return true;
}
void MaterialDefinitionResource::Unload() { m_data = {}; SetState(ResourceState::Unloaded); }
std::size_t MaterialDefinitionResource::GetByteSize() const { return sizeof(*this) + m_data.graph.Serialize().size(); }
MaterialDefinitionData MakeBuiltinPbrDefinition()
{
    MaterialDefinitionData definition;
    definition.sourcePath = "builtin:PBR";
    const auto number = [&](const std::string &name, MaterialParameterValue value) {
        MaterialParameterDecl p;
        p.name = p.displayName = name;
        p.type = GetMaterialParameterType(value);
        p.defaultValue = std::move(value);
        definition.parameters.push_back(std::move(p));
    };
    number("BaseColor", Vec4{.3f, .7f, 1, 1});
    number("Metallic", 0.f); number("Roughness", .7f); number("Emissive", Vec3{});
    number("AO", 1.f); number("Opacity", 1.f); number("PackedEnabled", 0.f); number("PackedLayout", 0.f);
    const auto texture = [&](const std::string &name, TextureSampling sampling) {
        MaterialParameterDecl p;
        p.name = p.displayName = name; p.type = MaterialParameterType::Texture2D;
        p.defaultValue = std::string{}; p.sampling = sampling;
        definition.parameters.push_back(std::move(p));
    };
    texture("BaseMap", TextureSampling::SRgb); texture("NormalMap", TextureSampling::Normal);
    texture("MetallicMap", TextureSampling::Linear); texture("RoughnessMap", TextureSampling::Linear);
    texture("AOMap", TextureSampling::Linear); texture("EmissiveMap", TextureSampling::SRgb); texture("PackedMap", TextureSampling::Linear);
    definition.graph.SetFormatVersion(2);
    definition.graph.AddNode(MaterialNodeType::SurfaceOutput, "Surface");
    definition.graph.GetParameters() = definition.parameters;
    return definition;
}
} // namespace MiniEngine
