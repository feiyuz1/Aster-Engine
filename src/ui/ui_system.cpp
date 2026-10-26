// 文件：src/ui/ui_system.cpp
// 作用：定义运行时 UI 控件树、布局、输入分发和绘制命令生成。
// 约定：布局和输入在 CPU 侧生成绘制命令，渲染后端不持有控件对象。
// 类型：实现文件。

#include "tinyengine/ui/ui_system.h"
#include "tinyengine/text/unicode.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace tinyengine
{
UIWidget& UISystem::AddPanel(std::string id, const UIRect rect, const Vec4 color)
{
    m_widgets.push_back({std::move(id), UIWidgetType::Panel, rect, color});
    return m_widgets.back();
}
UIWidget& UISystem::AddLabel(std::string id, std::string text, const UIRect rect)
{
    m_widgets.push_back({std::move(id), UIWidgetType::Label, rect, {1, 1, 1, 1}, std::move(text)});
    return m_widgets.back();
}
UIWidget& UISystem::AddButton(std::string id, std::string text, const UIRect rect, std::function<void()> onClick)
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
UIWidget& UISystem::AddImage(std::string id, std::string imagePath, const UIRect rect)
{
    m_widgets.push_back({std::move(id), UIWidgetType::Image, rect, {1, 1, 1, 1}, {}, std::move(imagePath)});
    return m_widgets.back();
}
UIWidget& UISystem::AddInputField(std::string id, std::string text, const UIRect rect, std::string placeholder)
{
    UIWidget& widget = AddPanel(std::move(id), rect, {0.08f, 0.1f, 0.14f, 1.0f});
    widget.type = UIWidgetType::InputField;
    widget.text = std::move(text);
    widget.placeholder = std::move(placeholder);
    return widget;
}
UIWidget& UISystem::AddList(std::string id, std::vector<std::string> items, const UIRect rect)
{
    UIWidget& widget = AddPanel(std::move(id), rect, {0.1f, 0.12f, 0.16f, 1.0f});
    widget.type = UIWidgetType::List;
    widget.items = std::move(items);
    return widget;
}
UIWidget& UISystem::AddDropdown(std::string id, std::vector<std::string> items, const UIRect rect)
{
    UIWidget& widget = AddList(std::move(id), std::move(items), rect);
    widget.type = UIWidgetType::Dropdown;
    return widget;
}
UIWidget& UISystem::AddProgressBar(std::string id, const float value, const UIRect rect, const float minimum,
                                   const float maximum)
{
    UIWidget& widget = AddPanel(std::move(id), rect, {0.08f, 0.1f, 0.14f, 1.0f});
    widget.type = UIWidgetType::ProgressBar;
    widget.minimum = std::min(minimum, maximum);
    widget.maximum = std::max(minimum, maximum);
    widget.value = std::clamp(value, widget.minimum, widget.maximum);
    return widget;
}
UIWidget* UISystem::Find(const std::string& id)
{
    const auto found =
        std::find_if(m_widgets.begin(), m_widgets.end(), [&](const UIWidget& widget) { return widget.id == id; });
    return found == m_widgets.end() ? nullptr : &*found;
}
void UISystem::Remove(const std::string& id)
{
    m_textLayouts.erase(id);
    m_widgets.erase(
        std::remove_if(m_widgets.begin(), m_widgets.end(), [&](const UIWidget& widget) { return widget.id == id; }),
        m_widgets.end());
    if (m_focusedId == id)
        m_focusedId.clear();
}
void UISystem::clear()
{
    m_widgets.clear();
    m_drawList.clear();
    m_focusedId.clear();
    m_textLayouts.clear();
    m_focusedCaretRect = {0, 0, 0, 0};
}
// 输入按最终布局后的逆绘制顺序命中，保证视觉上位于上层的控件优先接收事件。
void UISystem::Update(const float cursorX, const float cursorY, const bool primaryDown)
{
    resolveLayout();
    if (primaryDown && !m_wasPrimaryDown)
    {
        for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it)
        {
            const auto rect = m_resolvedRects.find(it->id);
            const bool interactive = it->type == UIWidgetType::Button || it->type == UIWidgetType::InputField ||
                                     it->type == UIWidgetType::List || it->type == UIWidgetType::Dropdown;
            if (!it->visible || !it->enabled || !interactive || rect == m_resolvedRects.end() ||
                !rect->second.contains(cursorX, cursorY))
                continue;
            for (auto& widget : m_widgets)
            {
                widget.focused = false;
                if (widget.id != it->id)
                {
                    widget.compositionText.clear();
                    widget.compositionCursor = widget.compositionSelection = 0;
                }
            }
            it->focused = true;
            m_focusedId = it->id;
            if (it->type == UIWidgetType::InputField && !it->password)
            {
                const auto layout = layoutText(*it, it->text, rect->second.width);
                const float top = rect->second.y + std::max(0.0f, (rect->second.height - layout->height) * 0.5f);
                const auto caret = layout->hitTestCaret(cursorX - rect->second.x + it->textScrollX, cursorY - top);
                setSelection(it->id, caret.sourceOffset, caret.sourceOffset);
                setVisualCaret(*it, *layout, caret, false);
            }
            if (it->type == UIWidgetType::Dropdown)
                it->expanded = !it->expanded;
            if ((it->type == UIWidgetType::List || (it->type == UIWidgetType::Dropdown && it->expanded)) &&
                !it->items.empty())
            {
                const float itemHeight = std::max(1.0f, rect->second.height / static_cast<float>(it->items.size()));
                selectItem(it->id, std::clamp(static_cast<int>((cursorY - rect->second.y) / itemHeight), 0,
                                              static_cast<int>(it->items.size()) - 1));
            }
            if (it->onClick)
                it->onClick();
            break;
        }
    }
    m_wasPrimaryDown = primaryDown;
}
bool UISystem::inputText(const std::string& utf8)
{
    UIWidget* focused = editableFocused();
    if (!focused || utf8.empty() || !Unicode::decode(utf8).valid)
        return false;
    eraseSelection(*focused);
    focused->text.insert(focused->caretOffset, utf8);
    focused->caretOffset += utf8.size();
    focused->selectionAnchor = focused->caretOffset;
    focused->caretTrailing = focused->selectionTrailing = false;
    focused->compositionText.clear();
    if (focused->onTextChanged)
        focused->onTextChanged(focused->text);
    return true;
}
bool UISystem::backspace()
{
    UIWidget* focused = editableFocused();
    if (!focused || !focused->compositionText.empty())
        return false;
    if (!eraseSelection(*focused))
    {
        if (focused->caretOffset == 0)
            return false;
        const auto offset = Unicode::previousGrapheme(focused->text, focused->caretOffset);
        focused->text.erase(offset, focused->caretOffset - offset);
        focused->caretOffset = focused->selectionAnchor = offset;
    }
    if (focused->onTextChanged)
        focused->onTextChanged(focused->text);
    return true;
}
void UISystem::normalizeSelection(UIWidget& widget)
{
    const auto edges = Unicode::graphemeBoundaries(widget.text);
    const auto snap = [&](const std::size_t offset)
    {
        const auto found = std::upper_bound(edges.begin(), edges.end(), std::min(offset, widget.text.size()));
        return found == edges.begin() ? std::size_t{0} : *std::prev(found);
    };
    widget.caretOffset = snap(widget.caretOffset);
    widget.selectionAnchor = snap(widget.selectionAnchor);
}
UIWidget* UISystem::editableFocused()
{
    auto* widget = Find(m_focusedId);
    if (!widget || widget->type != UIWidgetType::InputField || !widget->enabled || !widget->visible)
        return nullptr;
    normalizeSelection(*widget);
    return widget;
}
bool UISystem::eraseSelection(UIWidget& widget)
{
    normalizeSelection(widget);
    const auto begin = std::min(widget.caretOffset, widget.selectionAnchor);
    const auto end = std::max(widget.caretOffset, widget.selectionAnchor);
    if (begin == end)
        return false;
    widget.text.erase(begin, end - begin);
    widget.caretOffset = widget.selectionAnchor = begin;
    widget.caretTrailing = widget.selectionTrailing = false;
    return true;
}
bool UISystem::deleteForward()
{
    auto* widget = editableFocused();
    if (!widget || !widget->compositionText.empty())
        return false;
    if (!eraseSelection(*widget))
    {
        const auto next = Unicode::nextGrapheme(widget->text, widget->caretOffset);
        if (next == widget->caretOffset)
            return false;
        widget->text.erase(widget->caretOffset, next - widget->caretOffset);
    }
    if (widget->onTextChanged)
        widget->onTextChanged(widget->text);
    return true;
}
bool UISystem::setSelection(const std::string& id, const std::size_t anchor, const std::size_t caret)
{
    auto* widget = Find(id);
    if (!widget || widget->type != UIWidgetType::InputField)
        return false;
    widget->selectionAnchor = anchor;
    widget->caretOffset = caret;
    widget->caretTrailing = widget->selectionTrailing = false;
    normalizeSelection(*widget);
    widget->compositionText.clear();
    return true;
}
bool UISystem::selectAll()
{
    auto* widget = editableFocused();
    return widget && setSelection(widget->id, 0, widget->text.size());
}
bool UISystem::moveCursor(const int direction, const bool extend)
{
    auto* widget = editableFocused();
    if (!widget || direction == 0 || !widget->compositionText.empty())
        return false;
    if (widget->password)
    {
        widget->caretOffset = direction < 0 ? Unicode::previousGrapheme(widget->text, widget->caretOffset)
                                            : Unicode::nextGrapheme(widget->text, widget->caretOffset);
    }
    else
    {
        const auto layout = layoutText(*widget, widget->text, 0.0f);
        const auto current = layout->getCaret(widget->caretOffset, widget->caretTrailing);
        if (!extend && widget->selectionAnchor != widget->caretOffset)
        {
            const auto anchor = layout->getCaret(widget->selectionAnchor, widget->selectionTrailing);
            const bool anchorFirst =
                anchor.line < current.line || (anchor.line == current.line && anchor.x < current.x);
            setVisualCaret(*widget, *layout, (direction < 0) == anchorFirst ? anchor : current, false);
            return true;
        }
        auto positions = layout->carets;
        std::stable_sort(positions.begin(), positions.end(),
                         [](const auto& first, const auto& second)
                         { return first.line == second.line ? first.x < second.x : first.line < second.line; });
        positions.erase(std::unique(positions.begin(), positions.end(),
                                    [](const auto& first, const auto& second)
                                    {
                                        return first.sourceOffset == second.sourceOffset && first.line == second.line &&
                                               std::abs(first.x - second.x) < 0.01f;
                                    }),
                        positions.end());
        auto position = std::find_if(positions.begin(), positions.end(),
                                     [&](const auto& caret)
                                     {
                                         return caret.sourceOffset == widget->caretOffset &&
                                                caret.line == current.line && std::abs(caret.x - current.x) < 0.01f;
                                     });
        if (position != positions.end())
        {
            if (direction < 0 && position != positions.begin())
                --position;
            else if (direction > 0 && std::next(position) != positions.end())
                ++position;
            setVisualCaret(*widget, *layout, *position, extend);
        }
    }
    if (!extend)
    {
        widget->selectionAnchor = widget->caretOffset;
        widget->selectionTrailing = widget->caretTrailing;
    }
    return true;
}
bool UISystem::moveCursorToLineBoundary(const bool end, const bool extend)
{
    auto* widget = editableFocused();
    if (!widget || !widget->compositionText.empty())
        return false;
    const auto layout = layoutText(*widget, widget->text, 0.0f);
    const auto caret = layout->getCaret(widget->caretOffset, widget->caretTrailing);
    setVisualCaret(*widget, *layout, layout->hitTestCaret(end ? std::numeric_limits<float>::max() : -1.0f, caret.y),
                   extend);
    return true;
}
void UISystem::setVisualCaret(UIWidget& widget, const TextLayoutResult& layout, const TextCaret& caret,
                              const bool extend)
{
    widget.caretOffset = caret.sourceOffset;
    const auto trailing = layout.getCaret(caret.sourceOffset, true);
    widget.caretTrailing = trailing.line == caret.line && std::abs(trailing.x - caret.x) < 0.01f;
    if (!extend)
    {
        widget.selectionAnchor = widget.caretOffset;
        widget.selectionTrailing = widget.caretTrailing;
    }
}
bool UISystem::setComposition(const std::string& text, const int cursor, const int selectionLength)
{
    auto* widget = editableFocused();
    if (!widget || !Unicode::decode(text).valid)
        return false;
    widget->compositionText = text;
    const auto decoded = Unicode::decode(text);
    const auto at = [&](const std::size_t index)
    { return index < decoded.codepoints.size() ? decoded.codepoints[index].byteOffset : text.size(); };
    const auto first = static_cast<std::size_t>(std::max(0, cursor));
    widget->compositionCursor = at(first);
    widget->compositionSelection =
        at(first + static_cast<std::size_t>(std::max(0, selectionLength))) - widget->compositionCursor;
    return true;
}
void UISystem::processTextEvents(const std::vector<TextInputEvent>& events)
{
    for (const auto& event : events)
        if (event.type == TextInputEvent::Type::Commit)
            inputText(event.text);
        else
            setComposition(event.text, event.cursor, event.length);
}
void UISystem::setFontManager(FontManager* fonts)
{
    m_fonts = fonts;
    m_textLayouts.clear();
}
FontManager* UISystem::getFontManager() const
{
    return m_fonts ? m_fonts : &FontManager::defaultManager();
}
UIRect UISystem::getFocusedCaretRect() const
{
    return m_focusedCaretRect;
}
std::size_t UISystem::getTextLayoutBuildCount() const
{
    return m_textLayoutBuildCount;
}
std::shared_ptr<const TextLayoutResult> UISystem::layoutText(const UIWidget& widget, const std::string& text,
                                                             const float width)
{
    TextStyle style;
    style.size = widget.fontSize * m_dpiScale;
    style.font = widget.font;
    TextLayoutOptions options;
    options.maximumWidth = width;
    if (widget.type == UIWidgetType::InputField)
    {
        options.wrap = false;
        if (options.maximumWidth <= 0)
        {
            const auto found = m_resolvedRects.find(widget.id);
            options.maximumWidth =
                found == m_resolvedRects.end() ? widget.rect.width * m_dpiScale : found->second.width;
        }
    }
    options.direction = widget.textDirection;
    options.alignment = widget.type == UIWidgetType::Button && widget.textAlignment == TextAlignment::Start
                            ? TextAlignment::Center
                            : widget.textAlignment;
    options.richText = widget.richText && widget.type != UIWidgetType::InputField;
    std::ostringstream key;
    key << std::setprecision(std::numeric_limits<float>::max_digits10) << std::quoted(text) << ':' << style.size << ':'
        << std::quoted(style.font.family) << ':' << style.font.weight << ':' << style.font.italic << ':'
        << std::quoted(style.font.language) << ':' << options.maximumWidth << ':' << static_cast<int>(options.direction)
        << ':' << static_cast<int>(options.alignment) << ':' << options.richText << ':' << options.wrap << ':'
        << getFontManager()->getRevision();
    auto& cached = m_textLayouts[widget.id];
    const auto cacheKey = key.str();
    if (!cached.layout || cached.key != cacheKey)
    {
        cached.layout = std::make_shared<TextLayoutResult>(TextLayout::layout(text, options, style, getFontManager()));
        cached.key = cacheKey;
        ++m_textLayoutBuildCount;
    }
    return cached.layout;
}
bool UISystem::selectItem(const std::string& id, const int index)
{
    UIWidget* widget = Find(id);
    if (!widget || (widget->type != UIWidgetType::List && widget->type != UIWidgetType::Dropdown) || index < 0 ||
        index >= static_cast<int>(widget->items.size()))
        return false;
    widget->selectedIndex = index;
    if (widget->onSelectionChanged)
        widget->onSelectionChanged(index);
    return true;
}
bool UISystem::setValue(const std::string& id, const float value)
{
    UIWidget* widget = Find(id);
    if (!widget || widget->type != UIWidgetType::ProgressBar)
        return false;
    widget->value = std::clamp(value, widget->minimum, widget->maximum);
    if (widget->onValueChanged)
        widget->onValueChanged(widget->value);
    return true;
}
void UISystem::activateFocused()
{
    if (auto* focused = Find(m_focusedId); focused && focused->visible && focused->enabled && focused->onClick)
        focused->onClick();
}
bool UISystem::setParent(const std::string& childId, const std::string& parentId)
{
    UIWidget* child = Find(childId);
    UIWidget* parent = Find(parentId);
    if (!child || !parent || child == parent)
        return false;
    for (const UIWidget* ancestor = parent; ancestor && !ancestor->parentId.empty();
         ancestor = Find(ancestor->parentId))
        if (ancestor == child)
            return false;
    child->parentId = parentId;
    return true;
}
bool UISystem::setLayout(const std::string& id, const UILayout layout, const float spacing, const float padding)
{
    UIWidget* widget = Find(id);
    if (!widget)
        return false;
    widget->layout = layout;
    widget->spacing = std::max(0.0f, spacing);
    widget->padding = std::max(0.0f, padding);
    return true;
}
bool UISystem::setAnchor(const std::string& id, const UIAnchor anchor)
{
    UIWidget* widget = Find(id);
    if (!widget)
        return false;
    widget->anchor = anchor;
    return true;
}
void UISystem::setViewportSize(const float width, const float height)
{
    m_viewportWidth = std::max(1.0f, width);
    m_viewportHeight = std::max(1.0f, height);
}
void UISystem::setDpiScale(const float scale)
{
    m_dpiScale = std::clamp(scale, 0.5f, 4.0f);
}
float UISystem::getDpiScale() const
{
    return m_dpiScale;
}
bool UISystem::setScrollable(const std::string& id, const bool scrollable)
{
    UIWidget* widget = Find(id);
    if (!widget)
        return false;
    widget->scrollable = scrollable;
    return true;
}
bool UISystem::setScrollOffset(const std::string& id, const Vec2& offset)
{
    UIWidget* widget = Find(id);
    if (!widget || !widget->scrollable)
        return false;
    widget->scrollOffset = {std::max(0.0f, offset.x), std::max(0.0f, offset.y)};
    return true;
}
bool UISystem::Scroll(const std::string& id, const Vec2& delta)
{
    UIWidget* widget = Find(id);
    return widget && widget->scrollable &&
           setScrollOffset(id, {widget->scrollOffset.x + delta.x, widget->scrollOffset.y + delta.y});
}
void UISystem::resolveLayout()
{
    m_resolvedRects.clear();
    auto anchored = [](const UIWidget& widget, const UIRect& parent)
    {
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
    for (const auto& widget : m_widgets)
        if (widget.parentId.empty())
            m_resolvedRects[widget.id] = anchored(widget, viewport);
    for (std::size_t pass = 0; pass < m_widgets.size(); ++pass)
        for (const auto& widget : m_widgets)
            if (!widget.parentId.empty() && m_resolvedRects.count(widget.parentId) && !m_resolvedRects.count(widget.id))
                m_resolvedRects[widget.id] = anchored(widget, m_resolvedRects[widget.parentId]);
    for (const auto& parent : m_widgets)
    {
        if (parent.layout == UILayout::Absolute || !m_resolvedRects.count(parent.id))
            continue;
        const UIRect container = m_resolvedRects[parent.id];
        float cursor = parent.padding;
        for (const auto& child : m_widgets)
        {
            if (child.parentId != parent.id || !m_resolvedRects.count(child.id))
                continue;
            UIRect& rect = m_resolvedRects[child.id];
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
    for (const auto& child : m_widgets)
        if (!child.parentId.empty() && m_resolvedRects.count(child.id))
            if (const UIWidget* parent = Find(child.parentId); parent && parent->scrollable)
            {
                m_resolvedRects[child.id].x -= parent->scrollOffset.x;
                m_resolvedRects[child.id].y -= parent->scrollOffset.y;
            }
    for (auto& pair : m_resolvedRects)
    {
        pair.second.x *= m_dpiScale;
        pair.second.y *= m_dpiScale;
        pair.second.width *= m_dpiScale;
        pair.second.height *= m_dpiScale;
    }
}
void UISystem::focusNext()
{
    std::vector<UIWidget*> focusable;
    for (auto& widget : m_widgets)
        if (widget.visible && widget.enabled &&
            (widget.type == UIWidgetType::Button || widget.type == UIWidgetType::InputField ||
             widget.type == UIWidgetType::List || widget.type == UIWidgetType::Dropdown))
            focusable.push_back(&widget);
    if (focusable.empty())
        return;
    auto found =
        std::find_if(focusable.begin(), focusable.end(), [&](UIWidget* widget) { return widget->id == m_focusedId; });
    UIWidget* next = found == focusable.end() || ++found == focusable.end() ? focusable.front() : *found;
    for (auto& widget : m_widgets)
    {
        widget.focused = false;
        widget.compositionText.clear();
        widget.compositionCursor = widget.compositionSelection = 0;
    }
    next->focused = true;
    m_focusedId = next->id;
}
const std::vector<UIDrawCommand>& UISystem::BuildDrawList()
{
    m_drawList.clear();
    m_focusedCaretRect = {0, 0, 0, 0};
    resolveLayout();
    for (auto& widget : m_widgets)
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
        for (const UIWidget* current = &widget; current && !current->parentId.empty();)
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
        if (clip.width <= 0.0f || clip.height <= 0.0f)
            continue;
        if (widget.type == UIWidgetType::ProgressBar)
        {
            m_drawList.push_back(
                {UIWidgetType::Panel, rect->second, color, {}, {}, widget.fontSize * m_dpiScale, clip});
            UIRect fill = rect->second;
            const float range = std::max(0.0001f, widget.maximum - widget.minimum);
            fill.width *= std::clamp((widget.value - widget.minimum) / range, 0.0f, 1.0f);
            m_drawList.push_back(
                {UIWidgetType::Panel, fill, {0.2f, 0.65f, 0.35f, color.w}, {}, {}, widget.fontSize * m_dpiScale, clip});
            continue;
        }
        std::string displayText = widget.text;
        std::vector<std::size_t> passwordEdges;
        std::size_t caretPosition = 0, selectionBegin = 0, selectionEnd = 0;
        std::size_t compositionBegin = 0, compositionEnd = 0;
        if (widget.type == UIWidgetType::InputField)
        {
            normalizeSelection(widget);
            caretPosition = widget.caretOffset;
            selectionBegin = std::min(widget.caretOffset, widget.selectionAnchor);
            selectionEnd = std::max(widget.caretOffset, widget.selectionAnchor);
            if (widget.focused && !widget.compositionText.empty())
            {
                displayText.replace(selectionBegin, selectionEnd - selectionBegin, widget.compositionText);
                compositionBegin = selectionBegin;
                compositionEnd = selectionBegin + widget.compositionText.size();
                caretPosition = selectionBegin + widget.compositionCursor;
                selectionBegin = caretPosition;
                selectionEnd = caretPosition + widget.compositionSelection;
            }
            if (widget.password)
            {
                passwordEdges = Unicode::graphemeBoundaries(displayText);
                const auto masked = [&](const std::size_t offset)
                {
                    return static_cast<std::size_t>(
                        std::lower_bound(passwordEdges.begin(), passwordEdges.end(), offset) - passwordEdges.begin());
                };
                caretPosition = masked(caretPosition);
                selectionBegin = masked(selectionBegin);
                selectionEnd = masked(selectionEnd);
                compositionBegin = masked(compositionBegin);
                compositionEnd = masked(compositionEnd);
                displayText.assign(passwordEdges.size() - 1, '*');
            }
            if (displayText.empty())
                displayText = widget.placeholder;
        }
        else if (widget.type == UIWidgetType::Dropdown && widget.selectedIndex >= 0 &&
                 widget.selectedIndex < static_cast<int>(widget.items.size()))
            displayText = widget.items[widget.selectedIndex];
        else if (widget.type == UIWidgetType::List)
            for (std::size_t index = 0; index < widget.items.size(); ++index)
                displayText += (index ? "\n" : "") +
                               std::string(static_cast<int>(index) == widget.selectedIndex ? "> " : "  ") +
                               widget.items[index];
        m_drawList.push_back(
            {widget.type, rect->second, color, displayText, widget.imagePath, widget.fontSize * m_dpiScale, clip});
        auto& command = m_drawList.back();
        command.font = widget.font;
        command.textColor = widget.textColor;
        command.font.size = command.fontSize;
        if (!displayText.empty() || widget.type == UIWidgetType::InputField)
        {
            command.textLayout = layoutText(widget, displayText, command.rect.width);
            const auto& layout = *command.textLayout;
            const float vertical = std::max(0.0f, (command.rect.height - layout.height) * 0.5f);
            if (widget.type == UIWidgetType::InputField)
            {
                widget.textScrollX = std::clamp(widget.textScrollX, 0.0f,
                                                std::max(0.0f, layout.width - command.rect.width + 3.0f * m_dpiScale));
                command.textOffset.x = -widget.textScrollX;
            }
            if (widget.type == UIWidgetType::InputField && widget.focused && widget.enabled)
            {
                const auto caret = layout.getCaret(caretPosition, widget.caretTrailing);
                const float margin = 3.0f * m_dpiScale;
                if (caret.x - widget.textScrollX > command.rect.width - margin)
                    widget.textScrollX = caret.x - std::max(0.0f, command.rect.width - margin);
                else if (caret.x - widget.textScrollX < margin)
                    widget.textScrollX = std::max(0.0f, caret.x - margin);
                command.textOffset.x = -widget.textScrollX;
                const auto toRect = [&](const TextBounds& bounds)
                {
                    return UIRect{command.rect.x + command.textOffset.x + bounds.x,
                                  command.rect.y + vertical + bounds.y, bounds.width, bounds.height};
                };
                for (const auto& bounds : layout.getSelectionRects(selectionBegin, selectionEnd))
                    command.selectionRects.push_back(toRect(bounds));
                for (const auto& bounds : layout.getSelectionRects(compositionBegin, compositionEnd))
                {
                    auto underline = toRect(bounds);
                    underline.y += std::max(0.0f, underline.height - 2.0f * m_dpiScale);
                    underline.height = m_dpiScale;
                    command.compositionRects.push_back(underline);
                }
                command.caretRect = {command.rect.x + command.textOffset.x + caret.x,
                                     command.rect.y + vertical + caret.y, std::max(1.0f, m_dpiScale), caret.height};
                m_focusedCaretRect = command.caretRect;
            }
        }
    }
    return m_drawList;
}
const std::string& UISystem::GetFocusedId() const
{
    return m_focusedId;
}
} // namespace tinyengine
