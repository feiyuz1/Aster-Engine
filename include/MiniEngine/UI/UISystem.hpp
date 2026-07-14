#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
struct UIRect
{
    float x = 0.0f, y = 0.0f, width = 100.0f, height = 30.0f;
    bool Contains(float px, float py) const
    {
        return px >= x && py >= y && px <= x + width && py <= y + height;
    }
};
enum class UIWidgetType
{
    Panel,
    Label,
    Button,
    Image
};
enum class UIAnchor
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Center,
    Stretch
};
enum class UILayout
{
    Absolute,
    Horizontal,
    Vertical
};
struct UIDrawCommand
{
    UIWidgetType type = UIWidgetType::Panel;
    UIRect rect;
    Vec4 color{1, 1, 1, 1};
    std::string text;
    std::string imagePath;
    float fontSize = 18.0f;
    UIRect clipRect;
};

struct UIWidget
{
    std::string id;
    UIWidgetType type = UIWidgetType::Panel;
    UIRect rect;
    Vec4 color{0.12f, 0.15f, 0.2f, 0.9f};
    std::string text;
    std::string imagePath;
    bool visible = true;
    bool enabled = true;
    bool focused = false;
    std::function<void()> onClick;
    UIAnchor anchor = UIAnchor::TopLeft;
    UILayout layout = UILayout::Absolute;
    std::string parentId;
    float spacing = 4.0f;
    float padding = 4.0f;
    float fontSize = 18.0f;
    bool scrollable = false;
    Vec2 scrollOffset{};
};

class UISystem
{
  public:
    UIWidget &AddPanel(std::string id, UIRect rect, Vec4 color = {0.12f, 0.15f, 0.2f, 0.9f});
    UIWidget &AddLabel(std::string id, std::string text, UIRect rect);
    UIWidget &AddButton(std::string id, std::string text, UIRect rect, std::function<void()> onClick);
    UIWidget &AddImage(std::string id, std::string imagePath, UIRect rect);
    UIWidget *Find(const std::string &id);
    void Remove(const std::string &id);
    void Clear();
    void Update(float cursorX, float cursorY, bool primaryDown);
    void FocusNext();
    void ActivateFocused();
    bool SetParent(const std::string &childId, const std::string &parentId);
    bool SetLayout(const std::string &id, UILayout layout, float spacing = 4.0f, float padding = 4.0f);
    bool SetAnchor(const std::string &id, UIAnchor anchor);
    void SetViewportSize(float width, float height);
    void SetDpiScale(float scale);
    float GetDpiScale() const;
    bool SetScrollable(const std::string &id, bool scrollable);
    bool SetScrollOffset(const std::string &id, const Vec2 &offset);
    bool Scroll(const std::string &id, const Vec2 &delta);
    const std::vector<UIDrawCommand> &BuildDrawList();
    const std::string &GetFocusedId() const;

  private:
    std::vector<UIWidget> m_widgets;
    std::vector<UIDrawCommand> m_drawList;
    std::string m_focusedId;
    std::unordered_map<std::string, UIRect> m_resolvedRects;
    float m_viewportWidth = 1280.0f;
    float m_viewportHeight = 720.0f;
    bool m_wasPrimaryDown = false;
    float m_dpiScale = 1.0f;

    void ResolveLayout();
};
} // namespace MiniEngine
