// 文件：include/tinyengine/text/glyph_atlas.h
// 作用：定义按字体、字形编号和物理像素尺寸缓存的 CPU 字形图集。
// 约定：RGBA 像素保持白色字形和灰度透明度；仅在帧边界处理容量回收。

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tinyengine
{
class FontFace;

struct GlyphAtlasConfig
{
    int pageWidth = 1024, pageHeight = 1024;
    std::size_t maxPages = 8, maxBytes = 32 * 1024 * 1024;
    int padding = 1;
};

struct AtlasGlyph
{
    std::size_t page = 0;
    int x = 0, y = 0, width = 0, height = 0, bearingX = 0, bearingY = 0;
    float advance = 0;
    std::uint64_t generation = 0;
};

struct GlyphAtlasPage
{
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
    std::uint64_t revision = 0;
};

struct GlyphAtlasStats
{
    std::size_t pageCount = 0, glyphCount = 0, byteSize = 0;
    std::uint64_t hits = 0, misses = 0, overflows = 0, generation = 0;
};

// CPU 字形图集由渲染线程使用；一帧内不回收已分配的矩形。
class GlyphAtlas
{
  public:
    /// @brief 配置页尺寸、页数、总像素字节预算和字形边距。
    explicit GlyphAtlas(GlyphAtlasConfig config = {});

    /// @brief 容量不足时在下一帧开始清空，避免当前帧的 UV 失效。
    void beginFrame();
    /// @brief 丢弃全部页和缓存项并递增代数，保留累计统计。
    void clear();
    /// @brief 获取带边距缓存的字形；无效输入或容量不足时返回空值。
    std::optional<AtlasGlyph> getGlyph(const std::shared_ptr<FontFace>& face, std::uint32_t glyphId, float pixelSize);
    /// @brief 返回只读像素页；修改图集可能使引用失效。
    const std::vector<GlyphAtlasPage>& getPages() const;
    /// @brief 返回当前容量、累计命中及溢出次数和缓存代数。
    GlyphAtlasStats getStats() const;

  private:
    struct Key
    {
        std::uint64_t face = 0;
        std::uint32_t glyph = 0, size = 0;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash
    {
        std::size_t operator()(const Key& key) const;
    };
    struct Shelf
    {
        int x = 0, y = 0, height = 0;
    };

    bool allocate(std::size_t page, int width, int height, int& x, int& y);
    std::optional<AtlasGlyph> overflow(bool resetNextFrame);

    GlyphAtlasConfig m_config;
    bool m_valid = false, m_resetNextFrame = false;
    std::size_t m_pageBytes = 0;
    std::vector<GlyphAtlasPage> m_pages;
    std::vector<Shelf> m_shelves;
    std::unordered_map<Key, AtlasGlyph, KeyHash> m_glyphs;
    GlyphAtlasStats m_stats;
};
} // namespace tinyengine
