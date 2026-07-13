#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <functional>
#include <string>
#include <vector>

namespace MiniEngine
{
    struct UIRect { float x = 0.0f, y = 0.0f, width = 100.0f, height = 30.0f; bool Contains(float px, float py) const { return px >= x && py >= y && px <= x + width && py <= y + height; } };
    enum class UIWidgetType { Panel, Label, Button, Image };
    struct UIDrawCommand { UIWidgetType type = UIWidgetType::Panel; UIRect rect; Vec4 color{1, 1, 1, 1}; std::string text; std::string imagePath; };

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
    };

    class UISystem
    {
    public:
        UIWidget& AddPanel(std::string id, UIRect rect, Vec4 color = {0.12f, 0.15f, 0.2f, 0.9f});
        UIWidget& AddLabel(std::string id, std::string text, UIRect rect);
        UIWidget& AddButton(std::string id, std::string text, UIRect rect, std::function<void()> onClick);
        UIWidget& AddImage(std::string id, std::string imagePath, UIRect rect);
        UIWidget* Find(const std::string& id);
        void Remove(const std::string& id);
        void Clear();
        void Update(float cursorX, float cursorY, bool primaryDown);
        void FocusNext();
        const std::vector<UIDrawCommand>& BuildDrawList();
        const std::string& GetFocusedId() const;
    private:
        std::vector<UIWidget> m_widgets;
        std::vector<UIDrawCommand> m_drawList;
        std::string m_focusedId;
        bool m_wasPrimaryDown = false;
    };
}
