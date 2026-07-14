#include "MiniEngine/UI/UISystem.hpp"

#include <algorithm>

namespace MiniEngine
{
    UIWidget& UISystem::AddPanel(std::string id, const UIRect rect, const Vec4 color) { m_widgets.push_back({std::move(id), UIWidgetType::Panel, rect, color}); return m_widgets.back(); }
    UIWidget& UISystem::AddLabel(std::string id, std::string text, const UIRect rect) { m_widgets.push_back({std::move(id), UIWidgetType::Label, rect, {1, 1, 1, 1}, std::move(text)}); return m_widgets.back(); }
    UIWidget& UISystem::AddButton(std::string id, std::string text, const UIRect rect, std::function<void()> onClick)
    {
        m_widgets.push_back({std::move(id), UIWidgetType::Button, rect, {0.18f, 0.35f, 0.62f, 1.0f}, std::move(text), {}, true, true, false, std::move(onClick)}); return m_widgets.back();
    }
    UIWidget& UISystem::AddImage(std::string id, std::string imagePath, const UIRect rect) { m_widgets.push_back({std::move(id), UIWidgetType::Image, rect, {1, 1, 1, 1}, {}, std::move(imagePath)}); return m_widgets.back(); }
    UIWidget* UISystem::Find(const std::string& id) { const auto found = std::find_if(m_widgets.begin(), m_widgets.end(), [&](const UIWidget& widget) { return widget.id == id; }); return found == m_widgets.end() ? nullptr : &*found; }
    void UISystem::Remove(const std::string& id) { m_widgets.erase(std::remove_if(m_widgets.begin(), m_widgets.end(), [&](const UIWidget& widget) { return widget.id == id; }), m_widgets.end()); if (m_focusedId == id) m_focusedId.clear(); }
    void UISystem::Clear() { m_widgets.clear(); m_drawList.clear(); m_focusedId.clear(); }
    void UISystem::Update(const float cursorX, const float cursorY, const bool primaryDown)
    {
        ResolveLayout();
        if (primaryDown && !m_wasPrimaryDown)
        {
            for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it)
            {
                const auto rect = m_resolvedRects.find(it->id);
                if (!it->visible || !it->enabled || it->type != UIWidgetType::Button || rect == m_resolvedRects.end() || !rect->second.Contains(cursorX, cursorY)) continue;
                for (auto& widget : m_widgets) widget.focused = false;
                it->focused = true; m_focusedId = it->id; if (it->onClick) it->onClick(); break;
            }
        }
        m_wasPrimaryDown = primaryDown;
    }
    void UISystem::ActivateFocused() { if (auto* focused = Find(m_focusedId); focused && focused->visible && focused->enabled && focused->onClick) focused->onClick(); }
    bool UISystem::SetParent(const std::string& childId, const std::string& parentId)
    {
        UIWidget* child = Find(childId); UIWidget* parent = Find(parentId);
        if (!child || !parent || child == parent) return false;
        for (const UIWidget* ancestor = parent; ancestor && !ancestor->parentId.empty(); ancestor = Find(ancestor->parentId)) if (ancestor == child) return false;
        child->parentId = parentId; return true;
    }
    bool UISystem::SetLayout(const std::string& id, const UILayout layout, const float spacing, const float padding)
    {
        UIWidget* widget = Find(id); if (!widget) return false;
        widget->layout = layout; widget->spacing = std::max(0.0f, spacing); widget->padding = std::max(0.0f, padding); return true;
    }
    bool UISystem::SetAnchor(const std::string& id, const UIAnchor anchor) { UIWidget* widget = Find(id); if (!widget) return false; widget->anchor = anchor; return true; }
    void UISystem::SetViewportSize(const float width, const float height) { m_viewportWidth = std::max(1.0f, width); m_viewportHeight = std::max(1.0f, height); }
    void UISystem::ResolveLayout()
    {
        m_resolvedRects.clear();
        auto anchored = [](const UIWidget& widget, const UIRect& parent)
        {
            UIRect result = widget.rect;
            if (widget.anchor == UIAnchor::TopRight) result.x = parent.x + parent.width - widget.rect.x - widget.rect.width;
            else if (widget.anchor == UIAnchor::BottomLeft) result.y = parent.y + parent.height - widget.rect.y - widget.rect.height;
            else if (widget.anchor == UIAnchor::BottomRight) { result.x = parent.x + parent.width - widget.rect.x - widget.rect.width; result.y = parent.y + parent.height - widget.rect.y - widget.rect.height; }
            else if (widget.anchor == UIAnchor::Center) { result.x = parent.x + parent.width * 0.5f + widget.rect.x - widget.rect.width * 0.5f; result.y = parent.y + parent.height * 0.5f + widget.rect.y - widget.rect.height * 0.5f; }
            else if (widget.anchor == UIAnchor::Stretch) { result.x = parent.x + widget.rect.x; result.y = parent.y + widget.rect.y; result.width = std::max(0.0f, parent.width - widget.rect.x - widget.rect.width); result.height = std::max(0.0f, parent.height - widget.rect.y - widget.rect.height); }
            else { result.x += parent.x; result.y += parent.y; }
            return result;
        };
        const UIRect viewport{0.0f, 0.0f, m_viewportWidth, m_viewportHeight};
        for (const auto& widget : m_widgets) if (widget.parentId.empty()) m_resolvedRects[widget.id] = anchored(widget, viewport);
        for (std::size_t pass = 0; pass < m_widgets.size(); ++pass)
            for (const auto& widget : m_widgets) if (!widget.parentId.empty() && m_resolvedRects.count(widget.parentId) && !m_resolvedRects.count(widget.id)) m_resolvedRects[widget.id] = anchored(widget, m_resolvedRects[widget.parentId]);
        for (const auto& parent : m_widgets)
        {
            if (parent.layout == UILayout::Absolute || !m_resolvedRects.count(parent.id)) continue;
            const UIRect container = m_resolvedRects[parent.id]; float cursor = parent.padding;
            for (const auto& child : m_widgets)
            {
                if (child.parentId != parent.id || !m_resolvedRects.count(child.id)) continue;
                UIRect& rect = m_resolvedRects[child.id];
                if (parent.layout == UILayout::Vertical) { rect.x = container.x + parent.padding; rect.y = container.y + cursor; rect.width = std::max(0.0f, container.width - parent.padding * 2.0f); cursor += rect.height + parent.spacing; }
                else { rect.x = container.x + cursor; rect.y = container.y + parent.padding; rect.height = std::max(0.0f, container.height - parent.padding * 2.0f); cursor += rect.width + parent.spacing; }
            }
        }
    }
    void UISystem::FocusNext()
    {
        std::vector<UIWidget*> focusable;
        for (auto& widget : m_widgets) if (widget.visible && widget.enabled && widget.type == UIWidgetType::Button) focusable.push_back(&widget);
        if (focusable.empty()) return;
        auto found = std::find_if(focusable.begin(), focusable.end(), [&](UIWidget* widget) { return widget->id == m_focusedId; });
        UIWidget* next = found == focusable.end() || ++found == focusable.end() ? focusable.front() : *found;
        for (auto& widget : m_widgets) widget.focused = false;
        next->focused = true; m_focusedId = next->id;
    }
    const std::vector<UIDrawCommand>& UISystem::BuildDrawList()
    {
        m_drawList.clear(); ResolveLayout();
        for (const auto& widget : m_widgets)
        {
            if (!widget.visible) continue;
            Vec4 color = widget.color;
            if (!widget.enabled) color.w *= 0.4f;
            if (widget.focused) { color.x = std::min(1.0f, color.x + 0.2f); color.y = std::min(1.0f, color.y + 0.2f); }
            const auto rect = m_resolvedRects.find(widget.id); if (rect == m_resolvedRects.end()) continue;
            m_drawList.push_back({widget.type, rect->second, color, widget.text, widget.imagePath, widget.fontSize});
        }
        return m_drawList;
    }
    const std::string& UISystem::GetFocusedId() const { return m_focusedId; }
}
