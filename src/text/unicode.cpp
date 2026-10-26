// 文件：src/text/unicode.cpp
// 作用：UTF-8 校验、ICU 边界分析和原始字节索引映射。
#include "tinyengine/text/unicode.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <unicode/ubrk.h>

namespace tinyengine
{
UnicodeText Unicode::decode(const std::string_view text)
{
    UnicodeText result;
    for (std::size_t offset = 0; offset < text.size();)
    {
        const auto first = static_cast<unsigned char>(text[offset]);
        std::size_t length = first < 0x80                     ? 1
                             : first >= 0xc2 && first <= 0xdf ? 2
                             : first >= 0xe0 && first <= 0xef ? 3
                             : first >= 0xf0 && first <= 0xf4 ? 4
                                                              : 0;
        std::uint32_t value = length == 1   ? first
                              : length == 2 ? first & 0x1fu
                              : length == 3 ? first & 0x0fu
                                            : first & 0x07u;
        bool valid = length > 0 && length <= text.size() - offset;
        for (std::size_t index = 1; valid && index < length; ++index)
        {
            const auto byte = static_cast<unsigned char>(text[offset + index]);
            valid = (byte & 0xc0u) == 0x80u;
            value = (value << 6u) | (byte & 0x3fu);
        }
        valid = valid && !(length == 2 && value < 0x80) && !(length == 3 && value < 0x800) &&
                !(length == 4 && value < 0x10000) && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
        if (!valid)
        {
            result.valid = false;
            value = 0xfffd;
            length = 1;
        }
        result.codepoints.push_back({value, offset, length, result.utf16.size()});
        result.utf16ToByte.push_back(offset);
        if (value < 0x10000)
            result.utf16.push_back(static_cast<char16_t>(value));
        else
        {
            result.utf16.push_back(static_cast<char16_t>(0xd800u + ((value - 0x10000u) >> 10u)));
            result.utf16ToByte.push_back(offset);
            result.utf16.push_back(static_cast<char16_t>(0xdc00u + ((value - 0x10000u) & 0x3ffu)));
        }
        offset += length;
    }
    result.utf16ToByte.push_back(text.size());
    return result;
}

std::string Unicode::encode(const std::vector<std::uint32_t>& codepoints)
{
    std::string result;
    for (auto value : codepoints)
    {
        if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            value = 0xfffd;
        if (value <= 0x7f)
            result.push_back(static_cast<char>(value));
        else if (value <= 0x7ff)
        {
            result.push_back(static_cast<char>(0xc0u | (value >> 6u)));
            result.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
        }
        else if (value <= 0xffff)
        {
            result.push_back(static_cast<char>(0xe0u | (value >> 12u)));
            result.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3fu)));
            result.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
        }
        else
        {
            result.push_back(static_cast<char>(0xf0u | (value >> 18u)));
            result.push_back(static_cast<char>(0x80u | ((value >> 12u) & 0x3fu)));
            result.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3fu)));
            result.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
        }
    }
    return result;
}

namespace
{
std::vector<std::size_t> boundaries(const std::string_view text, const std::string_view locale,
                                    const UBreakIteratorType type)
{
    const auto decoded = Unicode::decode(text);
    std::vector<std::size_t> result{0};
    if (decoded.utf16.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return {0, text.size()};
    UErrorCode error = U_ZERO_ERROR;
    const std::string language(locale);
    std::unique_ptr<UBreakIterator, decltype(&ubrk_close)> iterator(
        ubrk_open(type, language.c_str(), reinterpret_cast<const UChar*>(decoded.utf16.data()),
                  static_cast<std::int32_t>(decoded.utf16.size()), &error),
        ubrk_close);
    if (U_SUCCESS(error) && iterator)
        for (auto position = ubrk_first(iterator.get()); position != UBRK_DONE; position = ubrk_next(iterator.get()))
        {
            const auto byte = decoded.utf16ToByte[static_cast<std::size_t>(position)];
            if (result.back() != byte)
                result.push_back(byte);
        }
    else
        for (const auto& point : decoded.codepoints)
            if (point.byteOffset != result.back())
                result.push_back(point.byteOffset);
    if (result.back() != text.size())
        result.push_back(text.size());
    return result;
}
} // namespace

std::vector<std::size_t> Unicode::graphemeBoundaries(const std::string_view text, const std::string_view locale)
{
    return boundaries(text, locale, UBRK_CHARACTER);
}
std::vector<std::size_t> Unicode::lineBreaks(const std::string_view text, const std::string_view locale)
{
    return boundaries(text, locale, UBRK_LINE);
}
std::size_t Unicode::previousGrapheme(const std::string_view text, const std::size_t byteOffset)
{
    const auto edges = graphemeBoundaries(text);
    const auto found = std::lower_bound(edges.begin(), edges.end(), std::min(byteOffset, text.size()));
    return found == edges.begin() ? 0 : *std::prev(found);
}
std::size_t Unicode::nextGrapheme(const std::string_view text, const std::size_t byteOffset)
{
    const auto edges = graphemeBoundaries(text);
    const auto found = std::upper_bound(edges.begin(), edges.end(), std::min(byteOffset, text.size()));
    return found == edges.end() ? text.size() : *found;
}
} // namespace tinyengine
