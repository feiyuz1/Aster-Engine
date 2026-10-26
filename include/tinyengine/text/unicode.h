// 文件：include/tinyengine/text/unicode.h
// 作用：严格解码 UTF-8，并提供字素、换行及 UTF-16 索引转换。
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tinyengine
{
struct UnicodeCodepoint
{
    std::uint32_t value = 0;
    std::size_t byteOffset = 0;
    std::size_t byteLength = 0;
    std::size_t utf16Offset = 0;
};

struct UnicodeText
{
    std::vector<UnicodeCodepoint> codepoints;
    std::u16string utf16;
    std::vector<std::size_t> utf16ToByte;
    bool valid = true;
};

class Unicode
{
  public:
    /// @brief 严格解码；每个非法字节替换为 U+FFFD，保留原始字节位置。
    static UnicodeText decode(std::string_view text);
    /// @brief 编码 Unicode 标量；非法标量替换为 U+FFFD。
    static std::string encode(const std::vector<std::uint32_t>& codepoints);
    /// @brief 返回包括首尾的原始 UTF-8 字节边界，不拆分组合字符。
    static std::vector<std::size_t> graphemeBoundaries(std::string_view text, std::string_view locale = "und");
    /// @brief 返回允许断行的 UTF-8 字节位置，包括文本末尾。
    static std::vector<std::size_t> lineBreaks(std::string_view text, std::string_view locale = "und");
    /// @brief 获取指定位置之前或之后的字素边界，结果限制在文本范围内。
    static std::size_t previousGrapheme(std::string_view text, std::size_t byteOffset);
    static std::size_t nextGrapheme(std::string_view text, std::size_t byteOffset);
};
} // namespace tinyengine
