// 文件：include/MiniEngine/UI/UISystem.hpp
// 作用：定义运行时 UI 控件树、布局、输入分发和绘制命令生成。
// 约定：布局和输入在 CPU 侧生成绘制命令，渲染后端不持有控件对象。
// 类型：公开声明。

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
    Image,
    InputField,
    List,
    Dropdown,
    ProgressBar
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
    std::vector<std::string> items;
    int selectedIndex = -1;
    float value = 0.0f;
    float minimum = 0.0f;
    float maximum = 1.0f;
    std::string placeholder;
    bool password = false;
    bool expanded = false;
    std::function<void(const std::string &)> onTextChanged;
    std::function<void(int)> onSelectionChanged;
    std::function<void(float)> onValueChanged;
};

class UISystem
{
  public:
    UIWidget &AddPanel(std::string id, UIRect rect, Vec4 color = {0.12f, 0.15f, 0.2f, 0.9f});
    UIWidget &AddLabel(std::string id, std::string text, UIRect rect);
    UIWidget &AddButton(std::string id, std::string text, UIRect rect, std::function<void()> onClick);
    UIWidget &AddImage(std::string id, std::string imagePath, UIRect rect);
    UIWidget &AddInputField(std::string id, std::string text, UIRect rect, std::string placeholder = {});
    UIWidget &AddList(std::string id, std::vector<std::string> items, UIRect rect);
    UIWidget &AddDropdown(std::string id, std::vector<std::string> items, UIRect rect);
    UIWidget &AddProgressBar(std::string id, float value, UIRect rect, float minimum = 0.0f, float maximum = 1.0f);
    UIWidget *Find(const std::string &id);
    void Remove(const std::string &id);
    void Clear();
    void Update(float cursorX, float cursorY, bool primaryDown);
    void FocusNext();
    void ActivateFocused();
    bool InputText(const std::string &utf8);
    bool Backspace();
    bool SelectItem(const std::string &id, int index);
    bool SetValue(const std::string &id, float value);
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
