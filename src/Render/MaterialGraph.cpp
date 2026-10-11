// 文件：src/Render/MaterialGraph.cpp
// 作用：定义可视化材质节点图并生成可供着色器管线使用的源码。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：实现文件。

#include "MiniEngine/Render/MaterialGraph.hpp"

#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace MiniEngine
{
const char *MaterialNodeTypeName(MaterialNodeType type)
{
    static const char *names[] = {"Constant", "Texture", "Add", "Multiply", "PbrOutput", "SurfaceOutput", "ScalarParameter",
        "VectorParameter", "TextureParameter", "TextureSample", "TexCoord", "Lerp", "OneMinus", "Clamp", "ComponentMask", "NormalDecode"};
    const auto index = static_cast<std::size_t>(type);
    return index < 16 ? names[index] : "Invalid";
}
bool ParseMaterialNodeType(const std::string &text, MaterialNodeType &type)
{
    for (int i = 0; i < 16; ++i) if (text == MaterialNodeTypeName(static_cast<MaterialNodeType>(i)))
    { type = static_cast<MaterialNodeType>(i); return true; }
    return false;
}
std::uint32_t MaterialGraph::AddNode(const MaterialNodeType type, std::string name, const Vec2 position)
{
    const std::uint32_t id = m_nextId++;
    m_nodes.push_back({id, type, std::move(name), position});
    return id;
}
bool MaterialGraph::RemoveNode(const std::uint32_t id)
{
    const auto oldSize = m_nodes.size();
    m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(), [id](const MaterialNode &node) { return node.id == id; }), m_nodes.end());
    m_links.erase(
        std::remove_if(m_links.begin(), m_links.end(), [id](const MaterialLink &link) { return link.fromNode == id || link.toNode == id; }),
        m_links.end());
    return m_nodes.size() != oldSize;
}
bool MaterialGraph::Connect(const std::uint32_t fromNode, const std::uint32_t toNode, const std::uint32_t toInput)
{
    if (fromNode == toNode || !std::any_of(m_nodes.begin(), m_nodes.end(), [fromNode](const auto &node) { return node.id == fromNode; }) ||
        !std::any_of(m_nodes.begin(), m_nodes.end(), [toNode](const auto &node) { return node.id == toNode; }))
        return false;
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                 [toNode, toInput](const MaterialLink &link) { return link.toNode == toNode && link.toInput == toInput; }),
                  m_links.end());
    m_links.push_back({fromNode, toNode, toInput});
    return true;
}
bool MaterialGraph::Load(const std::filesystem::path &path)
{
    return Parse(PlatformFile::ReadAllText(path));
}
bool MaterialGraph::Parse(const std::string &source, std::string *error)
{
    const auto fail = [error](const std::string &message) { if (error) *error = message; return false; };
    std::istringstream input(source);
    std::string signature;
    int version = 0;
    if (!(input >> signature >> version) || signature != "TINY_MATGRAPH" || (version != 1 && version != 2))
        return fail("Unsupported material graph version");
    MaterialGraph candidate;
    candidate.m_formatVersion = version;
    std::string line;
    std::getline(input, line);
    if (line.find_first_not_of(" \t\r") != std::string::npos) return fail("Invalid graph header");
    bool haveSettings = false;
    std::unordered_set<std::string> parameterNames, metadataNames;
    struct Meta { std::string display, group; std::optional<Vec2> range; };
    std::map<std::string, Meta> metadata;
    std::size_t number = 1;
    while (std::getline(input, line))
    {
        ++number;
        std::istringstream fields(line);
        std::string token;
        if (!(fields >> token) || token[0] == '#') continue;
        bool valid = true;
        if (token == "settings" && version == 2)
        {
            std::string shading, blend;
            int sided = 0;
            valid = !haveSettings && static_cast<bool>(fields >> shading >> blend >> sided >> candidate.m_renderState.maskCutoff) &&
                    (shading == "Lit" || shading == "Unlit") && (blend == "Opaque" || blend == "Masked" || blend == "Alpha") &&
                    (sided == 0 || sided == 1) && std::isfinite(candidate.m_renderState.maskCutoff) &&
                    candidate.m_renderState.maskCutoff >= 0 && candidate.m_renderState.maskCutoff <= 1;
            candidate.m_renderState.shadingModel = shading == "Unlit" ? MaterialShadingModel::Unlit : MaterialShadingModel::Lit;
            candidate.m_renderState.blendMode = blend == "Masked" ? MaterialBlendMode::Masked :
                blend == "Alpha" ? MaterialBlendMode::Alpha : MaterialBlendMode::Opaque;
            candidate.m_renderState.twoSided = sided != 0;
            haveSettings = true;
        }
        else if (token == "parameter" && version == 2)
        {
            MaterialParameterDecl declaration;
            std::string type, sampling;
            valid = static_cast<bool>(fields >> std::quoted(declaration.name) >> type >> sampling) &&
                    IsMaterialParameterName(declaration.name) && parameterNames.insert(declaration.name).second &&
                    ParseMaterialParameterType(type, declaration.type) &&
                    (sampling == "SRgb" || sampling == "Linear" || sampling == "Normal") &&
                    (declaration.type == MaterialParameterType::Texture2D || sampling == "Linear") &&
                    ReadMaterialValue(fields, declaration.type, declaration.defaultValue);
            declaration.sampling = sampling == "SRgb" ? TextureSampling::SRgb : sampling == "Normal" ? TextureSampling::Normal : TextureSampling::Linear;
            declaration.displayName = declaration.name;
            if (valid) candidate.m_parameters.push_back(std::move(declaration));
        }
        else if (token == "parameter_meta" && version == 2)
        {
            std::string name;
            Meta meta;
            int hasRange = 0;
            Vec2 range;
            valid = static_cast<bool>(fields >> std::quoted(name) >> std::quoted(meta.display) >> std::quoted(meta.group) >>
                                      hasRange >> range.x >> range.y) && metadataNames.insert(name).second &&
                    (hasRange == 0 || hasRange == 1) && std::isfinite(range.x) && std::isfinite(range.y) &&
                    (!hasRange || range.x <= range.y);
            if (hasRange) meta.range = range;
            metadata.emplace(name, std::move(meta));
        }
        else if (token == "node")
        {
            MaterialNode node;
            if (version == 1)
            {
                int type = 0;
                valid = static_cast<bool>(fields >> node.id >> type >> std::quoted(node.name) >> node.position.x >> node.position.y >>
                                          node.value.x >> node.value.y >> node.value.z >> node.value.w >> std::quoted(node.texturePath)) && type >= 0 && type <= 4;
                node.type = static_cast<MaterialNodeType>(type);
            }
            else
            {
                std::string type, constantType;
                valid = static_cast<bool>(fields >> node.id >> type >> std::quoted(node.name) >> node.position.x >> node.position.y >>
                                          constantType >> node.value.x >> node.value.y >> node.value.z >> node.value.w >>
                                          std::quoted(node.parameterName) >> std::quoted(node.channelMask)) &&
                        ParseMaterialNodeType(type, node.type) && ParseMaterialParameterType(constantType, node.constantType) &&
                        node.constantType != MaterialParameterType::Texture2D && node.type != MaterialNodeType::Texture;
            }
            valid = valid && node.id != 0 && node.id != std::numeric_limits<std::uint32_t>::max() &&
                    std::isfinite(node.position.x) && std::isfinite(node.position.y) && IsFiniteMaterialValue(node.value);
            candidate.m_nextId = std::max(candidate.m_nextId, node.id + 1);
            if (valid) candidate.m_nodes.push_back(std::move(node));
        }
        else if (token == "link")
        {
            MaterialLink link;
            if (version == 1) valid = static_cast<bool>(fields >> link.fromNode >> link.toNode >> link.toInput);
            else valid = static_cast<bool>(fields >> link.fromNode >> link.fromOutput >> link.toNode >> link.toInput);
            if (valid) candidate.m_links.push_back(link);
        }
        else valid = false;
        std::string extra;
        if (!valid || ((fields >> extra) && extra[0] != '#')) return fail("Invalid graph record at line " + std::to_string(number));
    }
    if (version == 2 && !haveSettings) return fail("Graph requires exactly one settings record");
    for (const auto &entry : metadata)
    {
        const auto found = std::find_if(candidate.m_parameters.begin(), candidate.m_parameters.end(), [&](const auto &p) { return p.name == entry.first; });
        if (found == candidate.m_parameters.end()) return fail("Metadata references undeclared parameter " + entry.first);
        found->displayName = entry.second.display;
        found->group = entry.second.group;
        found->range = entry.second.range;
    }
    *this = std::move(candidate);
    if (error) error->clear();
    return true;
}
std::string MaterialGraph::Serialize() const
{
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<float>::max_digits10) << "TINY_MATGRAPH " << m_formatVersion << '\n';
    if (m_formatVersion == 2)
    {
        output << "settings " << (m_renderState.shadingModel == MaterialShadingModel::Unlit ? "Unlit" : "Lit") << ' ' <<
            (m_renderState.blendMode == MaterialBlendMode::Masked ? "Masked" : m_renderState.blendMode == MaterialBlendMode::Alpha ? "Alpha" : "Opaque") << ' ' <<
            (m_renderState.twoSided ? 1 : 0) << ' ' << m_renderState.maskCutoff << '\n';
        for (const auto &p : m_parameters)
        {
            output << "parameter " << std::quoted(p.name) << ' ' << MaterialParameterTypeName(p.type) << ' ' <<
                (p.sampling == TextureSampling::SRgb ? "SRgb" : p.sampling == TextureSampling::Normal ? "Normal" : "Linear") << ' ';
            WriteMaterialValue(output, p.defaultValue);
            output << "\nparameter_meta " << std::quoted(p.name) << ' ' << std::quoted(p.displayName) << ' ' << std::quoted(p.group) << ' ' <<
                (p.range ? 1 : 0) << ' ' << (p.range ? p.range->x : 0) << ' ' << (p.range ? p.range->y : 1) << '\n';
        }
    }
    for (const auto &node : m_nodes)
    {
        output << "node " << node.id << ' ';
        if (m_formatVersion == 1) output << static_cast<int>(node.type);
        else output << MaterialNodeTypeName(node.type);
        output << ' ' << std::quoted(node.name) << ' ' << node.position.x << ' ' << node.position.y << ' ';
        if (m_formatVersion == 2) output << MaterialParameterTypeName(node.type == MaterialNodeType::Constant ? node.constantType : MaterialParameterType::Float) << ' ';
        const Vec4 value = m_formatVersion == 1 || node.type == MaterialNodeType::Constant ? node.value : Vec4{};
        output << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w << ' ';
        if (m_formatVersion == 1) output << std::quoted(node.texturePath);
        else output << std::quoted(node.parameterName) << ' ' << std::quoted(node.channelMask);
        output << '\n';
    }
    for (const auto &link : m_links)
    {
        output << "link " << link.fromNode << ' ';
        if (m_formatVersion == 2) output << link.fromOutput << ' ';
        output << link.toNode << ' ' << link.toInput << '\n';
    }
    return output.str();
}
bool MaterialGraph::Save(const std::filesystem::path &path) const
{
    return PlatformFile::WriteAllText(path, Serialize());
}
// 从输出节点反向遍历依赖并检测环；只有可达节点参与生成，孤立编辑节点不会污染 Shader。
std::string MaterialGraph::Compile(std::vector<std::string> *errors) const
{
    const auto output =
        std::find_if(m_nodes.begin(), m_nodes.end(), [](const MaterialNode &node) { return node.type == MaterialNodeType::PbrOutput; });
    if (output == m_nodes.end())
    {
        if (errors)
            errors->push_back("Material graph has no PBR Output node");
        return {};
    }
    std::unordered_map<std::uint32_t, const MaterialNode *> nodes;
    for (const auto &node : m_nodes)
        nodes[node.id] = &node;
    std::unordered_set<std::uint32_t> visiting;
    std::unordered_set<std::uint32_t> cycleNodes;
    std::function<std::string(std::uint32_t)> expression = [&](const std::uint32_t id) -> std::string {
        const auto found = nodes.find(id);
        if (found == nodes.end())
            return "vec4(1.0, 0.0, 1.0, 1.0)";
        if (!visiting.insert(id).second)
        {
            cycleNodes.insert(id);
            return "vec4(1.0, 0.0, 1.0, 1.0)";
        }
        const MaterialNode &node = *found->second;
        std::string result;
        if (node.type == MaterialNodeType::Constant)
        {
            std::ostringstream value;
            value << "vec4(" << node.value.x << ',' << node.value.y << ',' << node.value.z << ',' << node.value.w << ')';
            result = value.str();
        }
        else if (node.type == MaterialNodeType::Texture)
            result = "texture(uGraphTexture" + std::to_string(node.id) + ", vUv)";
        else
        {
            std::string inputs[2] = {"vec4(0.0)", "vec4(0.0)"};
            for (const auto &link : m_links)
                if (link.toNode == id && link.toInput < 2)
                    inputs[link.toInput] = expression(link.fromNode);
            result = '(' + inputs[0] + (node.type == MaterialNodeType::Multiply ? " * " : " + ") + inputs[1] + ')';
        }
        visiting.erase(id);
        return result;
    };
    std::string channels[4] = {"vec4(0.8,0.8,0.8,1.0)", "vec4(0.0)", "vec4(0.7)", "vec4(0.0)"};
    for (const auto &link : m_links)
        if (link.toNode == output->id && link.toInput < 4)
            channels[link.toInput] = expression(link.fromNode);
    if (!cycleNodes.empty())
    {
        if (errors)
        {
            std::ostringstream diagnostic;
            diagnostic << "Material graph cycle detected at node" << (cycleNodes.size() == 1 ? " " : "s ");
            bool first = true;
            for (const auto id : cycleNodes)
            {
                diagnostic << (first ? "" : ", ") << id;
                first = false;
            }
            errors->push_back(diagnostic.str());
        }
        return {};
    }
    std::ostringstream shader;
    shader << "#version 330 core\nin vec3 vNormal; in vec3 vWorldPosition; in vec2 vUv; out vec4 outColor;\n";
    for (const auto &node : m_nodes)
        if (node.type == MaterialNodeType::Texture)
            shader << "uniform sampler2D uGraphTexture" << node.id << ";\n";
    shader << "uniform vec3 uCameraPosition;\nvoid main(){ vec4 base=" << channels[0] << "; float metallic=clamp((" << channels[1]
           << ").x,0.0,1.0); float roughness=clamp((" << channels[2] << ").x,0.04,1.0); vec3 emissive=(" << channels[3]
           << ").rgb; vec3 n=normalize(vNormal); vec3 l=normalize(vec3(0.4,1.0,0.3)); float ndl=max(dot(n,l),0.0); vec3 "
              "diffuse=base.rgb*(0.08+ndl*(1.0-metallic)); vec3 v=normalize(uCameraPosition-vWorldPosition); vec3 h=normalize(v+l); float "
              "spec=pow(max(dot(n,h),0.0),mix(128.0,4.0,roughness)); outColor=vec4(diffuse+vec3(spec)+emissive,base.a);}\n";
    return shader.str();
}
std::vector<MaterialNode> &MaterialGraph::GetNodes()
{
    return m_nodes;
}
const std::vector<MaterialNode> &MaterialGraph::GetNodes() const
{
    return m_nodes;
}
std::vector<MaterialLink> &MaterialGraph::GetLinks()
{
    return m_links;
}
const std::vector<MaterialLink> &MaterialGraph::GetLinks() const
{
    return m_links;
}
} // 结束 MiniEngine 命名空间
