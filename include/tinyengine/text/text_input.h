// 文件：include/tinyengine/text/text_input.h
// 作用：在平台与 UI 之间按原始顺序传递提交和输入法预编辑事件。
#pragma once

#include <string>

namespace tinyengine
{
struct TextInputEvent
{
    enum class Type
    {
        Commit,
        Composition
    };
    Type type = Type::Commit;
    std::string text;
    int cursor = 0, length = 0;
};
} // namespace tinyengine
