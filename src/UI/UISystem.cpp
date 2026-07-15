#include "MiniEngine/UI/UISystem.hpp"

#include <algorithm>

namespace MiniEngine
{
UIWidget &UISystem::AddPanel(std::string id, const UIRect rect, const Vec4 color)
{
    m_widgets.push_back({std::move(id), UIWidgetType::Panel, rect, color});
    return m_widgets.back();
}
UIWidget &UISystem::AddLabel(std::string id, std::string text, const UIRect rect)
{
    m_widgets.push_back({std::move(id), UIWidgetType::Label, rect, {1, 1, 1, 1}, std::move(text)});
    return m_widgets.back();
}
UIWidget &UISystem::AddButton(std::string id, std::string text, const UIRect rect, std::function<void()> onClick)
{
    m_widgets.push_back({std::move(id),
                         UIWidgetType::Button,
                         rect,
                         {0.18f, 0.35f, 0.62f, 1.0f},
                         std::move(text),
                         {},
                         true,
                         true,
                         false,
                         std::move(onClick)});
    return m_widgets.back();
}
UIWidget &UISystem::AddImage(std::string id, std::string imagePath, const UIRect rect)
{
    m_widgets.push_back({std::move(id), UIWidgetType::Image, rect, {1, 1, 1, 1}, {}, std::move(imagePath)});
    return m_widgets.back();
}
UIWidget &UISystem::AddInputField(std::string id, std::string text, const UIRect rect, std::string placeholder)
{
    UIWidget &widget = AddPanel(std::move(id), rect, {0.08f, 0.1f, 0.14f, 1.0f});
    widget.type = UIWidgetType::InputField;
    widget.text = std::move(text);
    widget.placeholder = std::move(placeholder);
    return widget;
}
UIWidget &UISystem::AddList(std::string id, std::vector<std::string> items, const UIRect rect)
{
    UIWidget &widget = AddPanel(std::move(id), rect, {0.1f, 0.12f, 0.16f, 1.0f});
    widget.type = UIWidgetType::List;
    widget.items = std::move(items);
    return widget;
}
UIWidget &UISystem::AddDropdown(std::string id, std::vector<std::string> items, const UIRect rect)
{
    UIWidget &widget = AddList(std::move(id), std::move(items), rect);
    widget.type = UIWidgetType::Dropdown;
    return widget;
}
UIWidget &UISystem::AddProgressBar(std::string id, const float value, const UIRect rect, const float minimum, const float maximum)
{
    UIWidget &widget = AddPanel(std::move(id), rect, {0.08f, 0.1f, 0.14f, 1.0f});
    widget.type = UIWidgetType::ProgressBar;
    widget.minimum = std::min(minimum, maximum);
    widget.maximum = std::max(minimum, maximum);
    widget.value = std::clamp(value, widget.minimum, widget.maximum);
    return widget;
}
UIWidget *UISystem::Find(const std::string &id)
{
    const auto found = std::find_if(m_widgets.begin(), m_widgets.end(), [&](const UIWidget &widget) { return widget.id == id; });
    return found == m_widgets.end() ? nullptr : &*found;
}
void UISystem::Remove(const std::string &id)
{
    m_widgets.erase(std::remove_if(m_widgets.begin(), m_widgets.end(), [&](const UIWidget &widget) { return widget.id == id; }),
                    m_widgets.end());
    if (m_focusedId == id)
        m_focusedId.clear();
}
void UISystem::Clear()
{
    m_widgets.clear();
    m_drawList.clear();
    m_focusedId.clear();
}
void UISystem::Update(const float cursorX, const float cursorY, const bool primaryDown)
{
    ResolveLayout();
    if (primaryDown && !m_wasPrimaryDown)
    {
        for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it)
        {
            const auto rect = m_resolvedRects.find(it->id);
            const bool interactive = it->type == UIWidgetType::Button || it->type == UIWidgetType::InputField ||
                                     it->type == UIWidgetType::List || it->type == UIWidgetType::Dropdown;
            if (!it->visible || !it->enabled || !interactive || rect == m_resolvedRects.end() || !rect->second.Contains(cursorX, cursorY))
                continue;
            for (auto &widget : m_widgets)
                widget.focused = false;
            it->focused = true;
            m_focusedId = it->id;
            if (it->type == UIWidgetType::Dropdown)
                it->expanded = !it->expanded;
            if ((it->type == UIWidgetType::List || (it->type == UIWidgetType::Dropdown && it->expanded)) && !it->items.empty())
            {
                const float itemHeight = std::max(1.0f, rect->second.height / static_cast<float>(it->items.size()));
                SelectItem(it->id, std::clamp(static_cast<int>((cursorY - rect->second.y) / itemHeight), 0,
                                              static_cast<int>(it->items.size()) - 1));
            }
            if (it->onClick)
                it->onClick();
            break;
        }
    }
    m_wasPrimaryDown = primaryDown;
}
bool UISystem::InputText(const std::string &utf8)
{
    UIWidget *focused = Find(m_focusedId);
    if (!focused || focused->type != UIWidgetType::InputField || !focused->enabled || utf8.empty())
        return false;
    focused->text += utf8;
    if (focused->onTextChanged)
        focused->onTextChanged(focused->text);
    return true;
}
bool UISystem::Backspace()
{
    UIWidget *focused = Find(m_focusedId);
    if (!focused || focused->type != UIWidgetType::InputField || focused->text.empty())
        return false;
    std::size_t offset = focused->text.size() - 1;
    while (offset > 0 && (static_cast<unsigned char>(focused->text[offset]) & 0xC0u) == 0x80u)
        --offset;
    focused->text.erase(offset);
    if (focused->onTextChanged)
        focused->onTextChanged(focused->text);
    return true;
}
bool UISystem::SelectItem(const std::string &id, const int index)
{
    UIWidget *widget = Find(id);
    if (!widget || (widget->type != UIWidgetType::List && widget->type != UIWidgetType::Dropdown) || index < 0 ||
        index >= static_cast<int>(widget->items.size()))
        return false;
    widget->selectedIndex = index;
    if (widget->onSelectionChanged)
        widget->onSelectionChanged(index);
    return true;
}
bool UISystem::SetValue(const std::string &id, const float value)
{
    UIWidget *widget = Find(id);
    if (!widget || widget->type != UIWidgetType::ProgressBar)
        return false;
    widget->value = std::clamp(value, widget->minimum, widget->maximum);
    if (widget->onValueChanged)
        widget->onValueChanged(widget->value);
    return true;
}
void UISystem::ActivateFocused()
{
    if (auto *focused = Find(m_focusedId); focused && focused->visible && focused->enabled && focused->onClick)
        focused->onClick();
}
bool UISystem::SetParent(const std::string &childId, const std::string &parentId)
{
    UIWidget *child = Find(childId);
    UIWidget *parent = Find(parentId);
    if (!child || !parent || child == parent)
        return false;
    for (const UIWidget *ancestor = parent; ancestor && !ancestor->parentId.empty(); ancestor = Find(ancestor->parentId))
        if (ancestor == child)
            return false;
    child->parentId = parentId;
    return true;
}
bool UISystem::SetLayout(const std::string &id, const UILayout layout, const float spacing, const float padding)
{
    UIWidget *widget = Find(id);
    if (!widget)
        return false;
    widget->layout = layout;
    widget->spacing = std::max(0.0f, spacing);
    widget->padding = std::max(0.0f, padding);
    return true;
}
bool UISystem::SetAnchor(const std::string &id, const UIAnchor anchor)
{
    UIWidget *widget = Find(id);
    if (!widget)
        return false;
    widget->anchor = anchor;
    return true;
}
void UISystem::SetViewportSize(const float width, const float height)
{
    m_viewportWidth = std::max(1.0f, width);
    m_viewportHeight = std::max(1.0f, height);
}
void UISystem::SetDpiScale(const float scale)
{
    m_dpiScale = std::clamp(scale, 0.5f, 4.0f);
}
float UISystem::GetDpiScale() const
{
    return m_dpiScale;
}
bool UISystem::SetScrollable(const std::string &id, const bool scrollable)
{
    UIWidget *widget = Find(id);
    if (!widget)
        return false;
    widget->scrollable = scrollable;
    return true;
}
bool UISystem::SetScrollOffset(const std::string &id, const Vec2 &offset)
{
    UIWidget *widget = Find(id);
    if (!widget || !widget->scrollable)
        return false;
    widget->scrollOffset = {std::max(0.0f, offset.x), std::max(0.0f, offset.y)};
    return true;
}
bool UISystem::Scroll(const std::string &id, const Vec2 &delta)
{
    UIWidget *widget = Find(id);
    return widget && widget->scrollable && SetScrollOffset(id, {widget->scrollOffset.x + delta.x, widget->scrollOffset.y + delta.y});
}
void UISystem::ResolveLayout()
{
    m_resolvedRects.clear();
    auto anchored = [](const UIWidget &widget, const UIRect &parent) {
        UIRect result = widget.rect;
        if (widget.anchor == UIAnchor::TopRight)
            result.x = parent.x + parent.width - widget.rect.x - widget.rect.width;
        else if (widget.anchor == UIAnchor::BottomLeft)
            result.y = parent.y + parent.height - widget.rect.y - widget.rect.height;
        else if (widget.anchor == UIAnchor::BottomRight)
        {
            result.x = parent.x + parent.width - widget.rect.x - widget.rect.width;
            result.y = parent.y + parent.height - widget.rect.y - widget.rect.height;
        }
        else if (widget.anchor == UIAnchor::Center)
        {
            result.x = parent.x + parent.width * 0.5f + widget.rect.x - widget.rect.width * 0.5f;
            result.y = parent.y + parent.height * 0.5f + widget.rect.y - widget.rect.height * 0.5f;
        }
        else if (widget.anchor == UIAnchor::Stretch)
        {
            result.x = parent.x + widget.rect.x;
            result.y = parent.y + widget.rect.y;
            result.width = std::max(0.0f, parent.width - widget.rect.x - widget.rect.width);
            result.height = std::max(0.0f, parent.height - widget.rect.y - widget.rect.height);
        }
        else
        {
            result.x += parent.x;
            result.y += parent.y;
        }
        return result;
    };
    const UIRect viewport{0.0f, 0.0f, m_viewportWidth / m_dpiScale, m_viewportHeight / m_dpiScale};
    for (const auto &widget : m_widgets)
        if (widget.parentId.empty())
            m_resolvedRects[widget.id] = anchored(widget, viewport);
    for (std::size_t pass = 0; pass < m_widgets.size(); ++pass)
        for (const auto &widget : m_widgets)
            if (!widget.parentId.empty() && m_resolvedRects.count(widget.parentId) && !m_resolvedRects.count(widget.id))
                m_resolvedRects[widget.id] = anchored(widget, m_resolvedRects[widget.parentId]);
    for (const auto &parent : m_widgets)
    {
        if (parent.layout == UILayout::Absolute || !m_resolvedRects.count(parent.id))
            continue;
        const UIRect container = m_resolvedRects[parent.id];
        float cursor = parent.padding;
        for (const auto &child : m_widgets)
        {
            if (child.parentId != parent.id || !m_resolvedRects.count(child.id))
                continue;
            UIRect &rect = m_resolvedRects[child.id];
            if (parent.layout == UILayout::Vertical)
            {
                rect.x = container.x + parent.padding;
                rect.y = container.y + cursor;
                rect.width = std::max(0.0f, container.width - parent.padding * 2.0f);
                cursor += rect.height + parent.spacing;
            }
            else
            {
                rect.x = container.x + cursor;
                rect.y = container.y + parent.padding;
                rect.height = std::max(0.0f, container.height - parent.padding * 2.0f);
                cursor += rect.width + parent.spacing;
            }
        }
    }
    for (const auto &child : m_widgets)
        if (!child.parentId.empty() && m_resolvedRects.count(child.id))
            if (const UIWidget *parent = Find(child.parentId); parent && parent->scrollable)
            {
                m_resolvedRects[child.id].x -= parent->scrollOffset.x;
                m_resolvedRects[child.id].y -= parent->scrollOffset.y;
            }
    for (auto &pair : m_resolvedRects)
    {
        pair.second.x *= m_dpiScale;
        pair.second.y *= m_dpiScale;
        pair.second.width *= m_dpiScale;
        pair.second.height *= m_dpiScale;
    }
}
void UISystem::FocusNext()
{
    std::vector<UIWidget *> focusable;
    for (auto &widget : m_widgets)
        if (widget.visible && widget.enabled &&
            (widget.type == UIWidgetType::Button || widget.type == UIWidgetType::InputField || widget.type == UIWidgetType::List ||
             widget.type == UIWidgetType::Dropdown))
            focusable.push_back(&widget);
    if (focusable.empty())
        return;
    auto found = std::find_if(focusable.begin(), focusable.end(), [&](UIWidget *widget) { return widget->id == m_focusedId; });
    UIWidget *next = found == focusable.end() || ++found == focusable.end() ? focusable.front() : *found;
    for (auto &widget : m_widgets)
        widget.focused = false;
    next->focused = true;
    m_focusedId = next->id;
}
const std::vector<UIDrawCommand> &UISystem::BuildDrawList()
{
    m_drawList.clear();
    ResolveLayout();
    for (const auto &widget : m_widgets)
    {
        if (!widget.visible)
            continue;
        Vec4 color = widget.color;
        if (!widget.enabled)
            color.w *= 0.4f;
        if (widget.focused)
        {
            color.x = std::min(1.0f, color.x + 0.2f);
            color.y = std::min(1.0f, color.y + 0.2f);
        }
        const auto rect = m_resolvedRects.find(widget.id);
        if (rect == m_resolvedRects.end())
            continue;
        UIRect clip = rect->second;
        for (const UIWidget *current = &widget; current && !current->parentId.empty();)
        {
            const auto parentRect = m_resolvedRects.find(current->parentId);
            if (parentRect == m_resolvedRects.end())
                break;
            const float right = std::min(clip.x + clip.width, parentRect->second.x + parentRect->second.width),
                        bottom = std::min(clip.y + clip.height, parentRect->second.y + parentRect->second.height);
            clip.x = std::max(clip.x, parentRect->second.x);
            clip.y = std::max(clip.y, parentRect->second.y);
            clip.width = std::max(0.0f, right - clip.x);
            clip.height = std::max(0.0f, bottom - clip.y);
            current = Find(current->parentId);
        }
        if (widget.type == UIWidgetType::ProgressBar)
        {
            m_drawList.push_back({UIWidgetType::Panel, rect->second, color, {}, {}, widget.fontSize * m_dpiScale, clip});
            UIRect fill = rect->second;
            const float range = std::max(0.0001f, widget.maximum - widget.minimum);
            fill.width *= std::clamp((widget.value - widget.minimum) / range, 0.0f, 1.0f);
            m_drawList.push_back({UIWidgetType::Panel, fill, {0.2f, 0.65f, 0.35f, color.w}, {}, {}, widget.fontSize * m_dpiScale, clip});
            continue;
        }
        std::string displayText = widget.text;
        if (widget.type == UIWidgetType::InputField)
        {
            if (displayText.empty())
                displayText = widget.placeholder;
            else if (widget.password)
                displayText.assign(displayText.size(), '*');
        }
        else if (widget.type == UIWidgetType::Dropdown && widget.selectedIndex >= 0 &&
                 widget.selectedIndex < static_cast<int>(widget.items.size()))
            displayText = widget.items[widget.selectedIndex];
        else if (widget.type == UIWidgetType::List)
            for (std::size_t index = 0; index < widget.items.size(); ++index)
                displayText +=
                    (index ? "\n" : "") + std::string(static_cast<int>(index) == widget.selectedIndex ? "> " : "  ") + widget.items[index];
        m_drawList.push_back({widget.type, rect->second, color, displayText, widget.imagePath, widget.fontSize * m_dpiScale, clip});
    }
    return m_drawList;
}
const std::string &UISystem::GetFocusedId() const
{
    return m_focusedId;
}
} // namespace MiniEngine
