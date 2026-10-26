// 文件：src/ui/text_layout.cpp
// 作用：按真实字体度量进行 Unicode 塑形、双向重排、换行及编辑几何计算。
#include "tinyengine/ui/text_layout.h"
#include "tinyengine/text/unicode.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <string_view>
#include <unicode/ubidi.h>
#include <unicode/uchar.h>
#include <unicode/uscript.h>

namespace tinyengine
{
namespace
{
float validSize(const float size)
{
    return std::isfinite(size) && size > 0.0f ? std::clamp(size, 1.0f, 1024.0f) : 18.0f;
}
FontStyle fontStyle(const TextStyle& style)
{
    FontStyle font = style.font;
    font.size = validSize(style.size);
    if (style.bold)
        font.weight = std::max(font.weight, 700);
    font.italic = font.italic || style.italic;
    return font;
}
bool equalStyle(const TextStyle& first, const TextStyle& second)
{
    return first.size == second.size && first.bold == second.bold && first.italic == second.italic &&
           first.color.x == second.color.x && first.color.y == second.color.y && first.color.z == second.color.z &&
           first.color.w == second.color.w && first.font.family == second.font.family &&
           first.font.weight == second.font.weight && first.font.italic == second.font.italic &&
           first.font.language == second.font.language;
}
struct StyledUnit
{
    std::size_t begin = 0, end = 0;
    TextStyle style;
};
struct ParsedText
{
    std::string plain;
    std::vector<std::size_t> source;
    std::vector<StyledUnit> units;
};
bool applyTag(const std::string& tag, TextStyle& style)
{
    if (tag == "b")
        style.bold = true;
    else if (tag == "i")
        style.italic = true;
    else if (tag.starts_with("size="))
    {
        char* end = nullptr;
        const float value = std::strtof(tag.c_str() + 5, &end);
        if (!end || *end || !std::isfinite(value) || value <= 0)
            return false;
        style.size = validSize(value);
    }
    else if (tag.starts_with("font=") && tag.size() > 5)
        style.font.family = tag.substr(5);
    else if (tag.starts_with("color=#") && tag.size() == 13)
    {
        char* end = nullptr;
        const auto rgb = std::strtoul(tag.c_str() + 7, &end, 16);
        if (!end || *end)
            return false;
        style.color.x = static_cast<float>((rgb >> 16u) & 255u) / 255.0f;
        style.color.y = static_cast<float>((rgb >> 8u) & 255u) / 255.0f;
        style.color.z = static_cast<float>(rgb & 255u) / 255.0f;
    }
    else
        return false;
    return true;
}
ParsedText parse(const std::string& source, const TextStyle& baseStyle, const bool richText)
{
    ParsedText result;
    std::vector<std::pair<std::string, TextStyle>> stack{{"", baseStyle}};
    const auto decoded = Unicode::decode(source);
    std::size_t pointIndex = 0;
    std::size_t nextClose = source.find(']');
    for (std::size_t index = 0; index < source.size();)
    {
        if (richText && source[index] == '[')
        {
            if (nextClose != std::string::npos && nextClose < index)
                nextClose = source.find(']', index);
            const auto close = nextClose;
            if (close != std::string::npos)
            {
                const auto tag = source.substr(index + 1, close - index - 1);
                TextStyle next = stack.back().second;
                bool consumed = false;
                if (applyTag(tag, next))
                {
                    stack.push_back({tag.substr(0, tag.find('=')), next});
                    consumed = true;
                }
                else if (stack.size() > 1 && tag == "/" + stack.back().first)
                {
                    stack.pop_back();
                    consumed = true;
                }
                if (consumed)
                {
                    index = close + 1;
                    continue;
                }
            }
        }
        while (pointIndex < decoded.codepoints.size() && decoded.codepoints[pointIndex].byteOffset < index)
            ++pointIndex;
        if (pointIndex == decoded.codepoints.size())
            break;
        const auto& point = decoded.codepoints[pointIndex++];
        const auto encoded = Unicode::encode({point.value});
        const auto begin = result.plain.size();
        result.plain += encoded;
        result.source.insert(result.source.end(), encoded.size(), index);
        result.units.push_back({begin, result.plain.size(), stack.back().second});
        index += point.byteLength;
    }
    result.source.push_back(source.size());
    return result;
}
using Bidi = std::unique_ptr<UBiDi, decltype(&ubidi_close)>;
struct Segment
{
    std::size_t begin = 0, end = 0;
    TextStyle style;
    std::shared_ptr<FontFace> face;
    UScriptCode script = USCRIPT_COMMON;
    bool rtl = false;
};
struct PendingGlyph
{
    ShapedGlyph shaped;
    std::size_t begin = 0, end = 0;
};
struct Paragraph
{
    std::size_t begin = 0;
    std::string_view text;
    UnicodeText decoded;
    std::vector<std::size_t> graphemes;
    std::vector<std::size_t> breaks;
    std::vector<Segment> segments;
    Bidi bidi{nullptr, ubidi_close};
    bool rtl = false;
};
std::size_t utf16Offset(const UnicodeText& decoded, const std::size_t byte)
{
    const auto found = std::lower_bound(decoded.codepoints.begin(), decoded.codepoints.end(), byte,
                                        [](const auto& point, const auto offset) { return point.byteOffset < offset; });
    return found == decoded.codepoints.end() ? decoded.utf16.size() : found->utf16Offset;
}
void prepareParagraph(Paragraph& paragraph, const ParsedText& parsed, const std::size_t begin, const std::size_t end,
                      const TextLayoutOptions& options, const TextStyle& baseStyle, FontManager& fonts)
{
    paragraph.begin = begin;
    paragraph.text = std::string_view(parsed.plain).substr(begin, end - begin);
    paragraph.decoded = Unicode::decode(paragraph.text);
    paragraph.graphemes = Unicode::graphemeBoundaries(paragraph.text, baseStyle.font.language);
    paragraph.breaks = Unicode::lineBreaks(paragraph.text, baseStyle.font.language);
    UErrorCode error = U_ZERO_ERROR;
    paragraph.bidi.reset(ubidi_open());
    if (paragraph.bidi && !paragraph.decoded.utf16.empty())
    {
        const UBiDiLevel level = options.direction == TextDirection::Auto          ? UBIDI_DEFAULT_LTR
                                 : options.direction == TextDirection::RightToLeft ? 1
                                                                                   : 0;
        ubidi_setPara(paragraph.bidi.get(), reinterpret_cast<const UChar*>(paragraph.decoded.utf16.data()),
                      static_cast<std::int32_t>(paragraph.decoded.utf16.size()), level, nullptr, &error);
        if (U_FAILURE(error))
            paragraph.bidi.reset();
        else
            paragraph.rtl = (ubidi_getParaLevel(paragraph.bidi.get()) & 1u) != 0;
    }
    auto unit = std::lower_bound(parsed.units.begin(), parsed.units.end(), begin,
                                 [](const auto& item, const auto offset) { return item.begin < offset; });
    UScriptCode previousScript = USCRIPT_COMMON;
    for (std::size_t index = 0; index + 1 < paragraph.graphemes.size(); ++index)
    {
        const auto start = paragraph.graphemes[index], limit = paragraph.graphemes[index + 1];
        while (unit != parsed.units.end() && unit->end <= begin + start)
            ++unit;
        TextStyle style = unit == parsed.units.end() ? baseStyle : unit->style;
        style.size = validSize(style.size);
        std::vector<std::uint32_t> points;
        UScriptCode script = USCRIPT_COMMON;
        const auto pointBegin =
            std::lower_bound(paragraph.decoded.codepoints.begin(), paragraph.decoded.codepoints.end(), start,
                             [](const auto& point, const auto offset) { return point.byteOffset < offset; });
        for (auto point = pointBegin; point != paragraph.decoded.codepoints.end() && point->byteOffset < limit; ++point)
        {
            points.push_back(point->value);
            error = U_ZERO_ERROR;
            const auto candidate = uscript_getScript(static_cast<UChar32>(point->value), &error);
            if (U_SUCCESS(error) && candidate != USCRIPT_COMMON && candidate != USCRIPT_INHERITED)
                script = candidate;
        }
        if (script == USCRIPT_COMMON)
            script = previousScript;
        else
            previousScript = script;
        const auto face = fonts.resolve(fontStyle(style), points);
        const auto character = utf16Offset(paragraph.decoded, start);
        const bool rtl = paragraph.bidi && character < paragraph.decoded.utf16.size()
                             ? (ubidi_getLevelAt(paragraph.bidi.get(), static_cast<std::int32_t>(character)) & 1u) != 0
                             : paragraph.rtl;
        if (!paragraph.segments.empty())
        {
            auto& last = paragraph.segments.back();
            if (last.face == face && last.script == script && last.rtl == rtl && equalStyle(last.style, style))
            {
                last.end = limit;
                continue;
            }
        }
        paragraph.segments.push_back({start, limit, style, face, script, rtl});
    }
}
std::vector<PendingGlyph> shapeRange(const Paragraph& paragraph, const std::size_t start, const std::size_t end,
                                     const bool visual)
{
    std::vector<PendingGlyph> result;
    struct Range
    {
        std::size_t begin, end;
        bool rtl;
    };
    std::vector<Range> ranges;
    if (visual && paragraph.bidi && start != end)
    {
        UErrorCode error = U_ZERO_ERROR;
        Bidi line(ubidi_open(), ubidi_close);
        const auto first16 = utf16Offset(paragraph.decoded, start);
        const auto end16 = utf16Offset(paragraph.decoded, end);
        if (line)
            ubidi_setLine(paragraph.bidi.get(), static_cast<std::int32_t>(first16), static_cast<std::int32_t>(end16),
                          line.get(), &error);
        const auto count = line && U_SUCCESS(error) ? ubidi_countRuns(line.get(), &error) : 0;
        for (std::int32_t run = 0; U_SUCCESS(error) && run < count; ++run)
        {
            std::int32_t logicalStart = 0, length = 0;
            const bool rtl = ubidi_getVisualRun(line.get(), run, &logicalStart, &length) == UBIDI_RTL;
            ranges.push_back({paragraph.decoded.utf16ToByte[first16 + logicalStart],
                              paragraph.decoded.utf16ToByte[first16 + logicalStart + length], rtl});
        }
    }
    if (ranges.empty())
        ranges.push_back({start, end, paragraph.rtl});
    for (const auto& range : ranges)
    {
        std::vector<Segment> pieces;
        const auto firstSegment =
            std::lower_bound(paragraph.segments.begin(), paragraph.segments.end(), range.begin,
                             [](const auto& segment, const auto offset) { return segment.end <= offset; });
        for (auto current = firstSegment; current != paragraph.segments.end() && current->begin < range.end; ++current)
        {
            auto segment = *current;
            segment.begin = std::max(segment.begin, range.begin);
            segment.end = std::min(segment.end, range.end);
            if (segment.begin < segment.end)
                pieces.push_back(std::move(segment));
        }
        if (visual && range.rtl)
            std::reverse(pieces.begin(), pieces.end());
        for (const auto& piece : pieces)
        {
            const bool rtl = visual ? range.rtl : piece.rtl;
            if (!piece.face)
            {
                auto first = std::lower_bound(paragraph.graphemes.begin(), paragraph.graphemes.end(), piece.begin);
                auto last = std::lower_bound(paragraph.graphemes.begin(), paragraph.graphemes.end(), piece.end);
                std::vector<PendingGlyph> missing;
                for (auto edge = first; edge < last; ++edge)
                {
                    PendingGlyph glyph;
                    glyph.begin = *edge;
                    glyph.end = *std::next(edge);
                    glyph.shaped.style = piece.style;
                    glyph.shaped.advance = piece.style.size;
                    glyph.shaped.rightToLeft = rtl;
                    const auto values = Unicode::decode(paragraph.text.substr(glyph.begin, glyph.end - glyph.begin));
                    glyph.shaped.codepoint = values.codepoints.empty() ? 0xfffd : values.codepoints.front().value;
                    missing.push_back(std::move(glyph));
                }
                if (rtl)
                    std::reverse(missing.begin(), missing.end());
                result.insert(result.end(), missing.begin(), missing.end());
                continue;
            }
            const auto shaped = piece.face->shapeContext(paragraph.text.substr(start, end - start), piece.begin - start,
                                                         piece.end - piece.begin, piece.style.size, rtl,
                                                         piece.style.font.language, uscript_getShortName(piece.script));
            std::vector<std::size_t> clusters{piece.end};
            for (const auto& glyph : shaped)
                clusters.push_back(piece.begin + std::min(glyph.cluster, piece.end - piece.begin));
            std::sort(clusters.begin(), clusters.end());
            clusters.erase(std::unique(clusters.begin(), clusters.end()), clusters.end());
            for (const auto& glyph : shaped)
            {
                const auto cluster = piece.begin + std::min(glyph.cluster, piece.end - piece.begin);
                const auto next = std::upper_bound(clusters.begin(), clusters.end(), cluster);
                PendingGlyph pending;
                pending.begin = cluster;
                pending.end = next == clusters.end() ? piece.end : *next;
                auto& output = pending.shaped;
                output.face = piece.face;
                output.glyphId = glyph.glyphId;
                output.x = glyph.offsetX;
                output.y = -glyph.offsetY;
                output.advance = std::max(0.0f, glyph.advanceX);
                output.style = piece.style;
                output.rightToLeft = rtl;
                const auto decoded = Unicode::decode(paragraph.text.substr(cluster, pending.end - cluster));
                output.codepoint = decoded.codepoints.empty() ? 0 : decoded.codepoints.front().value;
                result.push_back(std::move(pending));
            }
        }
    }
    return result;
}
float rangeWidth(const std::vector<PendingGlyph>& glyphs)
{
    float width = 0.0f;
    for (const auto& glyph : glyphs)
        width += glyph.shaped.advance;
    return width;
}
void appendLine(TextLayoutResult& result, const ParsedText& parsed, const Paragraph& paragraph, const std::size_t begin,
                const std::size_t end, std::vector<PendingGlyph> glyphs, const TextStyle& baseStyle,
                const TextLayoutOptions& options, FontManager& fonts)
{
    const auto defaultFace = fonts.resolve(fontStyle(baseStyle));
    FontMetrics metrics = defaultFace ? defaultFace->getMetrics(validSize(baseStyle.size)) : FontMetrics{};
    float ascender = metrics.ascender, descender = metrics.descender, lineHeight = metrics.lineHeight;
    for (const auto& glyph : glyphs)
    {
        if (!glyph.shaped.face)
            continue;
        metrics = glyph.shaped.face->getMetrics(glyph.shaped.style.size);
        ascender = std::max(ascender, metrics.ascender);
        descender = std::max(descender, metrics.descender);
        lineHeight = std::max(lineHeight, metrics.lineHeight);
    }
    lineHeight = std::max({lineHeight, ascender + descender, validSize(baseStyle.size)});
    const float spacing = std::isfinite(options.lineSpacing) ? std::max(1.0f, options.lineSpacing) : 1.0f;
    lineHeight = std::ceil(lineHeight * spacing);
    const float width = rangeWidth(glyphs), y = result.height, baseline = y + ascender;
    float shift = 0.0f;
    const bool right = options.alignment == TextAlignment::Right ||
                       (options.alignment == TextAlignment::Start && paragraph.rtl) ||
                       (options.alignment == TextAlignment::End && !paragraph.rtl);
    if (std::isfinite(options.maximumWidth) && options.maximumWidth > width)
    {
        if (options.alignment == TextAlignment::Center)
            shift = (options.maximumWidth - width) * 0.5f;
        else if (right)
            shift = options.maximumWidth - width;
    }
    const auto lineIndex = result.lines.size();
    result.lines.push_back({parsed.source[paragraph.begin + begin], parsed.source[paragraph.begin + end], y, baseline,
                            width, lineHeight, paragraph.rtl});
    float pen = shift;
    std::map<std::pair<std::size_t, std::size_t>, TextClusterBox> boxes;
    for (auto& pending : glyphs)
    {
        auto& glyph = pending.shaped;
        const float advance = glyph.advance;
        glyph.cluster = parsed.source[paragraph.begin + pending.begin];
        glyph.clusterEnd = parsed.source[paragraph.begin + pending.end];
        glyph.x += pen;
        glyph.y += baseline;
        result.missingGlyphs = result.missingGlyphs || glyph.glyphId == 0;
        const auto ink = glyph.face ? glyph.face->getGlyphMetrics(glyph.glyphId, glyph.style.size) : FontGlyphMetrics{};
        if (ink.width > 0 && ink.height > 0)
        {
            const float left = glyph.x + ink.bearingX, top = glyph.y - ink.bearingY;
            if (result.inkBounds.width == 0 || result.inkBounds.height == 0)
                result.inkBounds = {left, top, ink.width, ink.height};
            else
            {
                const float rightEdge = std::max(result.inkBounds.x + result.inkBounds.width, left + ink.width);
                const float bottom = std::max(result.inkBounds.y + result.inkBounds.height, top + ink.height);
                result.inkBounds.x = std::min(result.inkBounds.x, left);
                result.inkBounds.y = std::min(result.inkBounds.y, top);
                result.inkBounds.width = rightEdge - result.inkBounds.x;
                result.inkBounds.height = bottom - result.inkBounds.y;
            }
        }
        const auto key = std::make_pair(pending.begin, pending.end);
        auto [found, inserted] = boxes.try_emplace(
            key, TextClusterBox{
                     glyph.cluster, glyph.clusterEnd, lineIndex, {pen, y, advance, lineHeight}, glyph.rightToLeft});
        if (!inserted)
        {
            const float edge = std::max(found->second.bounds.x + found->second.bounds.width, pen + advance);
            found->second.bounds.x = std::min(found->second.bounds.x, pen);
            found->second.bounds.width = edge - found->second.bounds.x;
        }
        pen += advance;
        result.glyphs.push_back(std::move(glyph));
    }
    for (const auto& [range, box] : boxes)
    {
        auto start = std::lower_bound(paragraph.graphemes.begin(), paragraph.graphemes.end(), range.first);
        auto limit = std::lower_bound(paragraph.graphemes.begin(), paragraph.graphemes.end(), range.second);
        const auto count = std::distance(start, limit);
        if (count <= 0)
            continue;
        const float cellWidth = box.bounds.width / static_cast<float>(count);
        for (std::ptrdiff_t item = 0; item < count; ++item)
        {
            const auto offset = *(start + item), next = *(start + item + 1);
            const float left = box.bounds.x + cellWidth * static_cast<float>(box.rightToLeft ? count - item - 1 : item);
            TextClusterBox cluster{parsed.source[paragraph.begin + offset],
                                   parsed.source[paragraph.begin + next],
                                   lineIndex,
                                   {left, y, cellWidth, lineHeight},
                                   box.rightToLeft};
            result.clusters.push_back(cluster);
            result.carets.push_back(
                {cluster.sourceBegin, lineIndex, box.rightToLeft ? left + cellWidth : left, y, lineHeight});
            result.carets.push_back(
                {cluster.sourceEnd, lineIndex, box.rightToLeft ? left : left + cellWidth, y, lineHeight});
        }
    }
    if (boxes.empty())
        result.carets.push_back({parsed.source[paragraph.begin + begin], lineIndex, shift, y, lineHeight});
    result.width = std::max(result.width, width);
    result.height += lineHeight;
}
} // namespace

std::vector<std::uint32_t> TextLayout::DecodeUtf8(const std::string& text)
{
    std::vector<std::uint32_t> values;
    for (const auto& point : Unicode::decode(text).codepoints)
        values.push_back(point.value);
    return values;
}
std::string TextLayout::encodeUtf8(const std::vector<std::uint32_t>& codepoints)
{
    return Unicode::encode(codepoints);
}
TextLayoutResult TextLayout::shape(const std::string& text, const float maximumWidth, const TextStyle& style,
                                   const TextDirection direction, const bool richText, FontManager* fonts)
{
    return layout(text, {maximumWidth, direction, richText}, style, fonts);
}
TextLayoutResult TextLayout::layout(const std::string& source, const TextLayoutOptions& options, const TextStyle& style,
                                    FontManager* fontService)
{
    TextLayoutResult result;
    FontManager& fonts = fontService ? *fontService : FontManager::defaultManager();
    const auto parsed = parse(source, style, options.richText);
    std::vector<UnicodeCodepoint> separators;
    for (const auto& point : Unicode::decode(parsed.plain).codepoints)
        if (point.value == '\r' || point.value == '\n' || point.value == 0x0b || point.value == 0x0c ||
            point.value == 0x85 || point.value == 0x2028 || point.value == 0x2029)
            separators.push_back(point);
    std::size_t separatorIndex = 0;
    std::size_t paragraphBegin = 0;
    TextLayoutOptions paragraphOptions = options;
    do
    {
        const auto paragraphEnd =
            separatorIndex < separators.size() ? separators[separatorIndex].byteOffset : parsed.plain.size();
        Paragraph paragraph;
        prepareParagraph(paragraph, parsed, paragraphBegin, paragraphEnd, paragraphOptions, style, fonts);
        std::vector<float> widths(paragraph.graphemes.size(), 0.0f);
        const auto logical = shapeRange(paragraph, 0, paragraph.text.size(), false);
        for (const auto& glyph : logical)
        {
            const auto first = std::lower_bound(paragraph.graphemes.begin(), paragraph.graphemes.end(), glyph.begin);
            const auto last = std::lower_bound(paragraph.graphemes.begin(), paragraph.graphemes.end(), glyph.end);
            const auto count = std::distance(first, last);
            for (auto item = first; item < last; ++item)
                widths[static_cast<std::size_t>(item - paragraph.graphemes.begin())] +=
                    glyph.shaped.advance / static_cast<float>(std::max<std::ptrdiff_t>(1, count));
        }
        std::size_t first = 0;
        do
        {
            const auto begin = paragraph.graphemes[first];
            std::size_t last = first + 1, lastBreak = first;
            float width = 0.0f;
            const bool wrap = options.wrap && std::isfinite(options.maximumWidth) && options.maximumWidth > 0.0f;
            while (last < paragraph.graphemes.size())
            {
                if (wrap && last > first + 1 && width + widths[last - 1] > options.maximumWidth)
                    break;
                width += widths[last - 1];
                if (std::binary_search(paragraph.breaks.begin(), paragraph.breaks.end(), paragraph.graphemes[last]))
                    lastBreak = last;
                ++last;
            }
            last = std::min(last - 1, paragraph.graphemes.size() - 1);
            if (last + 1 < paragraph.graphemes.size() && lastBreak > first)
                last = lastBreak;
            if (last <= first && first + 1 < paragraph.graphemes.size())
                last = first + 1;
            auto glyphs = shapeRange(paragraph, begin, paragraph.graphemes[last], true);
            while (wrap && rangeWidth(glyphs) > options.maximumWidth + 0.01f && last > first + 1)
            {
                auto previous = last - 1;
                for (auto candidate = previous; candidate > first; --candidate)
                    if (std::binary_search(paragraph.breaks.begin(), paragraph.breaks.end(),
                                           paragraph.graphemes[candidate]))
                    {
                        previous = candidate;
                        break;
                    }
                last = previous;
                glyphs = shapeRange(paragraph, begin, paragraph.graphemes[last], true);
            }
            appendLine(result, parsed, paragraph, begin, paragraph.graphemes[last], std::move(glyphs), style, options,
                       fonts);
            first = last;
        } while (first + 1 < paragraph.graphemes.size());
        if (paragraphEnd == parsed.plain.size())
            break;
        const auto separator = separators[separatorIndex++];
        paragraphBegin = paragraphEnd + separator.byteLength;
        paragraphOptions.direction = options.direction;
        if (separator.value == 0x2028 || separator.value == 0x0b || separator.value == 0x0c)
            paragraphOptions.direction = paragraph.rtl ? TextDirection::RightToLeft : TextDirection::LeftToRight;
        if (parsed.plain[paragraphEnd] == '\r' && paragraphBegin < parsed.plain.size() &&
            parsed.plain[paragraphBegin] == '\n')
        {
            ++paragraphBegin;
            ++separatorIndex;
        }
    } while (paragraphBegin <= parsed.plain.size());
    result.lineCount = result.lines.size();
    return result;
}
std::size_t TextLayoutResult::hitTest(const float x, const float y) const
{
    return hitTestCaret(x, y).sourceOffset;
}
TextCaret TextLayoutResult::hitTestCaret(const float x, const float y) const
{
    if (carets.empty())
        return {};
    std::size_t line = 0;
    for (std::size_t index = 0; index < lines.size(); ++index)
        if (y >= lines[index].y)
            line = index;
    const TextCaret* best = nullptr;
    float distance = std::numeric_limits<float>::max();
    for (const auto& caret : carets)
        if (caret.line == line && std::abs(caret.x - x) < distance)
        {
            best = &caret;
            distance = std::abs(caret.x - x);
        }
    return best ? *best : carets.back();
}
TextCaret TextLayoutResult::getCaret(const std::size_t sourceOffset, const bool trailing) const
{
    TextCaret result;
    std::size_t distance = std::numeric_limits<std::size_t>::max();
    for (const auto& caret : carets)
    {
        const auto candidate =
            caret.sourceOffset > sourceOffset ? caret.sourceOffset - sourceOffset : sourceOffset - caret.sourceOffset;
        if (candidate < distance || (trailing && candidate == distance))
        {
            result = caret;
            distance = candidate;
        }
    }
    return result;
}
std::vector<TextBounds> TextLayoutResult::getSelectionRects(std::size_t begin, std::size_t end) const
{
    if (begin > end)
        std::swap(begin, end);
    std::vector<TextBounds> result;
    if (begin == end)
        return result;
    for (const auto& cluster : clusters)
        if (cluster.sourceEnd > begin && cluster.sourceBegin < end && cluster.bounds.width > 0.0f)
            result.push_back(cluster.bounds);
    std::sort(result.begin(), result.end(),
              [](const auto& first, const auto& second)
              { return first.y == second.y ? first.x < second.x : first.y < second.y; });
    std::vector<TextBounds> merged;
    for (const auto& rect : result)
        if (!merged.empty() && merged.back().y == rect.y && merged.back().height == rect.height &&
            rect.x <= merged.back().x + merged.back().width + 0.01f)
            merged.back().width = std::max(merged.back().width, rect.x + rect.width - merged.back().x);
        else
            merged.push_back(rect);
    return merged;
}
} // namespace tinyengine
