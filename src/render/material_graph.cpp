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
    std::string signature;
    int version = 0;
    if (!(input >> signature >> version) || signature != "TINY_MATGRAPH" || (version != 1 && version != 2))
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
        if (token == "settings" && version == 2)
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
        else if (token == "parameter" && version == 2)
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
        else if (token == "parameter_meta" && version == 2)
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
            if (version == 1)
            {
                int type = 0;
                valid = static_cast<bool>(fields >> node.id >> type >> std::quoted(node.name) >> node.position.x >>
                                          node.position.y >> node.value.x >> node.value.y >> node.value.z >>
                                          node.value.w >> std::quoted(node.texturePath)) &&
                        type >= 0 && type <= 4;
                node.type = static_cast<MaterialNodeType>(type);
            }
            else
            {
                std::string type, constantType;
                valid = static_cast<bool>(fields >> node.id >> type >> std::quoted(node.name) >> node.position.x >>
                                          node.position.y >> constantType >> node.value.x >> node.value.y >>
                                          node.value.z >> node.value.w >> std::quoted(node.parameterName) >>
                                          std::quoted(node.channelMask)) &&
                        parseMaterialNodeType(type, node.type) &&
                        parseMaterialParameterType(constantType, node.constantType) &&
                        node.constantType != MaterialParameterType::Texture2D && node.type != MaterialNodeType::Texture;
            }
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
            if (version == 1)
                valid = static_cast<bool>(fields >> link.fromNode >> link.toNode >> link.toInput);
            else
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
    if (version == 2 && !haveSettings)
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
    output << std::setprecision(std::numeric_limits<float>::max_digits10) << "TINY_MATGRAPH " << m_formatVersion
           << '\n';
    if (m_formatVersion == 2)
    {
        output << "settings " << (m_renderState.shadingModel == MaterialShadingModel::Unlit ? "Unlit" : "Lit") << ' '
               << (m_renderState.blendMode == MaterialBlendMode::Masked  ? "Masked"
                   : m_renderState.blendMode == MaterialBlendMode::Alpha ? "Alpha"
                                                                         : "Opaque")
               << ' ' << (m_renderState.twoSided ? 1 : 0) << ' ' << m_renderState.maskCutoff << '\n';
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
    }
    for (const auto& node : m_nodes)
    {
        output << "node " << node.id << ' ';
        if (m_formatVersion == 1)
            output << static_cast<int>(node.type);
        else
            output << materialNodeTypeName(node.type);
        output << ' ' << std::quoted(node.name) << ' ' << node.position.x << ' ' << node.position.y << ' ';
        if (m_formatVersion == 2)
            output << materialParameterTypeName(node.type == MaterialNodeType::Constant ? node.constantType
                                                                                        : MaterialParameterType::Float)
                   << ' ';
        const Vec4 value = m_formatVersion == 1 || node.type == MaterialNodeType::Constant ? node.value : Vec4{};
        output << value.x << ' ' << value.y << ' ' << value.z << ' ' << value.w << ' ';
        if (m_formatVersion == 1)
            output << std::quoted(node.texturePath);
        else
            output << std::quoted(node.parameterName) << ' ' << std::quoted(node.channelMask);
        output << '\n';
    }
    for (const auto& link : m_links)
    {
        output << "link " << link.fromNode << ' ';
        if (m_formatVersion == 2)
            output << link.fromOutput << ' ';
        output << link.toNode << ' ' << link.toInput << '\n';
    }
    return output.str();
}
bool MaterialGraph::Save(const std::filesystem::path& path) const
{
    auto graph = *this;
    if (!graph.upgradeLegacy())
        return false;
    return PlatformFile::writeAllText(path, graph.serialize());
}
bool MaterialGraph::upgradeLegacy()
{
    if (m_formatVersion == 2)
        return true;
    if (m_formatVersion != 1)
        return false;
    for (const auto& node : m_nodes)
        m_nextId = std::max(m_nextId, node.id + 1);
    for (auto& node : m_nodes)
    {
        if (node.type == MaterialNodeType::PbrOutput)
            node.type = MaterialNodeType::SurfaceOutput;
        if (node.type == MaterialNodeType::Constant)
            node.constantType = MaterialParameterType::Vec4;
    }
    const auto oldNodes = m_nodes;
    for (const auto& node : oldNodes)
        if (node.type == MaterialNodeType::Add || node.type == MaterialNodeType::Multiply)
        {
            std::uint32_t zero = 0;
            for (std::uint32_t port = 0; port < 2; ++port)
                if (std::none_of(m_links.begin(), m_links.end(),
                                 [&](const auto& link) { return link.toNode == node.id && link.toInput == port; }))
                {
                    if (!zero)
                    {
                        zero = addNode(MaterialNodeType::Constant, "Legacy zero");
                        m_nodes.back().value = {};
                    }
                    m_links.push_back({zero, node.id, port, 0});
                }
        }
    for (const auto& node : oldNodes)
        if (node.type == MaterialNodeType::Texture)
        {
            auto name = "LegacyTexture_" + std::to_string(node.id);
            while (std::any_of(m_parameters.begin(), m_parameters.end(), [&](const auto& p) { return p.name == name; }))
                name += '_';
            MaterialParameterDecl p;
            p.name = p.displayName = name;
            p.type = MaterialParameterType::Texture2D;
            p.defaultValue = node.texturePath;
            p.sampling = TextureSampling::SRgb;
            m_parameters.push_back(std::move(p));
            const auto parameterId = addNode(MaterialNodeType::TextureParameter, name, node.position);
            m_nodes.back().parameterName = name;
            for (auto& n : m_nodes)
                if (n.id == node.id)
                {
                    n.type = MaterialNodeType::TextureSample;
                    n.texturePath.clear();
                }
            m_links.push_back({parameterId, node.id, 0, 0});
        }
    const auto oldLinks = m_links;
    m_links.clear();
    for (const auto& link : oldLinks)
    {
        const auto target =
            std::find_if(m_nodes.begin(), m_nodes.end(), [&](const auto& n) { return n.id == link.toNode; });
        if (target == m_nodes.end() || target->type != MaterialNodeType::SurfaceOutput)
        {
            m_links.push_back(link);
            continue;
        }
        if (link.toInput >= 4)
            return false;
        const auto mask = addNode(MaterialNodeType::ComponentMask, "Legacy channel");
        m_nodes.back().channelMask = link.toInput == 0 || link.toInput == 3 ? "xyz" : "x";
        m_links.push_back({link.fromNode, mask, 0, link.fromOutput});
        m_links.push_back({mask, link.toNode, link.toInput == 3 ? 5u : link.toInput, 0});
        if (link.toInput == 0)
        {
            const auto alpha = addNode(MaterialNodeType::ComponentMask, "Legacy alpha");
            m_nodes.back().channelMask = "w";
            m_links.push_back({link.fromNode, alpha, 0, link.fromOutput});
            m_links.push_back({alpha, link.toNode, 6, 0});
        }
    }
    m_formatVersion = 2;
    return true;
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
