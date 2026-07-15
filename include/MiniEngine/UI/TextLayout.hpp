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

class TextLayout
{
  public:
    static std::vector<std::uint32_t> DecodeUtf8(const std::string &text);
    static std::string EncodeUtf8(const std::vector<std::uint32_t> &codepoints);
    static TextLayoutResult Shape(const std::string &text, float maximumWidth, const TextStyle &style = {},
                                  TextDirection direction = TextDirection::Auto, bool richText = true);
};
} // namespace MiniEngine
