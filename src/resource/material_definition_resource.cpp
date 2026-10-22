#include "tinyengine/resource/material_definition_resource.h"
#include "tinyengine/platform/platform.h"

namespace tinyengine
{
bool MaterialDefinitionResource::load(const std::string& path)
{
    const bool hadValidData = isReady();
    MaterialDefinitionData candidate;
    std::string error;
    if (!candidate.graph.parse(PlatformFile::readAllText(fromUtf8Path(path)), &error))
    {
        setState(hadValidData ? ResourceState::Loaded : ResourceState::Failed, error);
        return false;
    }
    if (!candidate.graph.upgradeLegacy())
    {
        setState(hadValidData ? ResourceState::Loaded : ResourceState::Failed, "Invalid legacy material graph");
        return false;
    }
    candidate.parameters = candidate.graph.GetParameters();
    candidate.renderState = candidate.graph.GetRenderState();
    candidate.sourcePath = path;
    markLoading(path);
    m_data = std::move(candidate);
    setState(ResourceState::Loaded);
    return true;
}
void MaterialDefinitionResource::Unload()
{
    m_data = {};
    setState(ResourceState::Unloaded);
}
std::size_t MaterialDefinitionResource::getByteSize() const
{
    return sizeof(*this) + m_data.graph.serialize().size();
}
MaterialDefinitionData makeBuiltinPbrDefinition()
{
    MaterialDefinitionData definition;
    definition.sourcePath = "builtin:PBR";
    const auto number = [&](const std::string& name, MaterialParameterValue value)
    {
        MaterialParameterDecl p;
        p.name = p.displayName = name;
        p.type = getMaterialParameterType(value);
        p.defaultValue = std::move(value);
        definition.parameters.push_back(std::move(p));
    };
    number("BaseColor", Vec4{.3f, .7f, 1, 1});
    number("Metallic", 0.f);
    number("Roughness", .7f);
    number("Emissive", Vec3{});
    number("AO", 1.f);
    number("Opacity", 1.f);
    number("PackedEnabled", 0.f);
    number("PackedLayout", 0.f);
    number("MetallicMapEnabled", 0.f);
    number("RoughnessMapEnabled", 0.f);
    number("AOMapEnabled", 0.f);
    const auto texture = [&](const std::string& name, TextureSampling sampling)
    {
        MaterialParameterDecl p;
        p.name = p.displayName = name;
        p.type = MaterialParameterType::Texture2D;
        p.defaultValue = std::string{};
        p.sampling = sampling;
        definition.parameters.push_back(std::move(p));
    };
    texture("BaseMap", TextureSampling::SRgb);
    texture("NormalMap", TextureSampling::Normal);
    texture("MetallicMap", TextureSampling::Linear);
    texture("RoughnessMap", TextureSampling::Linear);
    texture("AOMap", TextureSampling::Linear);
    texture("EmissiveMap", TextureSampling::SRgb);
    texture("PackedMap", TextureSampling::Linear);
    definition.graph.setFormatVersion(2);
    definition.graph.addNode(MaterialNodeType::SurfaceOutput, "Surface");
    definition.graph.GetParameters() = definition.parameters;
    return definition;
}
} // namespace tinyengine
