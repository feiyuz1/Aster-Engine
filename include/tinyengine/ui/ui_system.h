// 文件：include/tinyengine/ui/ui_system.h
// 作用：定义运行时 UI 控件树、布局、输入分发和绘制命令生成。
// 约定：布局和输入在 CPU 侧生成绘制命令，渲染后端不持有控件对象。
// 类型：公开声明。

#pragma once

#include "tinyengine/math/math.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tinyengine
{
struct UIRect
{
    float x = 0.0f, y = 0.0f, width = 100.0f, height = 30.0f;
    /// @brief 判断集合是否包含目标元素。
    /// @param px 接口所需的输入参数。
    /// @param py 接口所需的输入参数。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool contains(float px, float py) const
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
    std::function<void(const std::string&)> onTextChanged;
    std::function<void(int)> onSelectionChanged;
    std::function<void(float)> onValueChanged;
};

class UISystem
{
  public:
    /// @brief 添加 `Panel`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param rect 控件或绘制区域矩形。
    /// @param color 接口所需的输入参数。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    UIWidget& AddPanel(std::string id, UIRect rect, Vec4 color = {0.12f, 0.15f, 0.2f, 0.9f});
    /// @brief 添加 `Label`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @param rect 控件或绘制区域矩形。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    UIWidget& AddLabel(std::string id, std::string text, UIRect rect);
    /// @brief 添加 `Button`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @param rect 控件或绘制区域矩形。
    /// @param onClick 操作过程中调用的回调函数。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    UIWidget& AddButton(std::string id, std::string text, UIRect rect, std::function<void()> onClick);
    /// @brief 添加 `Image`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param imagePath 相关文件、资源或目录路径。
    /// @param rect 控件或绘制区域矩形。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    UIWidget& AddImage(std::string id, std::string imagePath, UIRect rect);
    /// @brief 添加 `InputField`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @param rect 控件或绘制区域矩形。
    /// @param placeholder 接口所需的输入参数。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    UIWidget& AddInputField(std::string id, std::string text, UIRect rect, std::string placeholder = {});
    /// @brief 添加 `List`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param items 要处理的元素集合。
    /// @param rect 控件或绘制区域矩形。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    UIWidget& AddList(std::string id, std::vector<std::string> items, UIRect rect);
    /// @brief 添加 `Dropdown`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param items 要处理的元素集合。
    /// @param rect 控件或绘制区域矩形。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    UIWidget& AddDropdown(std::string id, std::vector<std::string> items, UIRect rect);
    /// @brief 添加 `ProgressBar`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param value 要写入、比较或计算的值。
    /// @param rect 控件或绘制区域矩形。
    /// @param minimum 目标尺寸、数量、索引或限制值。
    /// @param maximum 目标尺寸、数量、索引或限制值。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    UIWidget& AddProgressBar(std::string id, float value, UIRect rect, float minimum = 0.0f, float maximum = 1.0f);
    /// @brief 查找匹配的对象。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    UIWidget* Find(const std::string& id);
    /// @brief 移除指定元素。
    /// @param id 目标对象、消息或任务标识符。
    void Remove(const std::string& id);
    /// @brief 清空对象保存的状态和数据。
    void clear();
    /// @brief 更新对象当前状态。
    /// @param cursorX 接口所需的输入参数。
    /// @param cursorY 接口所需的输入参数。
    /// @param primaryDown 接口所需的输入参数。
    void Update(float cursorX, float cursorY, bool primaryDown);
    /// @brief 切换焦点到 `Next`。
    void focusNext();
    /// @brief 激活 `Focused`。
    void activateFocused();
    /// @brief 输入 `Text`。
    /// @param utf8 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool inputText(const std::string& utf8);
    /// @brief 删除输入文本中的前一个 Unicode 字符。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool backspace();
    /// @brief 选择 `Item`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param index 目标元素或骨骼索引。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool selectItem(const std::string& id, int index);
    /// @brief 设置 `Value`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param value 要写入、比较或计算的值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool setValue(const std::string& id, float value);
    /// @brief 设置 `Parent`。
    /// @param childId 相关实体句柄。
    /// @param parentId 相关实体句柄。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool setParent(const std::string& childId, const std::string& parentId);
    /// @brief 设置 `Layout`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param layout 控制本次操作行为的配置。
    /// @param spacing 布局元素之间的间距。
    /// @param padding 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool setLayout(const std::string& id, UILayout layout, float spacing = 4.0f, float padding = 4.0f);
    /// @brief 设置 `Anchor`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param anchor 目标状态、模式或类型。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool setAnchor(const std::string& id, UIAnchor anchor);
    /// @brief 设置 `ViewportSize`。
    /// @param width 目标宽度。
    /// @param height 目标高度。
    void setViewportSize(float width, float height);
    /// @brief 设置 `DpiScale`。
    /// @param scale 缩放比例或缩放向量。
    void setDpiScale(float scale);
    /// @brief 获取 `DpiScale`。
    /// @return 返回计算得到的数值、数量或索引。
    float getDpiScale() const;
    /// @brief 设置 `Scrollable`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param scrollable 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool setScrollable(const std::string& id, bool scrollable);
    /// @brief 设置 `ScrollOffset`。
    /// @param id 目标对象、消息或任务标识符。
    /// @param offset 数据、文件或滚动位置偏移。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool setScrollOffset(const std::string& id, const Vec2& offset);
    /// @brief 调整指定可滚动控件的滚动偏移。
    /// @param id 目标对象、消息或任务标识符。
    /// @param delta 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Scroll(const std::string& id, const Vec2& delta);
    /// @brief 构建 `DrawList`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<UIDrawCommand>& BuildDrawList();
    /// @brief 获取 `FocusedId`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string& GetFocusedId() const;

  private:
    std::vector<UIWidget> m_widgets;
    std::vector<UIDrawCommand> m_drawList;
    std::string m_focusedId;
    std::unordered_map<std::string, UIRect> m_resolvedRects;
    float m_viewportWidth = 1280.0f;
    float m_viewportHeight = 720.0f;
    bool m_wasPrimaryDown = false;
    float m_dpiScale = 1.0f;

    /// @brief 解析 `Layout`。
    void resolveLayout();
};
} // namespace tinyengine
