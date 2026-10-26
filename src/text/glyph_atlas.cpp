// 文件：src/text/glyph_atlas.cpp
// 作用：以带边距的行式装箱将真实灰度字形写入受预算限制的 RGBA 图集。
// 约定：当前帧内不驱逐字形；容量溢出只安排下一帧的整代重置。

#include "tinyengine/text/glyph_atlas.h"
#include "tinyengine/text/font.h"

#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

namespace tinyengine
{
GlyphAtlas::GlyphAtlas(GlyphAtlasConfig config) : m_config(config)
{
    // 限制单页大小，同时在任何分配前检查算术和预算。
    constexpr int maxPageSide = 16384;
    constexpr std::size_t maxPageBytes = 256u * 1024u * 1024u;
    if (m_config.pageWidth <= 0 || m_config.pageHeight <= 0 || m_config.pageWidth > maxPageSide ||
        m_config.pageHeight > maxPageSide || m_config.padding < 0 || m_config.maxPages == 0 || m_config.maxBytes == 0 ||
        m_config.padding > m_config.pageWidth / 2 || m_config.padding > m_config.pageHeight / 2)
        return;

    const auto width = static_cast<std::size_t>(m_config.pageWidth);
    const auto height = static_cast<std::size_t>(m_config.pageHeight);
    if (width > std::numeric_limits<std::size_t>::max() / height / 4)
        return;
    m_pageBytes = width * height * 4;
    m_valid = m_pageBytes <= m_config.maxBytes && m_pageBytes <= maxPageBytes;
}

std::size_t GlyphAtlas::KeyHash::operator()(const Key& key) const
{
    auto hash = std::hash<std::uint64_t>{}(key.face);
    hash ^= std::hash<std::uint32_t>{}(key.glyph) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    hash ^= std::hash<std::uint32_t>{}(key.size) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    return hash;
}

void GlyphAtlas::beginFrame()
{
    if (m_resetNextFrame)
        clear();
}

void GlyphAtlas::clear()
{
    m_glyphs.clear();
    m_pages.clear();
    m_shelves.clear();
    m_resetNextFrame = false;
    ++m_stats.generation;
}

bool GlyphAtlas::allocate(std::size_t page, int width, int height, int& x, int& y)
{
    auto next = m_shelves[page];
    if (width > m_config.pageWidth - next.x)
    {
        next.x = 0;
        next.y += next.height;
        next.height = 0;
    }
    if (height > m_config.pageHeight - next.y)
        return false;
    x = next.x;
    y = next.y;
    next.x += width;
    if (height > next.height)
        next.height = height;
    m_shelves[page] = next;
    return true;
}

std::optional<AtlasGlyph> GlyphAtlas::overflow(bool resetNextFrame)
{
    ++m_stats.overflows;
    m_resetNextFrame = m_resetNextFrame || resetNextFrame;
    return std::nullopt;
}

std::optional<AtlasGlyph> GlyphAtlas::getGlyph(const std::shared_ptr<FontFace>& face, std::uint32_t glyphId,
                                               float pixelSize)
{
    if (!m_valid || !face || !face->isLoaded() || !std::isfinite(pixelSize) || pixelSize <= 0 || pixelSize > 1024.0f)
        return std::nullopt;
    const auto size = static_cast<std::uint32_t>(std::round(static_cast<double>(pixelSize) * 64.0));
    if (size == 0)
        return std::nullopt;
    const Key key{face->getId(), glyphId, size};
    if (const auto found = m_glyphs.find(key); found != m_glyphs.end())
    {
        ++m_stats.hits;
        return found->second;
    }
    ++m_stats.misses;
    RasterizedGlyph raster;
    if (!face->rasterizeGlyph(glyphId, static_cast<float>(size) / 64.0f, raster) || raster.width < 0 ||
        raster.height < 0)
        return std::nullopt;

    AtlasGlyph glyph;
    glyph.width = raster.width;
    glyph.height = raster.height;
    glyph.bearingX = raster.bearingX;
    glyph.bearingY = raster.bearingY;
    glyph.advance = static_cast<float>(raster.advance);
    glyph.generation = m_stats.generation;
    if (raster.width == 0 || raster.height == 0)
    {
        if (raster.width != 0 || raster.height != 0 || !raster.pixels.empty())
            return std::nullopt;
        m_glyphs.emplace(key, glyph);
        return glyph;
    }

    // 先减去边距再比较，避免异常尺寸或边距导致整型加法溢出。
    if (raster.width > m_config.pageWidth - m_config.padding * 2 ||
        raster.height > m_config.pageHeight - m_config.padding * 2)
        return overflow(false);
    const auto rasterBytes = static_cast<std::size_t>(raster.width) * raster.height;
    if (raster.pixels.size() != rasterBytes)
        return std::nullopt;
    const int packedWidth = raster.width + m_config.padding * 2;
    const int packedHeight = raster.height + m_config.padding * 2;
    int x = 0, y = 0;
    std::size_t pageIndex = 0;
    while (pageIndex < m_pages.size() && !allocate(pageIndex, packedWidth, packedHeight, x, y))
        ++pageIndex;
    if (pageIndex == m_pages.size())
    {
        if (m_pages.size() >= m_config.maxPages || m_pages.size() >= m_config.maxBytes / m_pageBytes)
            return overflow(true);
        try
        {
            GlyphAtlasPage page;
            page.width = m_config.pageWidth;
            page.height = m_config.pageHeight;
            page.pixels.resize(m_pageBytes, 0);
            m_shelves.push_back({});
            try
            {
                m_pages.push_back(std::move(page));
            }
            catch (...)
            {
                m_shelves.pop_back();
                throw;
            }
        }
        catch (const std::bad_alloc&)
        {
            return overflow(true);
        }
        catch (const std::length_error&)
        {
            return overflow(true);
        }
        allocate(pageIndex, packedWidth, packedHeight, x, y);
    }

    glyph.page = pageIndex;
    glyph.x = x + m_config.padding;
    glyph.y = y + m_config.padding;
    auto& page = m_pages[pageIndex];
    for (int row = 0; row < raster.height; ++row)
        for (int column = 0; column < raster.width; ++column)
        {
            const auto target = (static_cast<std::size_t>(glyph.y + row) * page.width + glyph.x + column) * 4;
            page.pixels[target] = page.pixels[target + 1] = page.pixels[target + 2] = 255;
            page.pixels[target + 3] = raster.pixels[static_cast<std::size_t>(row) * raster.width + column];
        }
    ++page.revision;
    m_glyphs.emplace(key, glyph);
    return glyph;
}

const std::vector<GlyphAtlasPage>& GlyphAtlas::getPages() const
{
    return m_pages;
}

GlyphAtlasStats GlyphAtlas::getStats() const
{
    auto result = m_stats;
    result.pageCount = m_pages.size();
    result.glyphCount = m_glyphs.size();
    result.byteSize = m_pages.size() * m_pageBytes;
    return result;
}
} // namespace tinyengine
