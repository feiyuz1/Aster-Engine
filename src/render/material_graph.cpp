// 文件：src/render/material_graph.cpp
// 作用：定义可视化材质节点图并生成可供着色器管线使用的源码。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：实现文件。

#include "tinyengine/render/material_graph.h"
#include "tinyengine/render/material_compiler.h"

#include "tinyengine/platform/platform.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace tinyengine
{
const char* materialNodeTypeName(MaterialNodeType type)
{
    static const char* kNames[] = {"Constant",        "Texture",         "Add",
                                   "Multiply",        "PbrOutput",       "SurfaceOutput",
                                   "ScalarParameter", "VectorParameter", "TextureParameter",
                                   "TextureSample",   "TexCoord",        "Lerp",
                                   "OneMinus",        "Clamp",           "ComponentMask",
                                   "NormalDecode"};
    const auto index = static_cast<std::size_t>(type);
    return index < 16 ? kNames[index] : "Invalid";
}
bool parseMaterialNodeType(const std::string& text, MaterialNodeType& type)
{
    for (int i = 0; i < 16; ++i)
        if (text == materialNodeTypeName(static_cast<MaterialNodeType>(i)))
        {
            type = static_cast<MaterialNodeType>(i);
            return true;
        }
    return false;
}
std::uint32_t MaterialGraph::addNode(const MaterialNodeType type, std::string name, const Vec2 position)
{
    const std::uint32_t id = m_nextId++;
    m_nodes.push_back({id, type, std::move(name), position});
    return id;
}
bool MaterialGraph::removeNode(const std::uint32_t id)
{
    const auto oldSize = m_nodes.size();
    m_nodes.erase(
        std::remove_if(m_nodes.begin(), m_nodes.end(), [id](const MaterialNode& node) { return node.id == id; }),
        m_nodes.end());
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                 [id](const MaterialLink& link) { return link.fromNode == id || link.toNode == id; }),
                  m_links.end());
    return m_nodes.size() != oldSize;
}
bool MaterialGraph::connect(const std::uint32_t fromNode, const std::uint32_t toNode, const std::uint32_t toInput)
{
    if (fromNode == toNode ||
        !std::any_of(m_nodes.begin(), m_nodes.end(), [fromNode](const auto& node) { return node.id == fromNode; }) ||
        !std::any_of(m_nodes.begin(), m_nodes.end(), [toNode](const auto& node) { return node.id == toNode; }))
        return false;
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                 [toNode, toInput](const MaterialLink& link)
                                 { return link.toNode == toNode && link.toInput == toInput; }),
                  m_links.end());
    m_links.push_back({fromNode, toNode, toInput});
    return true;
}
bool MaterialGraph::load(const std::filesystem::path& path)
{
    return parse(PlatformFile::readAllText(path));
}
bool MaterialGraph::parse(const std::string& source, std::string* error)
{
    const auto fail = [error](const std::string& message)
    {
        if (error)
            *error = message;
        return false;
    };
    std::istringstream input(source);
    std::string signature, kind;
    int version = 0;
    if (!(input >> signature >> version >> kind) || signature != "TINY_MAT" || version != 3 || kind != "graph")
        return fail("Unsupported material graph version");
    MaterialGraph candidate;
    candidate.m_formatVersion = version;
    std::string line;
    std::getline(input, line);
    if (line.find_first_not_of(" \t\r") != std::string::npos)
        return fail("Invalid graph header");
    bool haveSettings = false;
    std::unordered_set<std::string> parameterNames, metadataNames;
    struct Meta
    {
        std::string display, group;
        std::optional<Vec2> range;
    };
    std::map<std::string, Meta> metadata;
    std::size_t number = 1;
    while (std::getline(input, line))
    {
        ++number;
        std::istringstream fields(line);
        std::string token;
        if (!(fields >> token) || token[0] == '#')
            continue;
        bool valid = true;
        if (token == "settings" && version == 3)
        {
            std::string shading, blend;
            int sided = 0;
            valid = !haveSettings &&
                    static_cast<bool>(fields >> shading >> blend >> sided >> candidate.m_renderState.maskCutoff) &&
                    (shading == "Lit" || shading == "Unlit") &&
                    (blend == "Opaque" || blend == "Masked" || blend == "Alpha") && (sided == 0 || sided == 1) &&
                    std::isfinite(candidate.m_renderState.maskCutoff) && candidate.m_renderState.maskCutoff >= 0 &&
                    candidate.m_renderState.maskCutoff <= 1;
            candidate.m_renderState.shadingModel =
                shading == "Unlit" ? MaterialShadingModel::Unlit : MaterialShadingModel::Lit;
            candidate.m_renderState.blendMode = blend == "Masked"  ? MaterialBlendMode::Masked
                                                : blend == "Alpha" ? MaterialBlendMode::Alpha
                                                                   : MaterialBlendMode::Opaque;
            candidate.m_renderState.twoSided = sided != 0;
            haveSettings = true;
        }
        else if (token == "parameter" && version == 3)
        {
            MaterialParameterDecl declaration;
            std::string type, sampling;
            valid = static_cast<bool>(fields >> std::quoted(declaration.name) >> type >> sampling) &&
                    isMaterialParameterName(declaration.name) && parameterNames.insert(declaration.name).second &&
                    parseMaterialParameterType(type, declaration.type) &&
                    (sampling == "SRgb" || sampling == "Linear" || sampling == "Normal") &&
                    (declaration.type == MaterialParameterType::Texture2D || sampling == "Linear") &&
                    readMaterialValue(fields, declaration.type, declaration.defaultValue);
            declaration.sampling = sampling == "SRgb"     ? TextureSampling::SRgb
                                   : sampling == "Normal" ? TextureSampling::Normal
                                                          : TextureSampling::Linear;
            declaration.displayName = declaration.name;
            if (valid)
                candidate.m_parameters.push_back(std::move(declaration));
        }
        else if (token == "parameter_meta" && version == 3)
        {
            std::string name;
            Meta meta;
            int hasRange = 0;
            Vec2 range;
            valid = static_cast<bool>(fields >> std::quoted(name) >> std::quoted(meta.display) >>
                                      std::quoted(meta.group) >> hasRange >> range.x >> range.y) &&
                    metadataNames.insert(name).second && (hasRange == 0 || hasRange == 1) && std::isfinite(range.x) &&
                    std::isfinite(range.y) && (!hasRange || range.x <= range.y);
            if (hasRange)
                meta.range = range;
            metadata.emplace(name, std::move(meta));
        }
        else if (token == "node")
        {
            MaterialNode node;
            std::string type, constantType;
            valid =
                static_cast<bool>(fields >> node.id >> type >> std::quoted(node.name) >> node.position.x >>
                                  node.position.y >> constantType >> node.value.x >> node.value.y >> node.value.z >>
                                  node.value.w >> std::quoted(node.parameterName) >> std::quoted(node.channelMask)) &&
                parseMaterialNodeType(type, node.type) && parseMaterialParameterType(constantType, node.constantType) &&
                node.constantType != MaterialParameterType::Texture2D && node.type != MaterialNodeType::Texture;
            valid = valid && node.id != 0 && node.id != std::numeric_limits<std::uint32_t>::max() &&
                    std::isfinite(node.position.x) && std::isfinite(node.position.y) &&
                    isFiniteMaterialValue(node.value);
            candidate.m_nextId = std::max(candidate.m_nextId, node.id + 1);
            if (valid)
                candidate.m_nodes.push_back(std::move(node));
        }
        else if (token == "link")
        {
            MaterialLink link;
            valid = static_cast<bool>(fields >> link.fromNode >> link.fromOutput >> link.toNode >> link.toInput);
            if (valid)
                candidate.m_links.push_back(link);
        }
        else
            valid = false;
        std::string extra;
        if (!valid || ((fields >> extra) && extra[0] != '#'))
            return fail("Invalid graph record at line " + std::to_string(number));
    }
    if (version == 3 && !haveSettings)
        return fail("Graph requires exactly one settings record");
    for (const auto& entry : metadata)
    {
        const auto found = std::find_if(candidate.m_parameters.begin(), candidate.m_parameters.end(),
                                        [&](const auto& p) { return p.name == entry.first; });
        if (found == candidate.m_parameters.end())
            return fail("Metadata references undeclared parameter " + entry.first);
        found->displayName = entry.second.display;
        found->group = entry.second.group;
        found->range = entry.second.range;
    }
    *this = std::move(candidate);
    if (error)
        error->clear();
    return true;
}
std::string MaterialGraph::serialize() const
{
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<float>::max_digits10) << "TINY_MAT 3 graph\n";
    output << "settings " << (m_renderState.shadingModel == MaterialShadingModel::Unlit ? "Unlit" : "Lit") << ' '
           << (m_renderState.blendMode == MaterialBlendMode::Masked  ? "Masked"
               : m_renderState.blendMode == MaterialBlendMode::Alpha ? "Alpha"
                                                                     : "Opaque")
           << ' ' << m_renderState.twoSided << ' ' << m_renderState.maskCutoff << '\n';
    for (const auto& p : m_parameters)
    {
        output << "parameter " << std::quoted(p.name) << ' ' << materialParameterTypeName(p.type) << ' '
               << (p.sampling == TextureSampling::SRgb     ? "SRgb"
                   : p.sampling == TextureSampling::Normal ? "Normal"
                                                           : "Linear")
               << ' ';
        writeMaterialValue(output, p.defaultValue);
        output << "\nparameter_meta " << std::quoted(p.name) << ' ' << std::quoted(p.displayName) << ' '
               << std::quoted(p.group) << ' ' << (p.range ? 1 : 0) << ' ' << (p.range ? p.range->x : 0) << ' '
               << (p.range ? p.range->y : 1) << '\n';
    }
    for (const auto& node : m_nodes)
    {
        const auto value = node.type == MaterialNodeType::Constant ? node.value : Vec4{};
        output << "node " << node.id << ' ' << materialNodeTypeName(node.type) << ' ' << std::quoted(node.name) << ' '
               << node.position.x << ' ' << node.position.y << ' '
               << materialParameterTypeName(node.type == MaterialNodeType::Constant ? node.constantType
                                                                                    : MaterialParameterType::Float)
               << ' ' << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w << ' '
               << std::quoted(node.parameterName) << ' ' << std::quoted(node.channelMask) << '\n';
    }
    for (const auto& link : m_links)
        output << "link " << link.fromNode << ' ' << link.fromOutput << ' ' << link.toNode << ' ' << link.toInput
               << '\n';
    return output.str();
}
bool MaterialGraph::Save(const std::filesystem::path& path) const
{
    return m_formatVersion == 3 && PlatformFile::writeAllText(path, serialize());
}
// 从输出节点反向遍历依赖并检测环；只有可达节点参与生成，孤立编辑节点不会污染 Shader。
std::string MaterialGraph::compile(std::vector<std::string>* errors) const
{
    MaterialDefinitionData definition;
    definition.graph = *this;
    definition.parameters = m_parameters;
    definition.renderState = m_renderState;
    const auto result = MaterialCompiler{}.compile(definition);
    if (errors)
        for (const auto& diagnostic : result.diagnostics)
            errors->push_back(diagnostic.message);
    return result.Success()
               ? result.material->parameterSource + result.material->surfaceSource + result.material->opacitySource
               : std::string{};
}
std::vector<MaterialNode>& MaterialGraph::GetNodes()
{
    return m_nodes;
}
const std::vector<MaterialNode>& MaterialGraph::GetNodes() const
{
    return m_nodes;
}
std::vector<MaterialLink>& MaterialGraph::GetLinks()
{
    return m_links;
}
const std::vector<MaterialLink>& MaterialGraph::GetLinks() const
{
    return m_links;
}
} // namespace tinyengine
