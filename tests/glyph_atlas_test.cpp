#include "tinyengine/text/font.h"
#include "tinyengine/text/glyph_atlas.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace tinyengine;

namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::shared_ptr<FontFace> loadFixture()
{
    std::string overridePath;
#ifdef _WIN32
    char* environmentPath = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&environmentPath, &length, "TINYENGINE_TEST_FONT_PATH") == 0 && environmentPath)
        overridePath = environmentPath;
    std::free(environmentPath);
#else
    if (const auto* environmentPath = std::getenv("TINYENGINE_TEST_FONT_PATH"))
        overridePath = environmentPath;
#endif
    const auto path = overridePath.empty() ? std::filesystem::path("tests/fixtures/fonts/noto_sans.ttf")
                                           : std::filesystem::path(overridePath);
    auto face = std::make_shared<FontFace>();
    check(face->load(path), "load bundled font fixture (or TINYENGINE_TEST_FONT_PATH)");
    return face;
}

void cacheUsesFaceGlyphAndCanonicalSize(const std::shared_ptr<FontFace>& face)
{
    GlyphAtlas atlas({256, 256, 2, 2 * 256 * 256 * 4, 1});
    const auto a = face->getGlyphIndex('A');
    const auto b = face->getGlyphIndex('B');
    const auto first = atlas.getGlyph(face, a, 18.0f);
    check(first.has_value(), "first rasterized glyph is cached");
    const auto revision = atlas.getPages().at(first->page).revision;
    const auto again = atlas.getGlyph(face, a, 18.001f);
    check(again && again->page == first->page && again->x == first->x && again->y == first->y,
          "sizes rounding to the same 1/64 pixel share an atlas entry");
    check(atlas.getPages().at(first->page).revision == revision, "cache hit does not modify pixels");
    const auto larger = atlas.getGlyph(face, a, 36.0f);
    check(larger && larger->height > first->height, "different physical sizes use different rasters");
    check(atlas.getGlyph(face, a, 18.02f).has_value(), "different 26.6 size gets its own entry");
    check(atlas.getGlyph(face, b, 18.0f).has_value(), "glyph ID participates in cache key");
    check(atlas.getGlyph(loadFixture(), a, 18.0f).has_value(), "face identity participates in cache key");
    const auto stats = atlas.getStats();
    check(stats.glyphCount == 5 && stats.hits == 1 && stats.misses == 5, "cache counters track unique keys");
    check(first->width > 0 && first->height > 0, "returned glyph value survives map growth");
    atlas.beginFrame();
    check(atlas.getStats().generation == stats.generation && atlas.getStats().glyphCount == 5,
          "unsaturated beginFrame retains the cache");
}

void pixelsAndPaddingMatchRaster(const std::shared_ptr<FontFace>& face)
{
    const auto id = face->getGlyphIndex('j');
    RasterizedGlyph raster;
    check(face->rasterizeGlyph(id, 31.0f, raster) && raster.width > 0 && raster.height > 0,
          "fixture has a visible glyph with bearings");
    GlyphAtlas atlas({128, 128, 1, 128 * 128 * 4, 2});
    const auto glyph = atlas.getGlyph(face, id, 31.0f);
    check(glyph && glyph->width == raster.width && glyph->height == raster.height &&
              glyph->bearingX == raster.bearingX && glyph->bearingY == raster.bearingY &&
              glyph->advance == static_cast<float>(raster.advance),
          "atlas preserves raster dimensions, bearings and advance");
    const auto& page = atlas.getPages().at(glyph->page);
    check(page.revision > 0 && page.pixels.size() == 128 * 128 * 4, "page has RGBA storage and a mutation revision");
    check(glyph->x >= 2 && glyph->y >= 2 && glyph->x + glyph->width + 2 <= page.width &&
              glyph->y + glyph->height + 2 <= page.height,
          "packed rectangle reserves padding on every edge");
    bool visible = false;
    for (int y = 0; y < page.height; ++y)
        for (int x = 0; x < page.width; ++x)
        {
            const auto offset = (static_cast<std::size_t>(y) * page.width + x) * 4;
            const bool inside =
                x >= glyph->x && x < glyph->x + glyph->width && y >= glyph->y && y < glyph->y + glyph->height;
            if (inside)
            {
                const auto alpha = raster.pixels[static_cast<std::size_t>(y - glyph->y) * raster.width + x - glyph->x];
                check(page.pixels[offset] == 255 && page.pixels[offset + 1] == 255 && page.pixels[offset + 2] == 255 &&
                          page.pixels[offset + 3] == alpha,
                      "glyph RGB is white and alpha equals tight grayscale raster");
                visible = visible || alpha > 0;
            }
            else
                check(page.pixels[offset] == 0 && page.pixels[offset + 1] == 0 && page.pixels[offset + 2] == 0 &&
                          page.pixels[offset + 3] == 0,
                      "padding and unused page pixels stay transparent zero");
        }
    check(visible, "atlas contains visible coverage");
}

void saturationDefersReset(const std::shared_ptr<FontFace>& face, bool byteLimited)
{
    const auto id = face->getGlyphIndex('W');
    RasterizedGlyph raster;
    check(face->rasterizeGlyph(id, 24.0f, raster), "rasterize exact page fixture");
    const int width = raster.width + 2;
    const int height = raster.height + 2;
    const auto pageBytes = static_cast<std::size_t>(width) * height * 4;
    GlyphAtlas atlas({width, height, byteLimited ? 8u : 2u, pageBytes * 2, 1});
    const auto first = atlas.getGlyph(face, id, 24.0f);
    const auto secondFace = loadFixture();
    const auto second = atlas.getGlyph(secondFace, id, 24.0f);
    check(first && second && first->page != second->page, "full shelf spills onto a second page");
    const auto before = atlas.getStats();
    const auto originalPixels = atlas.getPages().at(first->page).pixels;
    check(!atlas.getGlyph(loadFixture(), id, 24.0f), "page or byte budget rejects a third page");
    const auto saturated = atlas.getStats();
    check(saturated.pageCount == 2 && saturated.byteSize == pageBytes * 2 && saturated.glyphCount == 2 &&
              saturated.overflows == 1 && saturated.generation == before.generation,
          "saturation respects both budgets without changing generation");
    check(atlas.getPages().at(first->page).pixels == originalPixels && atlas.getGlyph(face, id, 24.0f).has_value(),
          "already-issued glyphs remain usable throughout the saturated frame");
    atlas.beginFrame();
    const auto reset = atlas.getStats();
    check(reset.pageCount == 0 && reset.glyphCount == 0 && reset.byteSize == 0 &&
              reset.generation > saturated.generation && reset.overflows == 1,
          "next beginFrame resets saturated cache and retains lifetime counters");
    const auto after = atlas.getGlyph(face, id, 24.0f);
    check(after && after->generation == reset.generation, "new rectangles carry the new generation");
    atlas.clear();
    check(atlas.getStats().generation > after->generation && atlas.getPages().empty() &&
              atlas.getStats().glyphCount == 0,
          "explicit clear discards rectangles and advances generation");
}

void emptyAndInvalidInputsAllocateNothing(const std::shared_ptr<FontFace>& face)
{
    GlyphAtlas atlas;
    const auto space = atlas.getGlyph(face, face->getGlyphIndex(' '), 20.0f);
    check(space && space->width == 0 && space->height == 0 && space->advance > 0,
          "space is a successful empty glyph preserving advance");
    check(atlas.getPages().empty() && atlas.getStats().glyphCount == 1, "space allocates no rectangle or page");
    const auto id = face->getGlyphIndex('A');
    for (const auto size : {0.0f, -1.0f, 1024.01f, 0.001f, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()})
        check(!atlas.getGlyph(face, id, size), "invalid sizes are rejected before allocation");
    check(!atlas.getGlyph(nullptr, id, 20.0f) && !atlas.getGlyph(std::make_shared<FontFace>(), id, 20.0f) &&
              !atlas.getGlyph(face, std::numeric_limits<std::uint32_t>::max(), 20.0f),
          "null, unloaded faces and invalid glyph IDs are rejected");
    check(atlas.getPages().empty() && atlas.getStats().glyphCount == 1, "invalid inputs leave cache intact");

    const GlyphAtlasConfig configs[] = {{0, 32, 1, 4096, 1},
                                        {-1, 32, 1, 4096, 1},
                                        {32, 32, 0, 4096, 1},
                                        {32, 32, 1, 0, 1},
                                        {32, 32, 1, 4096, -1},
                                        {std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 1,
                                         std::numeric_limits<std::size_t>::max(), 1},
                                        {32, 32, 1, 4096, std::numeric_limits<int>::max()}};
    for (const auto& config : configs)
    {
        GlyphAtlas invalid(config);
        check(!invalid.getGlyph(face, id, 20.0f) && invalid.getPages().empty(),
              "invalid configurations cannot allocate a page");
    }
    GlyphAtlas tooSmall({2, 2, 1, 16, 1});
    check(!tooSmall.getGlyph(face, id, 20.0f) && tooSmall.getPages().empty(),
          "glyph larger than padded page cannot write outside its bounds");
}
} // namespace

int main()
{
    try
    {
        const auto face = loadFixture();
        cacheUsesFaceGlyphAndCanonicalSize(face);
        pixelsAndPaddingMatchRaster(face);
        saturationDefersReset(face, false);
        saturationDefersReset(face, true);
        emptyAndInvalidInputsAllocateNothing(face);
        std::cout << "Glyph atlas regressions passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
