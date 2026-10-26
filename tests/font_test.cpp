// 字体行为回归：真实字形、塑形、资源读取及热重载所有权。
#include "tinyengine/platform/platform.h"
#include "tinyengine/resource/pak_archive.h"
#include "tinyengine/text/font.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
std::filesystem::path fixture(const char* name)
{
    for (const auto& root :
         {std::filesystem::current_path(), std::filesystem::path(__FILE__).parent_path().parent_path()})
    {
        const auto path = root / "tests/fixtures/fonts" / name;
        if (std::filesystem::exists(path))
        {
            return path;
        }
    }
    return {};
}
std::filesystem::path testFont()
{
    if (const auto* configured = std::getenv("TINYENGINE_TEST_FONT_PATH"))
    {
        return configured;
    }
    const auto bundled = fixture("noto_sans.ttf");
    if (!bundled.empty())
    {
        return bundled;
    }
    for (const char* path :
         {"C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/Library/Fonts/Arial.ttf"})
    {
        if (std::filesystem::exists(path))
        {
            return path;
        }
    }
    return {};
}
} // namespace

int main()
{
    using namespace tinyengine;
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("tinyengine_font_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        std::filesystem::create_directories(directory);
        const auto invalid = directory / "invalid.ttf";
        require(PlatformFile::writeAllBytes(invalid, {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}), "write invalid font");
        FontFace damaged;
        require(!damaged.load(invalid) && !damaged.isLoaded() && damaged.GetData().empty(),
                "reject damaged font and release invalid bytes");
        require(!damaged.load(directory / "missing.ttf"), "reject missing font");
        const auto path = testFont();
        require(!path.empty(), "no font available: set TINYENGINE_TEST_FONT_PATH");
        FontManager manager;
        const auto regular = manager.loadFace(path);
        require(regular && regular->isLoaded(), "load native font from bytes");
        require(manager.loadFace(path) == regular, "unchanged source reuses shared face");
        require(!manager.loadFace(path, -1) && !manager.loadFace(path, 1000000), "reject invalid face indices");
        const auto wideId = regular->getGlyphIndex('W'), narrowId = regular->getGlyphIndex('i');
        require(wideId && narrowId && wideId != narrowId, "character map exposes distinct glyph IDs");
        require(!regular->getGlyphIndex(0x110000) && !regular->getGlyphIndex(0xd800), "reject invalid Unicode");
        RasterizedGlyph wide, narrow, large;
        require(regular->rasterizeGlyph(wideId, 24, wide) && regular->rasterizeGlyph(narrowId, 24, narrow),
                "rasterize glyph IDs");
        require(wide.advance > narrow.advance && wide.width > narrow.width, "preserve proportional font widths");
        const auto ink = regular->getGlyphMetrics(wideId, 24);
        require(ink.width > 0 && ink.height > 0 && ink.advanceX == wide.advance && ink.bearingY > 0,
                "ink bounds without rasterization");
        require(wide.pixels.size() == static_cast<std::size_t>(wide.width * wide.height) &&
                    std::any_of(wide.pixels.begin(), wide.pixels.end(), [](auto alpha) { return alpha > 0; }),
                "tightly packed coverage");
        require(regular->rasterizeGlyph(wideId, 48, large) && large.advance > wide.advance &&
                    large.height > wide.height,
                "size changes bitmap");
        for (float size :
             {0.0f, -1.0f, 1025.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        {
            require(!regular->rasterizeGlyph(wideId, size, large) && large.pixels.empty(),
                    "reject invalid raster size");
            require(regular->shape("abc", size).empty(), "reject invalid shaping size");
        }
        require(!regular->rasterizeGlyph(std::numeric_limits<std::uint32_t>::max(), 24, large),
                "reject invalid glyph ID");
        RasterizedGlyph space;
        require(regular->rasterizeGlyph(regular->getGlyphIndex(' '), 24, space) && space.advance > 0 &&
                    space.pixels.empty(),
                "space has advance without bitmap");
        const auto metrics = regular->getMetrics(24);
        require(metrics.ascender > 0 && metrics.descender >= 0 &&
                    metrics.lineHeight >= metrics.ascender + metrics.descender - 0.1f,
                "baseline metrics");
        const auto plain = regular->shape("office", 24, false, "en", "Latn", false);
        const auto ligature = regular->shape("office", 24, false, "en", "Latn", true);
        require(plain.size() == 6 && ligature.size() < plain.size(), "optional Latin ligatures");
        require(ligature.front().cluster == 0 && ligature.back().cluster == 5, "ligature source clusters");
        const auto utf8 = regular->shape("a\xc3\xa9", 24);
        require(utf8.size() == 2 && utf8[0].cluster == 0 && utf8[1].cluster == 1, "UTF-8 byte clusters");
        const auto av = regular->shape("AV", 48), a = regular->shape("A", 48), v = regular->shape("V", 48);
        require(av.size() == 2 && av[0].advanceX + av[1].advanceX < a[0].advanceX + v[0].advanceX, "real kerning");
        const auto bold = std::make_shared<FontFace>();
        require(bold->load(path), "load independent style face");
        require(manager.registerFamily("Body", {regular, bold, {}, {}}) && manager.setDefaultFamily("Body"),
                "register default family");
        FontStyle style;
        style.family = "Body";
        require(manager.resolve(style, {'A'}) == regular, "regular style");
        style.weight = 700;
        require(manager.resolve(style, {'A'}) == bold, "bold style");
        style.weight = 400;
        require(manager.resolve(style, {'A', 0x200d, 0xfe0f}) == regular, "ignore joiner and variation controls");
        require(manager.resolve(style, {0x10ffff}) == regular, "retain primary for missing glyph");
        const auto arabicPath = fixture("noto_sans_arabic.ttf");
        if (!arabicPath.empty())
        {
            const auto arabic = manager.loadFace(arabicPath);
            require(arabic && arabic->getGlyphIndex(0x627), "load Arabic fallback");
            require(manager.registerFamily("Arabic", {arabic, {}, {}, {}}), "register Arabic fallback");
            manager.setFallbackFamilies({"Arabic"});
            require(manager.resolve(style, {0x627, 0x644}) == arabic, "fallback covers sequence");
            const auto shaped =
                arabic->shape("\xd8\xa7\xd9\x84\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", 24, true, "ar", "Arab");
            require(!shaped.empty() && shaped.front().cluster > shaped.back().cluster && shaped.front().advanceX >= 0,
                    "RTL visual order");
        }
        const auto copied = directory / "reload.ttf";
        std::filesystem::copy_file(path, copied);
        const auto old = manager.loadFace(copied);
        require(manager.registerFamily("Reload", {old, {}, {}, {}}), "register reload family");
        const auto revision = manager.getRevision();
        std::filesystem::last_write_time(copied, std::filesystem::last_write_time(copied) + std::chrono::seconds(2));
        const auto newer = manager.loadFace(copied);
        require(newer && newer != old && newer->getId() != old->getId() && manager.getRevision() > revision,
                "reload creates identity and generation");
        style.family = "Reload";
        require(manager.resolve(style, {'W'}) == newer, "reload updates family");
        std::filesystem::last_write_time(copied, std::filesystem::last_write_time(copied) + std::chrono::seconds(2));
        require(manager.reloadChanged() && manager.resolve(style, {'W'}) != newer,
                "explicit poll refreshes registered families");
        require(!manager.reloadChanged(), "unchanged source poll is stable");
        require(PlatformFile::writeAllBytes(copied, {0, 1, 0, 0}), "replace font with malformed bytes");
        require(manager.reloadChanged(), "invalid replacement advances revision");
        std::filesystem::copy_file(path, copied, std::filesystem::copy_options::overwrite_existing);
        require(manager.reloadChanged() && manager.resolve(style, {'W'})->getGlyphIndex('W'),
                "family recovers after invalid replacement");
        require(old->rasterizeGlyph(wideId, 24, large), "old face survives reload");
        const auto beforeClear = manager.getRevision();
        manager.clear();
        require(manager.getRevision() > beforeClear && old->rasterizeGlyph(wideId, 24, large),
                "clear retains externally shared faces");
        require(damaged.load(path) && !damaged.load(invalid) && !damaged.isLoaded(), "failed reload clears old state");
        const auto mount = directory / "mounted";
        std::filesystem::create_directories(mount);
        std::filesystem::copy_file(path, mount / "font.ttf");
        const auto pak = directory / "fonts.pak";
        require(PakArchive::packDirectory(mount, pak) && PlatformFile::mountPak(pak, 100), "mount font pak");
        PlatformFile::setReadPolicy(PlatformFile::ReadPolicy::MountedOnly);
        FontAsset asset;
        require(asset.load("font.ttf") && asset.isReady() && asset.getFace() && asset.getByteSize() > 12,
                "mounted font resource");
        const auto assetFace = asset.getFace();
        asset.Unload();
        require(!asset.getFace() && !asset.getByteSize() && assetFace->rasterizeGlyph(wideId, 24, large),
                "resource unload ownership");
        require(!asset.load("missing.ttf") && asset.getState() == ResourceState::Failed, "failed resource state");
        PlatformFile::clearMounts();
        for (const char* collection :
             {"C:/Windows/Fonts/msyh.ttc", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"})
        {
            if (std::filesystem::exists(collection))
            {
                FontFace first, second;
                require(first.load(collection, 0) && second.load(collection, 1), "TTC collection faces");
                break;
            }
        }
        std::filesystem::remove_all(directory);
        std::cout << "font tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        PlatformFile::clearMounts();
        std::filesystem::remove_all(directory);
        std::cerr << "font test failed: " << error.what() << '\n';
        return 1;
    }
}
