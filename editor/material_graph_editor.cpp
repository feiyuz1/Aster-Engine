#include "material_graph_editor.h"
#include "tinyengine/render/material_compiler.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

namespace tinyengine
{
namespace
{
struct NodeEntry
{
    MaterialNodeType type;
    const char* english;
    const char* chinese;
    int category;
};
constexpr NodeEntry kNodes[] = {{MaterialNodeType::SurfaceOutput, "Surface output", "表面输出", 0},
                                {MaterialNodeType::Constant, "Constant", "常量", 1},
                                {MaterialNodeType::ScalarParameter, "Scalar parameter", "标量参数", 1},
                                {MaterialNodeType::VectorParameter, "Vector parameter", "向量参数", 1},
                                {MaterialNodeType::TextureParameter, "Texture parameter", "纹理参数", 1},
                                {MaterialNodeType::TextureSample, "Texture sample", "纹理采样", 2},
                                {MaterialNodeType::TexCoord, "Texture coordinates", "纹理坐标", 2},
                                {MaterialNodeType::NormalDecode, "Normal decode", "法线解码", 2},
                                {MaterialNodeType::Add, "Add", "相加", 3},
                                {MaterialNodeType::Multiply, "Multiply", "相乘", 3},
                                {MaterialNodeType::Lerp, "Lerp", "线性插值", 3},
                                {MaterialNodeType::OneMinus, "One minus", "一减", 3},
                                {MaterialNodeType::Clamp, "Clamp", "限制范围", 3},
                                {MaterialNodeType::ComponentMask, "Component mask", "分量遮罩", 3}};
const char* nodeName(MaterialNodeType type, bool chinese)
{
    for (const auto& entry : kNodes)
        if (entry.type == type)
            return chinese ? entry.chinese : entry.english;
    return type == MaterialNodeType::PbrOutput ? (chinese ? "表面输出" : "Surface output") : materialNodeTypeName(type);
}
std::string nodeTitle(const MaterialNode& node, bool chinese)
{
    if (node.name.empty() || node.name == materialNodeTypeName(node.type) || node.name == nodeName(node.type, false) ||
        ((node.type == MaterialNodeType::SurfaceOutput || node.type == MaterialNodeType::PbrOutput) &&
         node.name == "Surface"))
        return nodeName(node.type, chinese);
    return node.name;
}
bool isOutput(MaterialNodeType type)
{
    return type == MaterialNodeType::SurfaceOutput || type == MaterialNodeType::PbrOutput;
}
std::vector<const char*> Inputs(MaterialNodeType type, bool chinese)
{
    switch (type)
    {
    case MaterialNodeType::PbrOutput:
    case MaterialNodeType::SurfaceOutput:
        return chinese ? std::vector<const char*>{"基础颜色（三维）", "金属度（标量）",   "粗糙度（标量）",
                                                  "切线法线（三维）", "环境遮蔽（标量）", "自发光（三维）",
                                                  "不透明度（标量）"}
                       : std::vector<const char*>{"Base color (Vec3)", "Metallic (Float)", "Roughness (Float)",
                                                  "Normal TS (Vec3)",  "AO (Float)",       "Emissive (Vec3)",
                                                  "Opacity (Float)"};
    case MaterialNodeType::Add:
    case MaterialNodeType::Multiply:
        return {"A", "B"};
    case MaterialNodeType::Lerp:
        return chinese ? std::vector<const char*>{"A", "B", "插值系数（标量）"}
                       : std::vector<const char*>{"A", "B", "Alpha (Float)"};
    case MaterialNodeType::Clamp:
        return chinese ? std::vector<const char*>{"数值", "最小值（标量）", "最大值（标量）"}
                       : std::vector<const char*>{"Value", "Min (Float)", "Max (Float)"};
    case MaterialNodeType::TextureSample:
        return chinese ? std::vector<const char*>{"二维纹理", "UV（二维）"}
                       : std::vector<const char*>{"Texture2D", "UV (Vec2)"};
    case MaterialNodeType::ComponentMask:
        return {chinese ? "输入向量" : "Vector"};
    case MaterialNodeType::NormalDecode:
        return {chinese ? "RGB（三维）" : "RGB (Vec3)"};
    case MaterialNodeType::OneMinus:
        return {chinese ? "数值" : "Value"};
    default:
        return {};
    }
}
const char* typeName(MaterialParameterType type, bool chinese)
{
    static const char* kNames[] = {"标量", "二维向量", "三维向量", "四维向量", "二维纹理"};
    return chinese ? kNames[std::clamp(static_cast<int>(type), 0, 4)] : materialParameterTypeName(type);
}
const MaterialNode* findNode(const MaterialDefinitionData& data, std::uint32_t id)
{
    const auto& nodes = data.graph.GetNodes();
    const auto node = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) { return n.id == id; });
    return node == nodes.end() ? nullptr : &*node;
}
MaterialParameterType outputType(const MaterialDefinitionData& data, std::uint32_t id, std::set<std::uint32_t>& visited)
{
    const auto* node = findNode(data, id);
    if (!node || !visited.insert(id).second)
        return MaterialParameterType::Float;
    switch (node->type)
    {
    case MaterialNodeType::Constant:
        return node->constantType;
    case MaterialNodeType::TextureParameter:
        return MaterialParameterType::Texture2D;
    case MaterialNodeType::Texture:
    case MaterialNodeType::TextureSample:
        return MaterialParameterType::Vec4;
    case MaterialNodeType::TexCoord:
        return MaterialParameterType::Vec2;
    case MaterialNodeType::NormalDecode:
        return MaterialParameterType::Vec3;
    case MaterialNodeType::ComponentMask:
        return static_cast<MaterialParameterType>(std::clamp(static_cast<int>(node->channelMask.size()), 1, 4) - 1);
    case MaterialNodeType::ScalarParameter:
    case MaterialNodeType::VectorParameter:
        for (const auto& p : data.parameters)
            if (p.name == node->parameterName)
                return p.type;
        return node->type == MaterialNodeType::ScalarParameter ? MaterialParameterType::Float
                                                               : MaterialParameterType::Vec3;
    default:
        auto result = MaterialParameterType::Float;
        for (const auto& link : data.graph.GetLinks())
            if (link.toNode == id && (link.toInput == 0 || ((node->type == MaterialNodeType::Add ||
                                                             node->type == MaterialNodeType::Multiply ||
                                                             node->type == MaterialNodeType::Lerp) &&
                                                            link.toInput == 1)))
            {
                auto branch = visited;
                const auto type = outputType(data, link.fromNode, branch);
                if (static_cast<int>(type) > static_cast<int>(result))
                    result = type;
            }
        return result;
    }
}
MaterialParameterType outputType(const MaterialDefinitionData& data, std::uint32_t id)
{
    std::set<std::uint32_t> visited;
    return outputType(data, id, visited);
}
ImU32 typeColor(MaterialParameterType type)
{
    static constexpr ImU32 kColors[] = {IM_COL32(125, 220, 150, 255), IM_COL32(88, 209, 210, 255),
                                        IM_COL32(100, 175, 255, 255), IM_COL32(209, 136, 241, 255),
                                        IM_COL32(245, 198, 98, 255)};
    return kColors[std::clamp(static_cast<int>(type), 0, 4)];
}
MaterialParameterType inputType(const MaterialDefinitionData& data, const MaterialNode& node, std::uint32_t port)
{
    if (isOutput(node.type))
        return port == 0 || port == 3 || port == 5 ? MaterialParameterType::Vec3 : MaterialParameterType::Float;
    if (node.type == MaterialNodeType::TextureSample)
        return port == 0 ? MaterialParameterType::Texture2D : MaterialParameterType::Vec2;
    if (node.type == MaterialNodeType::NormalDecode)
        return MaterialParameterType::Vec3;
    if ((node.type == MaterialNodeType::Lerp && port == 2) || (node.type == MaterialNodeType::Clamp && port > 0))
        return MaterialParameterType::Float;
    return outputType(data, node.id);
}
// The compiler emits reachable expressions. The authoring UI also validates
// disconnected links, so a red port never becomes a silently accepted draft.
MaterialCompileResult compileForEditor(const MaterialDefinitionData& data)
{
    auto result = MaterialCompiler{}.compile(data);
    const auto error = [&](std::uint32_t node, int port, const std::string& message)
    {
        result.material.reset();
        result.diagnostics.push_back({MaterialDiagnosticSeverity::Error, data.sourcePath, node, port, message});
    };
    const auto incoming = [&](std::uint32_t node, std::uint32_t port, MaterialParameterType fallback)
    {
        for (const auto& link : data.graph.GetLinks())
            if (link.toNode == node && link.toInput == port)
                return outputType(data, link.fromNode);
        return fallback;
    };
    for (const auto& link : data.graph.GetLinks())
    {
        const auto* node = findNode(data, link.toNode);
        if (!node || !findNode(data, link.fromNode) || link.toInput >= Inputs(node->type, false).size())
            continue; // Structural diagnostics already come from the compiler.
        const auto actual = outputType(data, link.fromNode);
        const bool fixed = isOutput(node->type) || node->type == MaterialNodeType::TextureSample ||
                           node->type == MaterialNodeType::NormalDecode ||
                           (node->type == MaterialNodeType::Lerp && link.toInput == 2) ||
                           (node->type == MaterialNodeType::Clamp && link.toInput > 0);
        if (fixed)
        {
            const auto expected = inputType(data, *node, link.toInput);
            const bool broadcast = actual == MaterialParameterType::Float &&
                                   (isOutput(node->type) || node->type == MaterialNodeType::NormalDecode);
            if (actual != expected && !broadcast)
                error(node->id, static_cast<int>(link.toInput),
                      "Expected " + std::string(materialParameterTypeName(expected)) + ", got " +
                          materialParameterTypeName(actual));
        }
        else if (actual == MaterialParameterType::Texture2D)
            error(node->id, static_cast<int>(link.toInput), "Node requires a numeric input");
    }
    for (const auto& node : data.graph.GetNodes())
    {
        if (node.type == MaterialNodeType::Add || node.type == MaterialNodeType::Multiply ||
            node.type == MaterialNodeType::Lerp)
        {
            const auto a = incoming(node.id, 0, MaterialParameterType::Float);
            const auto b = incoming(node.id, 1, MaterialParameterType::Float);
            if (a != MaterialParameterType::Texture2D && b != MaterialParameterType::Texture2D && a != b &&
                a != MaterialParameterType::Float && b != MaterialParameterType::Float)
                error(node.id, 1, "Numeric inputs must have matching dimensions or Float broadcast");
        }
        if (node.type == MaterialNodeType::ComponentMask)
        {
            const auto type = incoming(node.id, 0, MaterialParameterType::Vec4);
            bool valid =
                type != MaterialParameterType::Texture2D && !node.channelMask.empty() && node.channelMask.size() <= 4;
            for (const auto channel : node.channelMask)
                valid =
                    valid && std::string("xyzw").find(channel) < static_cast<std::size_t>(static_cast<int>(type) + 1);
            if (!valid)
                error(node.id, 0, "ComponentMask channels exceed the numeric input dimension");
        }
    }
    return result;
}
bool textField(const char* label, std::string& text)
{
    std::array<char, 512> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
    if (!ImGui::InputText(label, buffer.data(), buffer.size()))
        return false;
    text = buffer.data();
    return true;
}
std::string lower(std::string value)
{
    for (char& c : value)
        if (static_cast<unsigned char>(c) < 128)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
} // namespace

bool MaterialGraphEditor::Open(const std::string& path, const MaterialDefinitionData& data)
{
    auto draft = data;
    draft.sourcePath = path;
    return openDocument(path, draft);
}
std::string MaterialGraphEditor::state(const MaterialDefinitionData& data)
{
    auto graph = data.graph;
    graph.GetParameters() = data.parameters;
    graph.GetRenderState() = data.renderState;
    return graph.serialize();
}
void MaterialGraphEditor::updateDirty()
{
    m_dirty = state(m_draft) != state(m_saved);
}
void MaterialGraphEditor::onDocumentReset()
{
    m_undo.clear();
    m_redo.clear();
    m_editBefore.reset();
    m_selected = m_linkNode = 0;
    m_pan = {};
    m_zoom = 1.f;
    m_fitRequested = true;
}
bool MaterialGraphEditor::validateDraft()
{
    commitPendingEdit();
    m_draft.graph.GetParameters() = m_draft.parameters;
    m_draft.graph.GetRenderState() = m_draft.renderState;
    const auto result = compileForEditor(m_draft);
    m_diagnostics = result.diagnostics;
    return result.Success();
}
void MaterialGraphEditor::recordEdit(MaterialDefinitionData before)
{
    if (state(before) == state(m_draft))
        return;
    m_undo.push_back(std::move(before));
    if (m_undo.size() > 128)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_diagnostics.clear();
    updateDirty();
}
void MaterialGraphEditor::queueEdit(MaterialDefinitionData before)
{
    if (state(before) == state(m_draft))
        return;
    if (!m_editBefore)
        m_editBefore = std::move(before);
    m_redo.clear();
    m_diagnostics.clear();
    updateDirty();
}
void MaterialGraphEditor::commitPendingEdit()
{
    if (m_editBefore)
    {
        auto before = std::move(*m_editBefore);
        m_editBefore.reset();
        recordEdit(std::move(before));
    }
}
bool MaterialGraphEditor::undo()
{
    commitPendingEdit();
    if (m_undo.empty())
        return false;
    m_redo.push_back(m_draft);
    m_draft = std::move(m_undo.back());
    m_undo.pop_back();
    m_linkNode = 0;
    if (!findNode(m_draft, m_selected))
        m_selected = 0;
    m_diagnostics.clear();
    updateDirty();
    return true;
}
bool MaterialGraphEditor::redo()
{
    commitPendingEdit();
    if (m_redo.empty())
        return false;
    m_undo.push_back(m_draft);
    m_draft = std::move(m_redo.back());
    m_redo.pop_back();
    m_linkNode = 0;
    if (!findNode(m_draft, m_selected))
        m_selected = 0;
    m_diagnostics.clear();
    updateDirty();
    return true;
}
std::uint32_t MaterialGraphEditor::addNode(MaterialNodeType type, const std::string& name)
{
    commitPendingEdit();
    auto before = m_draft;
    const auto count = m_draft.graph.GetNodes().size();
    m_selected = m_draft.graph.addNode(
        type, name.empty() || name == materialNodeTypeName(type) ? nodeName(type, m_chinese) : name,
        {40.f + static_cast<float>(count % 3) * 230.f, 50.f + static_cast<float>(count / 3) * 230.f});
    auto& node = m_draft.graph.GetNodes().back();
    if (type == MaterialNodeType::ComponentMask)
        node.channelMask = "xyz";
    if (type == MaterialNodeType::ScalarParameter || type == MaterialNodeType::VectorParameter ||
        type == MaterialNodeType::TextureParameter)
    {
        MaterialParameterDecl parameter;
        const std::string prefix = type == MaterialNodeType::ScalarParameter   ? "Scalar"
                                   : type == MaterialNodeType::VectorParameter ? "Vector"
                                                                               : "Texture";
        unsigned int index = 1;
        do
        {
            parameter.name = prefix + std::to_string(index++);
        } while (std::any_of(m_draft.parameters.begin(), m_draft.parameters.end(),
                             [&](const auto& p) { return p.name == parameter.name; }));
        parameter.displayName = nodeName(type, m_chinese);
        parameter.type = type == MaterialNodeType::ScalarParameter   ? MaterialParameterType::Float
                         : type == MaterialNodeType::VectorParameter ? MaterialParameterType::Vec3
                                                                     : MaterialParameterType::Texture2D;
        parameter.defaultValue = type == MaterialNodeType::ScalarParameter   ? MaterialParameterValue(0.f)
                                 : type == MaterialNodeType::VectorParameter ? MaterialParameterValue(Vec3{1, 1, 1})
                                                                             : MaterialParameterValue(std::string{});
        node.parameterName = parameter.name;
        m_draft.parameters.push_back(std::move(parameter));
    }
    recordEdit(std::move(before));
    return m_selected;
}
bool MaterialGraphEditor::disconnectPort(std::uint32_t to, std::uint32_t input)
{
    commitPendingEdit();
    auto before = m_draft;
    auto& links = m_draft.graph.GetLinks();
    const auto old = links.size();
    links.erase(std::remove_if(links.begin(), links.end(),
                               [&](const auto& link) { return link.toNode == to && link.toInput == input; }),
                links.end());
    if (old == links.size())
        return false;
    recordEdit(std::move(before));
    return true;
}
bool MaterialGraphEditor::connectPorts(std::uint32_t from, std::uint32_t output, std::uint32_t to, std::uint32_t input)
{
    commitPendingEdit();
    auto candidate = m_draft;
    auto& links = candidate.graph.GetLinks();
    links.erase(std::remove_if(links.begin(), links.end(),
                               [&](const auto& link) { return link.toNode == to && link.toInput == input; }),
                links.end());
    links.push_back({from, to, input, output});
    const auto result = compileForEditor(candidate);
    m_diagnostics = result.diagnostics;
    if (!result.Success())
        return false;
    auto before = m_draft;
    m_draft = std::move(candidate);
    recordEdit(std::move(before));
    return true;
}
bool MaterialGraphEditor::deleteNode(std::uint32_t id)
{
    commitPendingEdit();
    auto before = m_draft;
    if (!m_draft.graph.removeNode(id))
        return false;
    if (m_selected == id)
        m_selected = 0;
    if (m_linkNode == id)
        m_linkNode = 0;
    recordEdit(std::move(before));
    return true;
}
bool MaterialGraphEditor::setParameterDefault(const std::string& name, const MaterialParameterValue& value)
{
    const auto parameter = std::find_if(m_draft.parameters.begin(), m_draft.parameters.end(),
                                        [&](const auto& p) { return p.name == name; });
    if (parameter == m_draft.parameters.end() || parameter->type != getMaterialParameterType(value) ||
        !isFiniteMaterialValue(value))
        return false;
    commitPendingEdit();
    auto before = m_draft;
    parameter->defaultValue = value;
    recordEdit(std::move(before));
    return true;
}
std::vector<std::string> MaterialGraphEditor::GetRetainedAssetReferences() const
{
    std::set<std::string> references;
    const auto collect = [&](const MaterialDefinitionData& data)
    {
        for (const auto& node : data.graph.GetNodes())
            if (!node.texturePath.empty())
                references.insert(node.texturePath);
        for (const auto& parameter : data.parameters)
            if (const auto* path = std::get_if<std::string>(&parameter.defaultValue); path && !path->empty())
                references.insert(*path);
    };
    collect(m_draft);
    collect(m_saved);
    for (const auto& data : m_undo)
        collect(data);
    for (const auto& data : m_redo)
        collect(data);
    if (m_editBefore)
        collect(*m_editBefore);
    return {references.begin(), references.end()};
}

void MaterialGraphEditor::drawPalette(float width, float height)
{
    ImGui::BeginChild("NodePalette", {width, height}, ImGuiChildFlags_Borders);
    ImGui::TextUnformatted(T("Node palette", "节点库"));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("###NodeSearch", T("Search nodes...", "搜索节点…"), m_search.data(), m_search.size());
    const auto query = lower(m_search.data());
    const auto matches = [&](const NodeEntry& entry)
    {
        return query.empty() || lower(entry.english).find(query) != std::string::npos ||
               std::string(entry.chinese).find(query) != std::string::npos ||
               lower(materialNodeTypeName(entry.type)).find(query) != std::string::npos;
    };
    const char* categories[] = {T("Output", "输出"), T("Values and parameters", "数值与参数"), T("Textures", "纹理"),
                                T("Math", "数学运算")};
    for (int category = 0; category < 4; ++category)
    {
        if (std::none_of(std::begin(kNodes), std::end(kNodes),
                         [&](const auto& entry) { return entry.category == category && matches(entry); }))
            continue;
        const auto header = std::string(categories[category]) + "###Category" + std::to_string(category);
        if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        for (const auto& entry : kNodes)
        {
            if (entry.category != category || !matches(entry))
                continue;
            const auto label =
                std::string(m_chinese ? entry.chinese : entry.english) + "###Create" + materialNodeTypeName(entry.type);
            const bool existingOutput =
                isOutput(entry.type) && std::any_of(m_draft.graph.GetNodes().begin(), m_draft.graph.GetNodes().end(),
                                                    [](const auto& n) { return isOutput(n.type); });
            ImGui::BeginDisabled(existingOutput);
            if (ImGui::Selectable(label.c_str()))
                addNode(entry.type, {});
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "%s", existingOutput ? T("The graph already has a surface output.", "材质图已有一个表面输出节点。")
                                         : T("Click to create this node.", "点击以创建此节点。"));
        }
    }
    ImGui::EndChild();
}
void MaterialGraphEditor::drawCanvas()
{
    ImGui::BeginChild("NodeCanvas", m_canvasSize, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const auto origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    auto* draw = ImGui::GetWindowDrawList();
    const auto& io = ImGui::GetIO();
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    if (hovered && io.MouseWheel != 0 && !m_linkNode)
    {
        const float zoom = std::clamp(m_zoom + io.MouseWheel * .1f, .35f, 2.f);
        const ImVec2 anchor{(io.MousePos.x - origin.x - m_pan.x) / m_zoom,
                            (io.MousePos.y - origin.y - m_pan.y) / m_zoom};
        m_pan = {io.MousePos.x - origin.x - anchor.x * zoom, io.MousePos.y - origin.y - anchor.y * zoom};
        m_zoom = zoom;
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0))
    {
        m_pan.x += io.MouseDelta.x;
        m_pan.y += io.MouseDelta.y;
    }
    if (m_fitRequested)
    {
        m_fitRequested = false;
        if (!m_draft.graph.GetNodes().empty())
        {
            ImVec2 low{1e20f, 1e20f}, high{-1e20f, -1e20f};
            for (const auto& node : m_draft.graph.GetNodes())
            {
                low.x = std::min(low.x, node.position.x);
                low.y = std::min(low.y, node.position.y);
                high.x = std::max(high.x, node.position.x + 200);
                high.y = std::max(
                    high.y, node.position.y +
                                std::max(90.f, 62.f + static_cast<float>(Inputs(node.type, m_chinese).size()) * 22));
            }
            m_zoom = std::clamp(
                std::min((size.x - 48) / std::max(high.x - low.x, 1.f), (size.y - 48) / std::max(high.y - low.y, 1.f)),
                .35f, 1.5f);
            m_pan = {(size.x - (high.x - low.x) * m_zoom) * .5f - low.x * m_zoom,
                     (size.y - (high.y - low.y) * m_zoom) * .5f - low.y * m_zoom};
        }
    }
    ImGui::TextDisabled("%.0f%%", m_zoom * 100);
    const auto screen = [&](float x, float y) {
        return ImVec2{origin.x + m_pan.x + x * m_zoom, origin.y + m_pan.y + y * m_zoom};
    };
    const float grid = 32 * m_zoom;
    for (float x = std::fmod(m_pan.x, grid); x < size.x; x += grid)
        draw->AddLine({origin.x + x, origin.y}, {origin.x + x, origin.y + size.y}, IM_COL32(60, 66, 76, 85));
    for (float y = std::fmod(m_pan.y, grid); y < size.y; y += grid)
        draw->AddLine({origin.x, origin.y + y}, {origin.x + size.x, origin.y + y}, IM_COL32(60, 66, 76, 85));
    const auto wire = [&](ImVec2 a, ImVec2 b, ImU32 color, float thickness)
    {
        const float bend = std::max(45 * m_zoom, std::abs(b.x - a.x) * .45f);
        draw->AddBezierCubic(a, {a.x + bend, a.y}, {b.x - bend, b.y}, b, color, thickness);
    };
    for (const auto& link : m_draft.graph.GetLinks())
    {
        const auto *from = findNode(m_draft, link.fromNode), *to = findNode(m_draft, link.toNode);
        if (!from || !to)
            continue;
        wire(screen(from->position.x + 200, from->position.y + 48),
             screen(to->position.x, to->position.y + 48 + link.toInput * 22), typeColor(outputType(m_draft, from->id)),
             2 * m_zoom);
    }
    std::array<std::uint32_t, 4> pending{};
    std::optional<std::pair<std::uint32_t, std::uint32_t>> disconnect;
    bool hoveredPort = false;
    ImU32 temporaryColor = IM_COL32(245, 205, 100, 255);
    for (auto& node : m_draft.graph.GetNodes())
    {
        const auto ports = Inputs(node.type, m_chinese);
        const auto pos = screen(node.position.x, node.position.y);
        const ImVec2 end{pos.x + 200 * m_zoom,
                         pos.y + std::max(90.f, 62.f + static_cast<float>(ports.size()) * 22) * m_zoom};
        draw->AddRectFilled(pos, end, IM_COL32(35, 43, 56, 255), 5 * m_zoom);
        draw->AddRectFilled(pos, {end.x, pos.y + 32 * m_zoom}, IM_COL32(53, 65, 82, 255), 5 * m_zoom);
        const bool error = std::any_of(
            m_diagnostics.begin(), m_diagnostics.end(),
            [&](const auto& d) { return d.nodeId == node.id && d.severity == MaterialDiagnosticSeverity::Error; });
        draw->AddRect(pos, end,
                      error                   ? IM_COL32(245, 85, 85, 255)
                      : m_selected == node.id ? IM_COL32(110, 185, 255, 255)
                                              : IM_COL32(80, 91, 109, 255),
                      5 * m_zoom, 0, m_selected == node.id || error ? 2.f : 1.f);
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("Header", {200 * m_zoom, 32 * m_zoom});
        if (ImGui::IsItemClicked())
            m_selected = node.id;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            auto before = m_draft;
            node.position.x += io.MouseDelta.x / m_zoom;
            node.position.y += io.MouseDelta.y / m_zoom;
            queueEdit(std::move(before));
        }
        const auto title = nodeTitle(node, m_chinese);
        draw->PushClipRect({pos.x + 6 * m_zoom, pos.y}, {end.x - 6 * m_zoom, pos.y + 32 * m_zoom}, true);
        draw->AddText(nullptr, ImGui::GetFontSize() * m_zoom, {pos.x + 8 * m_zoom, pos.y + 8 * m_zoom}, IM_COL32_WHITE,
                      title.c_str());
        draw->PopClipRect();
        const auto port = [&](bool input, std::uint32_t index, ImVec2 center, MaterialParameterType type)
        {
            ImGui::PushID(input ? "Inputs" : "Outputs");
            ImGui::PushID(static_cast<int>(index));
            ImGui::SetCursorScreenPos({center.x - 9 * m_zoom, center.y - 9 * m_zoom});
            ImGui::InvisibleButton("Port", {18 * m_zoom, 18 * m_zoom});
            const bool over = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            hoveredPort = hoveredPort || over;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                commitPendingEdit();
                m_selected = node.id;
                m_linkNode = node.id;
                m_linkPort = index;
                m_linkIsInput = input;
            }
            if (input && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                disconnect = std::make_pair(node.id, index);
            ImU32 color = typeColor(type);
            if (over && m_linkNode && input != m_linkIsInput)
            {
                const auto from = input ? m_linkNode : node.id, to = input ? node.id : m_linkNode;
                const auto fromPort = input ? m_linkPort : index, toPort = input ? index : m_linkPort;
                auto candidate = m_draft;
                auto& links = candidate.graph.GetLinks();
                links.erase(std::remove_if(links.begin(), links.end(),
                                           [&](const auto& link)
                                           { return link.toNode == to && link.toInput == toPort; }),
                            links.end());
                links.push_back({from, to, toPort, fromPort});
                const auto result = compileForEditor(candidate);
                color = temporaryColor = result.Success() ? IM_COL32(110, 245, 150, 255) : IM_COL32(255, 105, 105, 255);
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(T("Release to connect", "松开鼠标以连接"));
                for (const auto& diagnostic : result.diagnostics)
                    ImGui::TextWrapped("%s", diagnosticText(diagnostic.message).c_str());
                ImGui::EndTooltip();
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                    pending = {from, fromPort, to, toPort};
            }
            else if (over)
                ImGui::SetTooltip("%s\n%s", typeName(type, m_chinese),
                                  input
                                      ? T("Drag to connect. Right click to disconnect.", "拖动以连接；右键断开此输入。")
                                      : T("Drag to an input to connect.", "拖动到输入端口以连接。"));
            draw->AddCircleFilled(center, 5 * m_zoom, color);
            if (over)
                draw->AddCircle(center, 8 * m_zoom, color, 0, 1.5f);
            ImGui::PopID();
            ImGui::PopID();
        };
        if (!isOutput(node.type))
        {
            const auto type = outputType(m_draft, node.id);
            port(false, 0, screen(node.position.x + 200, node.position.y + 48), type);
            draw->AddText(nullptr, ImGui::GetFontSize() * m_zoom, screen(node.position.x + 112, node.position.y + 39),
                          typeColor(type), typeName(type, m_chinese));
        }
        for (std::size_t input = 0; input < ports.size(); ++input)
        {
            port(true, static_cast<std::uint32_t>(input), screen(node.position.x, node.position.y + 48 + input * 22),
                 inputType(m_draft, node, static_cast<std::uint32_t>(input)));
            draw->AddText(nullptr, ImGui::GetFontSize() * m_zoom,
                          screen(node.position.x + 14, node.position.y + 40 + input * 22), IM_COL32(235, 238, 245, 255),
                          ports[input]);
        }
        ImGui::PopID();
    }
    if (m_linkNode)
    {
        if (const auto* node = findNode(m_draft, m_linkNode))
        {
            const auto anchor =
                screen(node->position.x + (m_linkIsInput ? 0 : 200), node->position.y + 48 + m_linkPort * 22);
            wire(m_linkIsInput ? io.MousePos : anchor, m_linkIsInput ? anchor : io.MousePos, temporaryColor, 2.5f);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_linkNode = 0;
    }
    if (hovered && !hoveredPort && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        m_selected = 0;
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(size);
    ImGui::EndChild();
    if (disconnect)
        disconnectPort(disconnect->first, disconnect->second);
    if (pending[0])
        connectPorts(pending[0], pending[1], pending[2], pending[3]);
    if (!ImGui::IsAnyItemActive())
        commitPendingEdit();
}

void MaterialGraphEditor::drawProperties()
{
    auto before = m_draft;
    ImGui::TextUnformatted(T("Node properties", "节点属性"));
    const auto selected = std::find_if(m_draft.graph.GetNodes().begin(), m_draft.graph.GetNodes().end(),
                                       [&](const auto& node) { return node.id == m_selected; });
    std::uint32_t eraseNode = 0;
    if (selected != m_draft.graph.GetNodes().end())
    {
        auto& node = *selected;
        ImGui::TextDisabled("%s · %u", nodeName(node.type, m_chinese), node.id);
        textField(T("Name###NodeName", "名称###NodeName"), node.name);
        if (node.type == MaterialNodeType::ScalarParameter || node.type == MaterialNodeType::VectorParameter ||
            node.type == MaterialNodeType::TextureParameter)
        {
            textField(T("Parameter identifier###ParameterName", "参数标识符###ParameterName"), node.parameterName);
            ImGui::TextWrapped("%s", T("Identifiers stay unchanged when the interface language changes.",
                                       "参数标识符用于序列化和引用，不随界面语言改变。"));
        }
        if (node.type == MaterialNodeType::ComponentMask)
            textField(T("Components (xyzw)###ChannelMask", "分量（xyzw）###ChannelMask"), node.channelMask);
        if (node.type == MaterialNodeType::Constant)
        {
            int type = static_cast<int>(node.constantType);
            if (ImGui::Combo(T("Constant type###ConstantType", "常量类型###ConstantType"), &type,
                             T("Float\0Vec2\0Vec3\0Vec4\0", "标量\0二维向量\0三维向量\0四维向量\0")))
                node.constantType = static_cast<MaterialParameterType>(type);
            MaterialParameterValue value = node.constantType == MaterialParameterType::Float
                                               ? MaterialParameterValue(node.value.x)
                                           : node.constantType == MaterialParameterType::Vec2
                                               ? MaterialParameterValue(Vec2{node.value.x, node.value.y})
                                           : node.constantType == MaterialParameterType::Vec3
                                               ? MaterialParameterValue(Vec3{node.value.x, node.value.y, node.value.z})
                                               : MaterialParameterValue(node.value);
            if (drawMaterialValue(T("Value###ConstantValue", "数值###ConstantValue"), value))
                node.value = materialNumericSlot(value);
        }
        if (node.type == MaterialNodeType::TexCoord)
            ImGui::TextUnformatted(T("Texture coordinate channel: UV0", "纹理坐标通道：UV0"));
        if (ImGui::Button(T("Delete selected node###DeleteNode", "删除选中节点###DeleteNode")))
            eraseNode = node.id;
    }
    else
        ImGui::TextWrapped("%s", T("Select a node to edit its properties.", "选择节点以编辑其属性。"));
    ImGui::Separator();
    if (ImGui::CollapsingHeader(T("Material settings###MaterialSettings", "材质设置###MaterialSettings"),
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        int shading = static_cast<int>(m_draft.renderState.shadingModel),
            blend = static_cast<int>(m_draft.renderState.blendMode);
        if (ImGui::Combo(T("Shading###Shading", "着色模型###Shading"), &shading, T("Lit\0Unlit\0", "光照\0无光照\0")))
            m_draft.renderState.shadingModel = static_cast<MaterialShadingModel>(shading);
        if (ImGui::Combo(T("Blend###Blend", "混合模式###Blend"), &blend,
                         T("Opaque\0Masked\0Alpha\0", "不透明\0遮罩\0透明混合\0")))
            m_draft.renderState.blendMode = static_cast<MaterialBlendMode>(blend);
        ImGui::Checkbox(T("Two sided###TwoSided", "双面渲染###TwoSided"), &m_draft.renderState.twoSided);
        if (m_draft.renderState.blendMode == MaterialBlendMode::Masked)
            ImGui::SliderFloat(T("Mask cutoff###MaskCutoff", "遮罩阈值###MaskCutoff"), &m_draft.renderState.maskCutoff,
                               0, 1);
    }
    if (ImGui::CollapsingHeader(T("Parameter declarations###Parameters", "参数声明###Parameters"),
                                ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button(T("Add parameter###AddParameter", "添加参数###AddParameter")))
        {
            MaterialParameterDecl parameter;
            unsigned int index = 1;
            do
            {
                parameter.name = "Parameter" + std::to_string(index++);
            } while (std::any_of(m_draft.parameters.begin(), m_draft.parameters.end(),
                                 [&](const auto& p) { return p.name == parameter.name; }));
            parameter.displayName = T("Parameter", "参数");
            m_draft.parameters.push_back(std::move(parameter));
        }
        std::size_t erase = m_draft.parameters.size();
        for (std::size_t index = 0; index < m_draft.parameters.size(); ++index)
        {
            auto& parameter = m_draft.parameters[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::Separator();
            textField(T("Identifier###Identifier", "标识符###Identifier"), parameter.name);
            textField(T("Display name###DisplayName", "显示名称###DisplayName"), parameter.displayName);
            textField(T("Group###Group", "分组###Group"), parameter.group);
            int type = static_cast<int>(parameter.type);
            if (ImGui::Combo(
                    T("Type###ParameterType", "类型###ParameterType"), &type,
                    T("Float\0Vec2\0Vec3\0Vec4\0Texture2D\0", "标量\0二维向量\0三维向量\0四维向量\0二维纹理\0")))
            {
                parameter.type = static_cast<MaterialParameterType>(type);
                parameter.sampling = TextureSampling::Linear;
                parameter.defaultValue = type == 0   ? MaterialParameterValue(0.f)
                                         : type == 1 ? MaterialParameterValue(Vec2{})
                                         : type == 2 ? MaterialParameterValue(Vec3{})
                                         : type == 3 ? MaterialParameterValue(Vec4{})
                                                     : MaterialParameterValue(std::string{});
            }
            drawMaterialValue(T("Default###DefaultValue", "默认值###DefaultValue"), parameter.defaultValue,
                              parameter.range);
            if (parameter.type == MaterialParameterType::Texture2D)
            {
                int sampling = static_cast<int>(parameter.sampling);
                if (ImGui::Combo(T("Sampling###Sampling", "采样方式###Sampling"), &sampling,
                                 T("sRGB\0Linear\0Normal\0", "sRGB 颜色\0线性\0法线\0")))
                    parameter.sampling = static_cast<TextureSampling>(sampling);
            }
            if (ImGui::Button(T("Remove parameter###RemoveParameter", "删除参数###RemoveParameter")))
                erase = index;
            ImGui::PopID();
        }
        if (erase < m_draft.parameters.size())
            m_draft.parameters.erase(m_draft.parameters.begin() + erase);
    }
    queueEdit(std::move(before));
    if (eraseNode)
    {
        commitPendingEdit();
        deleteNode(eraseNode);
    }
    if (!ImGui::IsAnyItemActive())
        commitPendingEdit();
}
void MaterialGraphEditor::draw()
{
    if (!m_visible)
    {
        m_focused = false;
        return;
    }
    if (beginDocument(T("Material graph###MaterialGraphEditor", "节点材质###MaterialGraphEditor")))
    {
        ImGui::BeginDisabled(!canUndo());
        if (ImGui::Button(T("Undo###UndoGraph", "撤销###UndoGraph")))
            undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!canRedo());
        if (ImGui::Button(T("Redo###RedoGraph", "重做###RedoGraph")))
            redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(T("Fit graph###FitGraph", "适配画布###FitGraph")))
            m_fitRequested = true;
        ImGui::SameLine();
        if (ImGui::Button(T("Reset view###ResetGraphView", "重置视图###ResetGraphView")))
        {
            m_pan = {};
            m_zoom = 1.f;
        }
        ImGui::TextWrapped(
            "%s", T("Drag ports to connect · right click input to disconnect · middle drag to pan · wheel to zoom",
                    "拖动端口连线 · 右键输入断线 · 中键拖动画布 · 滚轮缩放"));
        const auto available = ImGui::GetContentRegionAvail();
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float paletteWidth = std::clamp(available.x * .18f, 145.f, 210.f),
                    propertiesWidth = std::clamp(available.x * .26f, 215.f, 310.f);
        const float height = std::max(available.y, 180.f);
        drawPalette(paletteWidth, height);
        ImGui::SameLine();
        m_canvasSize = {std::max(120.f, available.x - paletteWidth - propertiesWidth - 2 * gap), height};
        drawCanvas();
        ImGui::SameLine();
        ImGui::BeginChild("NodeProperties", {propertiesWidth, height}, ImGuiChildFlags_Borders);
        ImGui::PushItemWidth(std::max(80.f, ImGui::GetContentRegionAvail().x * .5f));
        drawProperties();
        ImGui::PopItemWidth();
        ImGui::EndChild();
        m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const auto& io = ImGui::GetIO();
        if (m_focused && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        {
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                if (io.KeyShift)
                    redo();
                else
                    undo();
            }
            else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
                redo();
            else if (!io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_selected)
                deleteNode(m_selected);
            else if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
                m_fitRequested = true;
        }
        m_draft.graph.GetParameters() = m_draft.parameters;
        m_draft.graph.GetRenderState() = m_draft.renderState;
    }
    endDocument();
}
} // namespace tinyengine
