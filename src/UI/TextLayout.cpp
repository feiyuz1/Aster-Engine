// 文件：src/UI/TextLayout.cpp
// 作用：完成 Unicode 字形缓存、文本塑形、双向排版、换行与富文本布局。
// 约定：布局和输入在 CPU 侧生成绘制命令，渲染后端不持有控件对象。
// 类型：实现文件。

#include "MiniEngine/UI/TextLayout.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace MiniEngine
{
namespace
{
bool IsRtl(const std::uint32_t codepoint)
{
    return (codepoint >= 0x0590 && codepoint <= 0x08FF) || (codepoint >= 0xFB1D && codepoint <= 0xFEFC);
}

float GlyphAdvance(const std::uint32_t codepoint, const TextStyle &style)
{
    if (codepoint == ' ')
        return style.size * 0.34f;
    if (codepoint >= 0x2E80 || IsRtl(codepoint))
        return style.size;
    return style.size * (style.bold ? 0.64f : 0.60f);
}

std::uint32_t Ligature(const std::uint32_t first, const std::uint32_t second)
{
    if (first == 'f' && second == 'i')
        return 0xFB01;
    if (first == 'f' && second == 'l')
        return 0xFB02;
    return 0;
}

bool ParseColor(const std::string &value, Vec4 &color)
{
    if (value.size() != 7 || value.front() != '#')
        return false;
    char *end = nullptr;
    const unsigned long rgb = std::strtoul(value.c_str() + 1, &end, 16);
    if (!end || *end != '\0')
        return false;
    color = {static_cast<float>((rgb >> 16u) & 255u) / 255.0f, static_cast<float>((rgb >> 8u) & 255u) / 255.0f,
             static_cast<float>(rgb & 255u) / 255.0f, color.w};
    return true;
}
} // namespace

// 无效 UTF-8 序列替换为 U+FFFD 并至少前进一个字节，既保留后续文本也避免死循环。
std::vector<std::uint32_t> TextLayout::DecodeUtf8(const std::string &text)
{
    std::vector<std::uint32_t> result;
    for (std::size_t index = 0; index < text.size();)
    {
        const auto first = static_cast<std::uint8_t>(text[index]);
        std::uint32_t codepoint = 0xFFFD;
        std::size_t count = 1;
        if (first < 0x80)
            codepoint = first;
        else if ((first & 0xE0u) == 0xC0u && index + 1 < text.size())
        {
            codepoint = ((first & 0x1Fu) << 6u) | (static_cast<std::uint8_t>(text[index + 1]) & 0x3Fu);
            count = 2;
        }
        else if ((first & 0xF0u) == 0xE0u && index + 2 < text.size())
        {
            codepoint = ((first & 0x0Fu) << 12u) | ((static_cast<std::uint8_t>(text[index + 1]) & 0x3Fu) << 6u) |
                        (static_cast<std::uint8_t>(text[index + 2]) & 0x3Fu);
            count = 3;
        }
        else if ((first & 0xF8u) == 0xF0u && index + 3 < text.size())
        {
            codepoint = ((first & 0x07u) << 18u) | ((static_cast<std::uint8_t>(text[index + 1]) & 0x3Fu) << 12u) |
                        ((static_cast<std::uint8_t>(text[index + 2]) & 0x3Fu) << 6u) | (static_cast<std::uint8_t>(text[index + 3]) & 0x3Fu);
            count = 4;
        }
        if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            codepoint = 0xFFFD;
        result.push_back(codepoint);
        index += count;
    }
    return result;
}

std::string TextLayout::EncodeUtf8(const std::vector<std::uint32_t> &codepoints)
{
    std::string result;
    for (const std::uint32_t codepoint : codepoints)
        if (codepoint <= 0x7F)
            result.push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7FF)
        {
            result.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
            result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
        else if (codepoint <= 0xFFFF)
        {
            result.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
            result.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
        else
        {
            result.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
            result.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            result.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
    return result;
}

// 富文本样式只影响显式范围；形状与 BiDi 重排后再断行，保持 cluster 可追溯到逻辑文本。
TextLayoutResult TextLayout::Shape(const std::string &source, const float maximumWidth, const TextStyle &baseStyle,
                                   const TextDirection requestedDirection, const bool richText)
{
    struct StyledCodepoint
    {
        std::uint32_t value;
        std::size_t cluster;
        TextStyle style;
    };
    std::vector<StyledCodepoint> logical;
    std::vector<TextStyle> styles{baseStyle};
    for (std::size_t index = 0; index < source.size();)
    {
        if (richText && source[index] == '[')
        {
            const std::size_t close = source.find(']', index);
            if (close != std::string::npos)
            {
                const std::string tag = source.substr(index + 1, close - index - 1);
                if (tag == "b" || tag == "i" || tag.rfind("color=", 0) == 0 || tag.rfind("size=", 0) == 0)
                {
                    TextStyle next = styles.back();
                    if (tag == "b")
                        next.bold = true;
                    else if (tag == "i")
                        next.italic = true;
                    else if (tag.rfind("color=", 0) == 0)
                        ParseColor(tag.substr(6), next.color);
                    else
                        next.size = std::clamp(std::strtof(tag.c_str() + 5, nullptr), 6.0f, 256.0f);
                    styles.push_back(next);
                    index = close + 1;
                    continue;
                }
                if (!tag.empty() && tag.front() == '/' && styles.size() > 1)
                {
                    styles.pop_back();
                    index = close + 1;
                    continue;
                }
            }
        }
        std::size_t count = 1;
        const auto decoded = DecodeUtf8(source.substr(index, 4));
        const std::uint8_t byte = static_cast<std::uint8_t>(source[index]);
        if ((byte & 0xE0u) == 0xC0u)
            count = 2;
        else if ((byte & 0xF0u) == 0xE0u)
            count = 3;
        else if ((byte & 0xF8u) == 0xF0u)
            count = 4;
        logical.push_back({decoded.empty() ? 0xFFFDu : decoded.front(), index, styles.back()});
        index = std::min(source.size(), index + count);
    }
    TextDirection direction = requestedDirection;
    if (direction == TextDirection::Auto)
    {
        direction = TextDirection::LeftToRight;
        for (const auto &item : logical)
        {
            if (IsRtl(item.value))
            {
                direction = TextDirection::RightToLeft;
                break;
            }
            if ((item.value >= 'A' && item.value <= 'Z') || (item.value >= 'a' && item.value <= 'z'))
                break;
        }
    }
    for (std::size_t begin = 0; begin < logical.size();)
    {
        const bool rtl = IsRtl(logical[begin].value);
        std::size_t end = begin + 1;
        while (end < logical.size() && IsRtl(logical[end].value) == rtl && logical[end].value != '\n')
            ++end;
        if (rtl)
            std::reverse(logical.begin() + static_cast<std::ptrdiff_t>(begin), logical.begin() + static_cast<std::ptrdiff_t>(end));
        begin = end;
    }
    if (direction == TextDirection::RightToLeft)
        std::reverse(logical.begin(), logical.end());
    TextLayoutResult result;
    float x = 0.0f, y = 0.0f, lineHeight = baseStyle.size * 1.25f;
    result.lineCount = 1;
    for (std::size_t index = 0; index < logical.size(); ++index)
    {
        auto item = logical[index];
        if (item.value == '\n')
        {
            result.width = std::max(result.width, x);
            x = 0.0f;
            y += lineHeight;
            lineHeight = baseStyle.size * 1.25f;
            ++result.lineCount;
            continue;
        }
        if (index + 1 < logical.size())
            if (const std::uint32_t ligature = Ligature(item.value, logical[index + 1].value))
            {
                item.value = ligature;
                ++index;
            }
        const float advance = GlyphAdvance(item.value, item.style);
        if (maximumWidth > 0.0f && x > 0.0f && x + advance > maximumWidth)
        {
            result.width = std::max(result.width, x);
            x = 0.0f;
            y += lineHeight;
            lineHeight = baseStyle.size * 1.25f;
            ++result.lineCount;
        }
        result.glyphs.push_back({item.value, item.cluster, x, y, advance, item.style});
        x += advance;
        lineHeight = std::max(lineHeight, item.style.size * 1.25f);
    }
    result.width = std::max(result.width, x);
    result.height = y + lineHeight;
    return result;
}
} // namespace MiniEngine
