#include "tinyengine/resource/material_definition_resource.h"
#include "tinyengine/platform/platform.h"
#include <algorithm>

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
    definition.graph.setFormatVersion(3);
    definition.graph.addNode(MaterialNodeType::SurfaceOutput, "Surface");
    definition.graph.GetParameters() = definition.parameters;
    return definition;
}
MaterialDefinitionData makeEditablePbrDefinition()
{
    auto definition = makeBuiltinPbrDefinition();
    definition.sourcePath.clear();
    auto& graph = definition.graph;
    graph.GetNodes().front().position = {1500, 400};
    const auto node = [&](MaterialNodeType type, const std::string& name, Vec2 position)
    { return graph.addNode(type, name, position); };
    const auto parameter = [&](const std::string& name, Vec2 position)
    {
        const auto declaration = std::find_if(definition.parameters.begin(), definition.parameters.end(),
                                              [&](const auto& p) { return p.name == name; });
        const auto type = declaration->type == MaterialParameterType::Texture2D ? MaterialNodeType::TextureParameter
                          : declaration->type == MaterialParameterType::Float   ? MaterialNodeType::ScalarParameter
                                                                                : MaterialNodeType::VectorParameter;
        const auto id = node(type, name, position);
        graph.GetNodes().back().parameterName = name;
        return id;
    };
    const auto mask = [&](std::uint32_t input, const std::string& channels, Vec2 position)
    {
        const auto id = node(MaterialNodeType::ComponentMask, channels, position);
        graph.GetNodes().back().channelMask = channels;
        graph.connect(input, id, 0);
        return id;
    };
    const auto sample = [&](const std::string& name, float y)
    {
        const auto texture = parameter(name, {0, y});
        const auto id = node(MaterialNodeType::TextureSample, name + " sample", {240, y});
        graph.connect(texture, id, 0);
        return id;
    };
    const auto binary =
        [&](MaterialNodeType type, const std::string& name, std::uint32_t a, std::uint32_t b, Vec2 position)
    {
        const auto id = node(type, name, position);
        graph.connect(a, id, 0);
        graph.connect(b, id, 1);
        return id;
    };
    const auto select =
        [&](const std::string& name, std::uint32_t a, std::uint32_t b, std::uint32_t amount, Vec2 position)
    {
        const auto id = binary(MaterialNodeType::Lerp, name, a, b, position);
        graph.connect(amount, id, 2);
        return id;
    };
    const auto color = parameter("BaseColor", {0, -300});
    const auto baseSample = sample("BaseMap", -100);
    const auto baseRgb = mask(baseSample, "xyz", {480, -100});
    const auto colorRgb = mask(color, "xyz", {480, -300});
    graph.connect(binary(MaterialNodeType::Multiply, "Base color", colorRgb, baseRgb, {1100, -200}), 1, 0);
    const auto alpha = binary(MaterialNodeType::Multiply, "Color alpha", mask(color, "w", {480, 80}),
                              mask(baseSample, "w", {480, 240}), {750, 100});
    graph.connect(binary(MaterialNodeType::Multiply, "Opacity", alpha, parameter("Opacity", {480, 400}), {1100, 100}),
                  1, 6);
    const auto packed = sample("PackedMap", 600);
    const auto layout = parameter("PackedLayout", {240, 800});
    const auto packedR = mask(packed, "x", {480, 600});
    const auto packedG = mask(packed, "y", {480, 800});
    const auto packedB = mask(packed, "z", {480, 1000});
    const auto packedRoughness = select("ORM / RMA roughness", packedG, packedR, layout, {750, 750});
    const auto packedAo = select("ORM / RMA AO", packedR, packedG, layout, {750, 1000});
    const std::string names[] = {"Metallic", "Roughness", "AO"};
    const std::string channels[] = {"z", "y", "x"};
    const std::uint32_t packedChannels[] = {packedB, packedRoughness, packedAo};
    const std::uint32_t ports[] = {1, 2, 4};
    for (int i = 0; i < 3; ++i)
    {
        const float y = 1250.f + i * 440.f;
        const auto sampled = mask(sample(names[i] + "Map", y), channels[i], {480, y});
        const auto enabled = parameter(names[i] + "MapEnabled", {480, y + 200});
        const auto selected = select(names[i] + " map", packedChannels[i], sampled, enabled, {750, y});
        const auto value = parameter(names[i], {750, y + 200});
        graph.connect(binary(MaterialNodeType::Multiply, names[i], value, selected, {1100, y}), 1, ports[i]);
    }
    const auto normal = mask(sample("NormalMap", 2700), "xyz", {480, 2700});
    const auto decoded = node(MaterialNodeType::NormalDecode, "Normal", {1100, 2700});
    graph.connect(normal, decoded, 0);
    graph.connect(decoded, 1, 3);
    const auto emissive = mask(sample("EmissiveMap", 3000), "xyz", {480, 3000});
    graph.connect(
        binary(MaterialNodeType::Multiply, "Emissive", parameter("Emissive", {480, 3200}), emissive, {1100, 3000}), 1,
        5);
    graph.GetParameters() = definition.parameters;
    return definition;
}
} // namespace tinyengine
