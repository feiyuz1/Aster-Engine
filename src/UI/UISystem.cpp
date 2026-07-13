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
        if (primaryDown && !m_wasPrimaryDown)
        {
            for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it)
            {
                if (!it->visible || !it->enabled || it->type != UIWidgetType::Button || !it->rect.Contains(cursorX, cursorY)) continue;
                for (auto& widget : m_widgets) widget.focused = false;
                it->focused = true; m_focusedId = it->id; if (it->onClick) it->onClick(); break;
            }
        }
        m_wasPrimaryDown = primaryDown;
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
        m_drawList.clear();
        for (const auto& widget : m_widgets)
        {
            if (!widget.visible) continue;
            Vec4 color = widget.color;
            if (!widget.enabled) color.w *= 0.4f;
            if (widget.focused) { color.x = std::min(1.0f, color.x + 0.2f); color.y = std::min(1.0f, color.y + 0.2f); }
            m_drawList.push_back({widget.type, widget.rect, color, widget.text, widget.imagePath});
        }
        return m_drawList;
    }
    const std::string& UISystem::GetFocusedId() const { return m_focusedId; }
}
