#include "editor_app.h"
#include "tinyengine/platform/platform.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace tinyengine
{
void EditorApp::openAssetReferences(const std::string& path)
{
    m_assetReferenceRoot = path;
    m_showAssetReferences = true;
    m_assetReferencesDirty = true;
    m_assetReferencesFit = true;
}

void EditorApp::drawAssetReferenceViewer()
{
    const auto* resources = m_engine.GetContext().resourceManager;
    if (!m_showAssetReferences || !resources)
        return;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(
        {std::min(scaled(960.0f), viewport->WorkSize.x * .9f), std::min(scaled(600.0f), viewport->WorkSize.y * .85f)},
        ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(localize("Asset References###AssetReferences", "资产依赖图###AssetReferences"),
                      &m_showAssetReferences))
    {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", m_assetReferenceRoot.c_str());
    ImGui::SetNextItemWidth(scaled(150.0f));
    if (ImGui::SliderInt(localize("Depth", "展开层数"), &m_assetReferenceDepth, 1, 8))
        m_assetReferencesDirty = true;
    ImGui::SameLine();
    if (ImGui::Button(localize("Refresh", "刷新")))
        m_assetReferencesDirty = true;
    ImGui::SameLine();
    if (ImGui::Button(localize("Fit", "适应画布")))
        m_assetReferencesFit = true;
    if (m_assetReferencesDirty)
    {
        m_assetReferenceGraph = buildAssetReferenceGraph(*resources, m_assetReferenceRoot, m_assetReferenceDepth);
        m_assetReferencesDirty = false;
        m_assetReferencesFit = true;
    }
    ImGui::TextDisabled("%s", localize("Referencers  ->  Selected asset  ->  Dependencies",
                                       "引用它的资产  →  当前资产  →  它依赖的资产"));
    ImGui::TextDisabled("%s", localize("Click to locate; double-click to focus. Middle-drag to pan; wheel to zoom.",
                                       "单击定位资产，双击以此为中心；中键拖动画布，滚轮缩放。"));
    if (m_assetReferenceGraph.truncated)
        ImGui::TextColored({1, .7f, .25f, 1}, "%s",
                           localize("Showing at most 128 nodes; focus a node to explore further.",
                                    "最多显示 128 个节点；可双击节点继续查看其引用关系。"));
    ImGui::Text(localize("%zu nodes, %zu references", "%zu 个节点，%zu 条引用"), m_assetReferenceGraph.nodes.size(),
                m_assetReferenceGraph.edges.size());
    ImGui::BeginChild("ReferenceCanvas", {0, 0}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const auto origin = ImGui::GetCursorScreenPos();
    const auto available = ImGui::GetContentRegionAvail();
    const ImVec2 extent{std::max(1.0f, available.x), std::max(1.0f, available.y)};
    const ImVec2 center{origin.x + extent.x * .5f, origin.y + extent.y * .5f};
    const float width = scaled(200.0f), height = scaled(72.0f), xStep = scaled(280.0f), yStep = scaled(110.0f);
    auto& graph = m_assetReferenceGraph;
    if (m_assetReferencesFit)
    {
        float minX = -width * .5f, maxX = width * .5f, minY = -height * .5f, maxY = height * .5f;
        for (const auto& node : graph.nodes)
        {
            minX = std::min(minX, node.column * xStep - width * .5f);
            maxX = std::max(maxX, node.column * xStep + width * .5f);
            minY = std::min(minY, node.row * yStep - height * .5f);
            maxY = std::max(maxY, node.row * yStep + height * .5f);
        }
        m_assetReferenceZoom = std::clamp(
            std::min((extent.x - scaled(24)) / (maxX - minX), (extent.y - scaled(24)) / (maxY - minY)), .35f, 1.0f);
        m_assetReferencePan = {-(minX + maxX) * .5f * m_assetReferenceZoom,
                               -(minY + maxY) * .5f * m_assetReferenceZoom};
        m_assetReferencesFit = false;
    }
    ImGui::InvisibleButton("Canvas", extent, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        m_assetReferencePan.x += io.MouseDelta.x;
        m_assetReferencePan.y += io.MouseDelta.y;
    }
    if (hovered && io.MouseWheel != 0)
    {
        const float before = m_assetReferenceZoom;
        m_assetReferenceZoom = std::clamp(before * std::pow(1.15f, io.MouseWheel), .35f, 2.0f);
        const float ratio = m_assetReferenceZoom / before;
        m_assetReferencePan = {io.MousePos.x - center.x - (io.MousePos.x - center.x - m_assetReferencePan.x) * ratio,
                               io.MousePos.y - center.y - (io.MousePos.y - center.y - m_assetReferencePan.y) * ratio};
    }
    const float zoom = m_assetReferenceZoom;
    const auto nodeOrigin = [&](const AssetReferenceNode& node)
    {
        return ImVec2{center.x + m_assetReferencePan.x + (node.column * xStep - width * .5f) * zoom,
                      center.y + m_assetReferencePan.y + (node.row * yStep - height * .5f) * zoom};
    };
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + extent.x, origin.y + extent.y}, true);
    for (const auto& edge : graph.edges)
    {
        const auto from = nodeOrigin(graph.nodes[edge.from]), to = nodeOrigin(graph.nodes[edge.to]);
        const ImVec2 start{from.x + width * zoom, from.y + height * zoom * .5f};
        const ImVec2 end{to.x, to.y + height * zoom * .5f};
        const float bend = std::max(scaled(40) * zoom, std::abs(end.x - start.x) * .5f);
        const auto color = graph.nodes[edge.to].missing ? IM_COL32(225, 105, 85, 255) : IM_COL32(125, 157, 189, 255);
        draw->AddBezierCubic(start, {start.x + bend, start.y}, {end.x - bend, end.y}, end, color,
                             std::max(1.0f, 2 * zoom));
        draw->AddTriangleFilled(end, {end.x - 8 * zoom, end.y - 4 * zoom}, {end.x - 8 * zoom, end.y + 4 * zoom}, color);
    }
    for (std::size_t index = 0; index < graph.nodes.size(); ++index)
    {
        const auto& node = graph.nodes[index];
        const auto top = nodeOrigin(node);
        const ImVec2 bottom{top.x + width * zoom, top.y + height * zoom};
        const bool nodeHovered = hovered && io.MousePos.x >= top.x && io.MousePos.x <= bottom.x &&
                                 io.MousePos.y >= top.y && io.MousePos.y <= bottom.y;
        const auto border = node.missing ? IM_COL32(225, 105, 85, 255)
                            : index == 0 ? IM_COL32(103, 184, 149, 255)
                                         : IM_COL32(104, 146, 191, 255);
        draw->AddRectFilled(top, bottom, nodeHovered ? IM_COL32(52, 62, 76, 255) : IM_COL32(35, 42, 52, 255), 5 * zoom);
        draw->AddRect(top, bottom, border, 5 * zoom, 0, index == 0 ? 2.5f : 1.5f);
        draw->PushClipRect({top.x + 6 * zoom, top.y}, {bottom.x - 6 * zoom, bottom.y}, true);
        const auto label = node.builtin ? node.path : toUtf8Path(fromUtf8Path(node.path).filename());
        const auto type =
            node.missing ? std::string(localize("Missing reference", "缺失引用")) : localizeReflected(node.type);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * zoom, {top.x + 10 * zoom, top.y + 12 * zoom},
                      IM_COL32(232, 236, 242, 255), label.c_str());
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize() * zoom, {top.x + 10 * zoom, top.y + 38 * zoom}, border,
                      type.c_str());
        draw->PopClipRect();
        if (nodeHovered)
        {
            ImGui::SetTooltip("%s\n%s", node.path.c_str(), type.c_str());
            if (!node.missing && !node.builtin)
            {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    openAssetReferences(node.path);
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    locateAsset(node.path);
            }
        }
    }
    draw->PopClipRect();
    ImGui::EndChild();
    ImGui::End();
}
} // namespace tinyengine
