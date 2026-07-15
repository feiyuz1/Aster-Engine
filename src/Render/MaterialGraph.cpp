#include "MiniEngine/Render/MaterialGraph.hpp"

#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace MiniEngine
{
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
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string signature, token;
    int version = 0;
    if (!(input >> signature >> version) || signature != "TINY_MATGRAPH" || version != 1)
        return false;
    m_nodes.clear();
    m_links.clear();
    m_nextId = 1;
    while (input >> token)
        if (token == "node")
        {
            MaterialNode node;
            int type = 0;
            input >> node.id >> type >> std::quoted(node.name) >> node.position.x >> node.position.y >> node.value.x >> node.value.y >>
                node.value.z >> node.value.w >> std::quoted(node.texturePath);
            node.type = static_cast<MaterialNodeType>(type);
            m_nextId = std::max(m_nextId, node.id + 1);
            m_nodes.push_back(std::move(node));
        }
        else if (token == "link")
        {
            MaterialLink link;
            input >> link.fromNode >> link.toNode >> link.toInput;
            m_links.push_back(link);
        }
    return true;
}
bool MaterialGraph::Save(const std::filesystem::path &path) const
{
    std::ostringstream output;
    output << "TINY_MATGRAPH 1\n";
    for (const auto &node : m_nodes)
        output << "node " << node.id << ' ' << static_cast<int>(node.type) << ' ' << std::quoted(node.name) << ' ' << node.position.x << ' '
               << node.position.y << ' ' << node.value.x << ' ' << node.value.y << ' ' << node.value.z << ' ' << node.value.w << ' '
               << std::quoted(node.texturePath) << '\n';
    for (const auto &link : m_links)
        output << "link " << link.fromNode << ' ' << link.toNode << ' ' << link.toInput << '\n';
    return PlatformFile::WriteAllText(path, output.str());
}
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
    std::function<std::string(std::uint32_t)> expression = [&](const std::uint32_t id) -> std::string {
        const auto found = nodes.find(id);
        if (found == nodes.end() || !visiting.insert(id).second)
            return "vec4(1.0, 0.0, 1.0, 1.0)";
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
} // namespace MiniEngine
