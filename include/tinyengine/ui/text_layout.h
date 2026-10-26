// 文件：include/tinyengine/ui/text_layout.h
// 作用：完成 Unicode 字形缓存、文本塑形、双向排版、换行与富文本布局。
// 约定：布局和输入在 CPU 侧生成绘制命令，渲染后端不持有控件对象。
// 类型：公开声明。

#pragma once

#include "tinyengine/math/math.h"
#include "tinyengine/text/font.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tinyengine
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
    FontStyle font;
};

enum class TextAlignment
{
    Start,
    Center,
    End,
    Left,
    Right
};

struct TextLayoutOptions
{
    float maximumWidth = 0.0f;
    TextDirection direction = TextDirection::Auto;
    bool richText = true;
    TextAlignment alignment = TextAlignment::Start;
    float lineSpacing = 1.0f;
    bool wrap = true;
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
    std::shared_ptr<FontFace> face;
    std::uint32_t glyphId = 0;
    std::size_t clusterEnd = 0;
    bool rightToLeft = false;
};

struct TextBounds
{
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
};

struct TextLine
{
    std::size_t sourceBegin = 0, sourceEnd = 0;
    float y = 0.0f, baseline = 0.0f, width = 0.0f, height = 0.0f;
    bool rightToLeft = false;
};

struct TextCaret
{
    std::size_t sourceOffset = 0, line = 0;
    float x = 0.0f, y = 0.0f, height = 0.0f;
};

struct TextClusterBox
{
    std::size_t sourceBegin = 0, sourceEnd = 0, line = 0;
    TextBounds bounds;
    bool rightToLeft = false;
};

struct TextLayoutResult
{
    std::vector<ShapedGlyph> glyphs;
    float width = 0.0f;
    float height = 0.0f;
    std::size_t lineCount = 0;
    TextBounds inkBounds;
    std::vector<TextLine> lines;
    std::vector<TextCaret> carets;
    std::vector<TextClusterBox> clusters;
    bool missingGlyphs = false;

    /// @brief 将坐标映射到原始 UTF-8 字节位置，结果落在字素边界。
    std::size_t hitTest(float x, float y) const;
    TextCaret hitTestCaret(float x, float y) const;
    /// @brief 查询光标几何；换行和双向边界可用 trailing 选择后一个位置。
    TextCaret getCaret(std::size_t sourceOffset, bool trailing = false) const;
    /// @brief 查询选区的可视矩形，支持跨行和混合文字方向。
    std::vector<TextBounds> getSelectionRects(std::size_t begin, std::size_t end) const;
};

// CPU 排版结果拥有字体引用；渲染只消费 glyphId、基线坐标和样式。
class TextLayout
{
  public:
    /// @brief 解码 `Utf8`。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @return 返回查询或处理得到的结果集合。
    static std::vector<std::uint32_t> DecodeUtf8(const std::string& text);
    /// @brief 编码 `Utf8`。
    /// @param codepoints Unicode 码点序列。
    /// @return 返回对应的文本或路径。
    static std::string encodeUtf8(const std::vector<std::uint32_t>& codepoints);
    /// @brief 对 UTF-8 文本执行塑形、双向重排和自动换行。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @param maximumWidth 目标尺寸、数量、索引或限制值。
    /// @param style 控制本次操作行为的配置。
    /// @param direction 目标状态、模式或类型。
    /// @param richText 要处理的输入或输出数据。
    /// @return 返回接口计算或查询得到的结果。
    static TextLayoutResult shape(const std::string& text, float maximumWidth, const TextStyle& style = {},
                                  TextDirection direction = TextDirection::Auto, bool richText = true,
                                  FontManager* fonts = nullptr);
    /// @brief 按真实字体度量塑形、断行并生成绘制及编辑几何。
    static TextLayoutResult layout(const std::string& text, const TextLayoutOptions& options,
                                   const TextStyle& style = {}, FontManager* fonts = nullptr);
};
} // namespace tinyengine
