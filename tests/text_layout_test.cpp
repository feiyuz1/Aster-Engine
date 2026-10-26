// 文件：tests/text_layout_test.cpp
// 作用：验证真实字体度量、Unicode 塑形、换行和文本位置映射。
#include "tinyengine/text/unicode.h"
#include "tinyengine/ui/text_layout.h"

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace tinyengine;

int main()
{
    int failures = 0;
    auto check = [&](bool value, const char* message)
    {
        if (!value)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    };
    FontManager fonts;
    const auto latin = fonts.loadFace("tests/fixtures/fonts/noto_sans.ttf");
    const auto arabic = fonts.loadFace("tests/fixtures/fonts/noto_sans_arabic.ttf");
    const auto chinese = fonts.loadFace("tests/fixtures/fonts/noto_sans_sc_subset.otf");
    check(latin && arabic && chinese, "deterministic multilingual fixtures must load");
    if (!latin || !arabic || !chinese)
        return 1;
    fonts.registerFamily("latin", {latin});
    fonts.registerFamily("arabic", {arabic});
    fonts.registerFamily("chinese", {chinese});
    fonts.setDefaultFamily("latin");
    fonts.setFallbackFamilies({"arabic", "chinese"});
    const auto shape = [&](const std::string& text, const float width = 0.0f,
                           const TextDirection direction = TextDirection::Auto, const bool rich = false)
    { return TextLayout::shape(text, width, {}, direction, rich, &fonts); };
    const auto narrow = shape("iii");
    const auto wide = shape("WWW");
    check(wide.width > narrow.width * 2.0f, "proportional font metrics must distinguish iii from WWW");
    const auto invalid = TextLayout::DecodeUtf8(std::string("\xc0\xaf", 2));
    check(!invalid.empty() && invalid.front() == 0xfffd, "overlong UTF-8 must not decode as slash");
    const auto continuation = TextLayout::DecodeUtf8(std::string("\xe4"
                                                                 "AB",
                                                                 3));
    check(continuation.size() == 3 && continuation[0] == 0xfffd && continuation[1] == 'A',
          "invalid UTF-8 must preserve subsequent ASCII");
    check(TextLayout::DecodeUtf8(TextLayout::encodeUtf8({0xd800})).front() == 0xfffd,
          "encoding a surrogate must emit replacement scalar");
    const auto av = shape("AV");
    check(av.width < shape("A").width + shape("V").width - 0.1f, "font kerning must affect measurement");
    const auto fi = shape("fi");
    check(fi.glyphs.size() == 1 && fi.glyphs.front().glyphId != 0 && fi.glyphs.front().cluster == 0 &&
              fi.glyphs.front().clusterEnd == 2,
          "font ligature must retain full source cluster");
    const std::string combining = "e\xcc\x81";
    const auto accents = shape(combining);
    check(std::all_of(accents.carets.begin(), accents.carets.end(),
                      [](const auto& caret) { return caret.sourceOffset == 0 || caret.sourceOffset == 3; }),
          "combining grapheme must have no internal caret");
    const auto emoji = Unicode::encode({0x1f469, 0x200d, 0x1f4bb});
    check(Unicode::graphemeBoundaries(emoji) == std::vector<std::size_t>{0, emoji.size()},
          "ZWJ emoji must remain one editing grapheme");
    check(Unicode::graphemeBoundaries("\xf0\x9f\x87\xa8\xf0\x9f\x87\xb3").size() == 2,
          "regional indicator flag must remain one grapheme");
    const auto wrapped = shape("alpha beta gamma delta", 65);
    check(wrapped.lineCount > 1 && std::all_of(wrapped.lines.begin(), wrapped.lines.end(),
                                               [](const auto& line) { return line.width <= 65.01f; }),
          "wrapped lines must use real advances");
    const auto lineEnding = shape("a\r\nb\nc\r");
    check(lineEnding.lines.size() == 4 && lineEnding.lines[1].sourceBegin == 3 && lineEnding.lines[3].sourceBegin == 7,
          "CRLF and trailing empty line preserve source positions");
    for (const auto separator : {0x85u, 0x2028u, 0x2029u})
        check(shape("a" + Unicode::encode({separator}) + "b").lineCount == 2,
              "Unicode mandatory break must split lines even without wrapping");
    const std::string cjk = Unicode::encode({0x4e2d, 0x6587, 0xff0c, 0x6d4b, 0x8bd5, 0x3002});
    const auto mixed = shape("A" + cjk);
    check(!mixed.missingGlyphs && std::any_of(mixed.glyphs.begin(), mixed.glyphs.end(),
                                              [&](const auto& glyph) { return glyph.face == chinese; }),
          "CJK must select explicit fallback face");
    const auto cjkWrapped = shape(cjk, 50);
    check(std::all_of(cjkWrapped.lines.begin(), cjkWrapped.lines.end(),
                      [&](const auto& line)
                      {
                          const auto values = Unicode::decode(std::string_view(cjk).substr(line.sourceBegin));
                          return values.codepoints.empty() || (values.codepoints.front().value != 0xff0c &&
                                                               values.codepoints.front().value != 0x3002);
                      }),
          "CJK closing punctuation must not start a normally wrappable line");
    const auto arabicText = Unicode::encode({0x0633, 0x0644, 0x0627, 0x0645});
    const auto rtl = shape(arabicText + " 123 ABC", 0, TextDirection::RightToLeft);
    float digits[3] = {-1, -1, -1};
    bool joined = false;
    for (const auto& glyph : rtl.glyphs)
    {
        if (glyph.codepoint >= '1' && glyph.codepoint <= '3')
            digits[glyph.codepoint - '1'] = glyph.x;
        if (glyph.face == arabic && glyph.glyphId != arabic->getGlyphIndex(glyph.codepoint))
            joined = true;
    }
    check(joined && !rtl.missingGlyphs, "Arabic must shape contextual joined glyphs");
    check(digits[0] >= 0 && digits[0] < digits[1] && digits[1] < digits[2], "RTL must preserve number visual order");
    const auto wholeArabic = shape(arabicText);
    const auto coloredArabic =
        shape("[color=#ff0000]" + Unicode::encode({0x0633}) + "[/color]" + Unicode::encode({0x0644, 0x0627, 0x0645}), 0,
              TextDirection::Auto, true);
    check(wholeArabic.glyphs.size() == coloredArabic.glyphs.size() &&
              std::equal(wholeArabic.glyphs.begin(), wholeArabic.glyphs.end(), coloredArabic.glyphs.begin(),
                         [](const auto& first, const auto& second) { return first.glyphId == second.glyphId; }),
          "paint-only rich spans must preserve contextual Arabic joining");
    const auto rich = shape("[color=#ff0000]A[/color][size=36]B[/size]", 0, TextDirection::Auto, true);
    check(rich.glyphs.size() == 2 && rich.glyphs[0].style.color.x == 1 && rich.glyphs[0].style.color.y == 0 &&
              rich.glyphs[1].style.size == 36 && rich.lines[0].height > av.lines[0].height,
          "rich spans must preserve actual styles and expand line metrics");
    check(shape("[/wrong]", 0, TextDirection::Auto, true).glyphs.size() == 8,
          "unmatched closing rich tag must remain literal");
    const auto ordinary = shape("abc");
    for (std::size_t position = 0; position <= 3; ++position)
    {
        const auto caret = ordinary.getCaret(position);
        check(ordinary.hitTest(caret.x, caret.y) == position, "caret and hit testing must roundtrip ASCII source");
    }
    check(ordinary.getSelectionRects(0, 2).size() == 1 && ordinary.getSelectionRects(1, 1).empty(),
          "selection must merge adjacent graphemes and exclude empty range");
    const auto selectedLigature = fi.getSelectionRects(1, 2);
    check(selectedLigature.size() == 1 && selectedLigature[0].width > 0 && selectedLigature[0].width < fi.width,
          "partial ligature selection must have a bounded visual rectangle");
    const auto ttc = fonts.loadFace("tests/fixtures/fonts/noto_test_collection.ttc", 1);
    check(ttc && ttc->getGlyphIndex(0x0633) != 0, "TTC face index must select Arabic face");
    std::cout << (failures ? "Text layout failures\n" : "Text layout regressions passed\n");
    return failures ? 1 : 0;
}
