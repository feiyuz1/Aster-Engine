// 文件：include/MiniEngine/UI/TextLayout.hpp
// 作用：完成 Unicode 字形缓存、文本塑形、双向排版、换行与富文本布局。
// 约定：布局和输入在 CPU 侧生成绘制命令，渲染后端不持有控件对象。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace MiniEngine
{
enum class TextDirection
{
    Auto,
    LeftToRight,
    RightToLeft
};

struct TextStyle
{
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float size = 18.0f;
    bool bold = false;
    bool italic = false;
};

// 排版后的字形位置使用像素坐标；cluster 保留到原始 UTF-8 文本的逻辑索引。
struct ShapedGlyph
{
    std::uint32_t codepoint = 0;
    std::size_t cluster = 0;
    float x = 0.0f;
    float y = 0.0f;
    float advance = 0.0f;
    TextStyle style;
};

struct TextLayoutResult
{
    std::vector<ShapedGlyph> glyphs;
    float width = 0.0f;
    float height = 0.0f;
    std::size_t lineCount = 0;
};

// 文本布局前端：先解码与塑形，再执行 BiDi、换行和对齐；字形纹理由 UISystem 缓存。
class TextLayout
{
  public:
    /// @brief 解码 `Utf8`。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @return 返回查询或处理得到的结果集合。
    static std::vector<std::uint32_t> DecodeUtf8(const std::string &text);
    /// @brief 编码 `Utf8`。
    /// @param codepoints Unicode 码点序列。
    /// @return 返回对应的文本或路径。
    static std::string EncodeUtf8(const std::vector<std::uint32_t> &codepoints);
    /// @brief 对 UTF-8 文本执行塑形、双向重排和自动换行。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @param maximumWidth 目标尺寸、数量、索引或限制值。
    /// @param style 控制本次操作行为的配置。
    /// @param direction 目标状态、模式或类型。
    /// @param richText 要处理的输入或输出数据。
    /// @return 返回接口计算或查询得到的结果。
    static TextLayoutResult Shape(const std::string &text, float maximumWidth, const TextStyle &style = {},
                                  TextDirection direction = TextDirection::Auto, bool richText = true);
};
} // 结束 MiniEngine 命名空间
