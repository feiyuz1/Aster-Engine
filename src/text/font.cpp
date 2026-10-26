// 文件：src/text/font.cpp
// 作用：以持久内存字节连接 FreeType 和 HarfBuzz，并管理可热重载字体家族。
#include "tinyengine/text/font.h"
#include "tinyengine/platform/platform.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <limits>
#include <unordered_map>

namespace tinyengine
{
namespace
{
std::atomic<std::uint64_t> nextFaceId{1};

FT_F26Dot6 fontSize(const float pixelSize)
{
    if (!std::isfinite(pixelSize) || pixelSize <= 0 || pixelSize > 1024)
    {
        return 0;
    }
    return static_cast<FT_F26Dot6>(std::round(pixelSize * 64.0f));
}

bool validCodepoint(const std::uint32_t value)
{
    return value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}

bool coverageControl(const std::uint32_t value)
{
    return value == 0x200c || value == 0x200d || (value >= 0xfe00 && value <= 0xfe0f) ||
           (value >= 0xe0100 && value <= 0xe01ef);
}

std::vector<std::filesystem::path> systemFontPaths()
{
    std::vector<std::filesystem::path> paths;
    if (const char* configured = std::getenv("TINYENGINE_FONT_PATH"))
    {
        paths.emplace_back(configured);
    }
#ifdef _WIN32
    const char* windows = std::getenv("WINDIR");
    const auto root = std::filesystem::path(windows ? windows : "C:/Windows") / "Fonts";
    for (const char* name : {"msyh.ttc", "arial.ttf", "segoeui.ttf", "simsun.ttc", "malgun.ttf", "seguiemj.ttf"})
    {
        paths.push_back(root / name);
    }
#elif defined(__APPLE__)
    for (const char* path : {"/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/Supplemental/Arial.ttf",
                             "/Library/Fonts/Arial Unicode.ttf", "/System/Library/Fonts/Helvetica.ttc"})
    {
        paths.emplace_back(path);
    }
#else
    for (const char* path : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                             "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
                             "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                             "/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf",
                             "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"})
    {
        paths.emplace_back(path);
    }
#endif
    return paths;
}

std::shared_ptr<FontFace> selectVariant(const FontFamily& family, const FontStyle& style)
{
    const bool bold = style.weight >= 600;
    const auto desired =
        style.italic ? (bold ? family.boldItalic : family.italic) : (bold ? family.bold : family.regular);
    if (desired && desired->isLoaded())
    {
        return desired;
    }
    // 无精确变体时先保留斜体或粗体意图，再使用常规字体。
    const auto alternate = style.italic ? family.italic : (bold ? family.bold : family.regular);
    for (const auto& face : {alternate, family.regular, family.bold, family.italic, family.boldItalic})
    {
        if (face && face->isLoaded())
        {
            return face;
        }
    }
    return {};
}

bool covers(const std::shared_ptr<FontFace>& face, const std::vector<std::uint32_t>& points)
{
    return face &&
           std::all_of(points.begin(), points.end(),
                       [&](const auto point) { return coverageControl(point) || face->getGlyphIndex(point) != 0; });
}
} // namespace

struct FontFace::Impl
{
    mutable std::mutex mutex;
    std::vector<std::uint8_t> bytes;
    std::filesystem::path path;
    std::string fingerprint;
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    int index = 0;
    std::uint64_t id = 0;

    void reset()
    {
        if (face)
        {
            FT_Done_Face(face);
            face = nullptr;
        }
        if (library)
        {
            FT_Done_FreeType(library);
            library = nullptr;
        }
        bytes.clear();
        path.clear();
        fingerprint.clear();
        index = 0;
        id = 0;
    }
    ~Impl()
    {
        reset();
    }
    bool setSize(const float pixelSize) const
    {
        const auto size = fontSize(pixelSize);
        return face && size > 0 && FT_Set_Char_Size(face, 0, size, 72, 72) == 0;
    }
};

FontFace::FontFace() : m_impl(std::make_unique<Impl>()) {}
FontFace::~FontFace() = default;

bool FontFace::load(const std::filesystem::path& path, const int faceIndex)
{
    const std::lock_guard lock(m_impl->mutex);
    m_impl->reset();
    if (faceIndex < 0)
    {
        return false;
    }
    m_impl->bytes = PlatformFile::ReadAllBytes(path);
    if (m_impl->bytes.empty() || m_impl->bytes.size() > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max()) ||
        FT_Init_FreeType(&m_impl->library) != 0 ||
        FT_New_Memory_Face(m_impl->library, m_impl->bytes.data(), static_cast<FT_Long>(m_impl->bytes.size()), faceIndex,
                           &m_impl->face) != 0)
    {
        m_impl->reset();
        return false;
    }
    // 排版和字符覆盖依赖 Unicode cmap；只包含符号编码的资源不能作为通用文本字体。
    if (FT_Select_Charmap(m_impl->face, FT_ENCODING_UNICODE) != 0)
    {
        m_impl->reset();
        return false;
    }
    m_impl->path = path;
    m_impl->fingerprint = PlatformFile::getReadCacheKey(path);
    m_impl->index = faceIndex;
    m_impl->id = nextFaceId.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool FontFace::isLoaded() const
{
    const std::lock_guard lock(m_impl->mutex);
    return m_impl->face != nullptr;
}
const std::vector<std::uint8_t>& FontFace::GetData() const
{
    return m_impl->bytes;
}
const std::filesystem::path& FontFace::GetPath() const
{
    return m_impl->path;
}
std::uint64_t FontFace::getId() const
{
    const std::lock_guard lock(m_impl->mutex);
    return m_impl->id;
}
std::uint32_t FontFace::getGlyphIndex(const std::uint32_t codepoint) const
{
    const std::lock_guard lock(m_impl->mutex);
    return m_impl->face && validCodepoint(codepoint) ? FT_Get_Char_Index(m_impl->face, codepoint) : 0;
}

FontMetrics FontFace::getMetrics(const float pixelSize) const
{
    const std::lock_guard lock(m_impl->mutex);
    FontMetrics result;
    if (!m_impl->setSize(pixelSize))
    {
        return result;
    }
    const auto& metrics = m_impl->face->size->metrics;
    result.ascender = std::max(0.0f, metrics.ascender / 64.0f);
    result.descender = std::max(0.0f, -metrics.descender / 64.0f);
    result.lineHeight = std::max(result.ascender + result.descender, metrics.height / 64.0f);
    result.lineGap = result.lineHeight - result.ascender - result.descender;
    return result;
}

FontGlyphMetrics FontFace::getGlyphMetrics(const std::uint32_t glyphId, const float pixelSize) const
{
    const std::lock_guard lock(m_impl->mutex);
    if (!m_impl->setSize(pixelSize) || glyphId >= static_cast<std::uint64_t>(m_impl->face->num_glyphs) ||
        FT_Load_Glyph(m_impl->face, glyphId, FT_LOAD_DEFAULT) != 0)
    {
        return {};
    }
    const auto& metrics = m_impl->face->glyph->metrics;
    return {metrics.width / 64.0f, metrics.height / 64.0f, metrics.horiBearingX / 64.0f, metrics.horiBearingY / 64.0f,
            m_impl->face->glyph->advance.x / 64.0f};
}

bool FontFace::rasterizeGlyph(const std::uint32_t glyphId, const float pixelSize, RasterizedGlyph& glyph) const
{
    glyph = {};
    const std::lock_guard lock(m_impl->mutex);
    if (!m_impl->setSize(pixelSize) || glyphId >= static_cast<std::uint64_t>(m_impl->face->num_glyphs) ||
        FT_Load_Glyph(m_impl->face, glyphId, FT_LOAD_DEFAULT) != 0)
    {
        return false;
    }
    const auto slot = m_impl->face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0)
    {
        return false;
    }
    const auto& bitmap = slot->bitmap;
    if (bitmap.width > 8192 || bitmap.rows > 8192 ||
        static_cast<std::uint64_t>(bitmap.width) * bitmap.rows > 16u * 1024u * 1024u)
    {
        return false;
    }
    RasterizedGlyph result;
    result.width = static_cast<int>(bitmap.width);
    result.height = static_cast<int>(bitmap.rows);
    result.bearingX = slot->bitmap_left;
    result.bearingY = slot->bitmap_top;
    result.advance = static_cast<int>(std::lround(slot->advance.x / 64.0));
    if (bitmap.width && bitmap.rows)
    {
        const auto stride = std::abs(static_cast<std::int64_t>(bitmap.pitch));
        const auto rowBytes = bitmap.pixel_mode == FT_PIXEL_MODE_MONO ? (bitmap.width + 7u) / 8u : bitmap.width;
        if (!bitmap.buffer || stride < rowBytes ||
            (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && bitmap.pixel_mode != FT_PIXEL_MODE_MONO))
        {
            return false;
        }
        result.pixels.resize(static_cast<std::size_t>(bitmap.width) * bitmap.rows);
        for (unsigned int y = 0; y < bitmap.rows; ++y)
        {
            // FreeType 的 pitch 是按视觉顺序向下一行的有符号偏移。
            const auto* source = bitmap.buffer + static_cast<std::ptrdiff_t>(y) * bitmap.pitch;
            auto* target = result.pixels.data() + static_cast<std::size_t>(y) * bitmap.width;
            for (unsigned int x = 0; x < bitmap.width; ++x)
            {
                if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
                {
                    target[x] = (source[x / 8] & (0x80u >> (x % 8))) ? 255 : 0;
                }
                else
                {
                    const auto maximum = bitmap.num_grays > 1 ? bitmap.num_grays - 1u : 255u;
                    target[x] = static_cast<std::uint8_t>(std::min(255u, source[x] * 255u / maximum));
                }
            }
        }
    }
    glyph = std::move(result);
    return true;
}

std::vector<FontShapingGlyph> FontFace::shape(const std::string_view utf8, const float pixelSize,
                                              const bool rightToLeft, const std::string_view language,
                                              const std::string_view script, const bool ligatures) const
{
    return shapeContext(utf8, 0, utf8.size(), pixelSize, rightToLeft, language, script, ligatures);
}

std::vector<FontShapingGlyph> FontFace::shapeContext(const std::string_view utf8, const std::size_t begin,
                                                     const std::size_t length, const float pixelSize,
                                                     const bool rightToLeft, const std::string_view language,
                                                     const std::string_view script, const bool ligatures) const
{
    const std::lock_guard lock(m_impl->mutex);
    std::vector<FontShapingGlyph> result;
    if (utf8.empty() || begin > utf8.size() || length > utf8.size() - begin || length == 0 ||
        utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        language.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        script.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || !m_impl->setSize(pixelSize))
    {
        return result;
    }
    const auto font = hb_ft_font_create_referenced(m_impl->face);
    const auto buffer = hb_buffer_create();
    hb_ft_font_set_load_flags(font, FT_LOAD_DEFAULT);
    hb_buffer_add_utf8(buffer, utf8.data(), static_cast<int>(utf8.size()), static_cast<unsigned int>(begin),
                       static_cast<int>(length));
    hb_buffer_set_direction(buffer, rightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
    hb_buffer_set_language(buffer, hb_language_from_string(language.data(), static_cast<int>(language.size())));
    if (!script.empty())
    {
        hb_buffer_set_script(buffer, hb_script_from_string(script.data(), static_cast<int>(script.size())));
    }
    hb_buffer_guess_segment_properties(buffer);
    const hb_feature_t disabled[] = {{HB_TAG('l', 'i', 'g', 'a'), 0, 0, HB_FEATURE_GLOBAL_END},
                                     {HB_TAG('c', 'l', 'i', 'g'), 0, 0, HB_FEATURE_GLOBAL_END}};
    hb_shape(font, buffer, ligatures ? nullptr : disabled, ligatures ? 0u : 2u);
    unsigned int count = 0;
    const auto* infos = hb_buffer_get_glyph_infos(buffer, &count);
    const auto* positions = hb_buffer_get_glyph_positions(buffer, &count);
    result.reserve(count);
    for (unsigned int index = 0; index < count; ++index)
    {
        result.push_back({infos[index].codepoint, infos[index].cluster >= begin ? infos[index].cluster - begin : 0,
                          positions[index].x_advance / 64.0f, positions[index].y_advance / 64.0f,
                          positions[index].x_offset / 64.0f, positions[index].y_offset / 64.0f});
    }
    hb_buffer_destroy(buffer);
    hb_font_destroy(font);
    return result;
}

bool FontAsset::load(const std::string& path)
{
    const std::lock_guard lock(m_mutex);
    markLoading(path);
    m_face.reset();
    const auto face = std::make_shared<FontFace>();
    const std::u8string utf8Path(reinterpret_cast<const char8_t*>(path.data()), path.size());
    if (!face->load(std::filesystem::path(utf8Path)))
    {
        setState(ResourceState::Failed, "Unable to load font: " + path);
        return false;
    }
    m_face = face;
    setState(ResourceState::Loaded);
    return true;
}
void FontAsset::Unload()
{
    const std::lock_guard lock(m_mutex);
    m_face.reset();
    setState(ResourceState::Unloaded);
}
std::size_t FontAsset::getByteSize() const
{
    const std::lock_guard lock(m_mutex);
    return m_face ? m_face->GetData().size() : 0;
}
std::shared_ptr<FontFace> FontAsset::getFace() const
{
    const std::lock_guard lock(m_mutex);
    return m_face;
}

struct FontManager::Impl
{
    struct Entry
    {
        std::filesystem::path path;
        int index = 0;
        std::string fingerprint;
        std::shared_ptr<FontFace> face;
    };
    mutable std::mutex mutex;
    std::unordered_map<std::string, Entry> cache;
    std::unordered_map<std::string, FontFamily> families;
    std::unordered_map<std::string, std::array<std::string, 4>> bindings;
    std::string defaultFamily;
    std::vector<std::string> fallbacks;
    std::uint64_t revision = 1;
    bool discovered = false;

    static std::string cacheKey(const std::filesystem::path& path, const int index)
    {
        const auto normalized = path.lexically_normal().generic_u8string();
        return std::to_string(index) + ':' +
               std::string(reinterpret_cast<const char*>(normalized.data()), normalized.size());
    }
    void replaceReferences(const std::string& key, const std::shared_ptr<FontFace>& newFace)
    {
        for (auto& [name, family] : families)
        {
            const auto found = bindings.find(name);
            if (found == bindings.end())
            {
                continue;
            }
            const std::array slots{&family.regular, &family.bold, &family.italic, &family.boldItalic};
            for (std::size_t index = 0; index < slots.size(); ++index)
            {
                if (found->second[index] == key)
                {
                    *slots[index] = newFace;
                }
            }
        }
    }
    std::shared_ptr<FontFace> load(const std::filesystem::path& path, const int index)
    {
        if (path.empty() || index < 0)
        {
            return {};
        }
        const auto key = cacheKey(path, index);
        auto& entry = cache[key];
        const auto fingerprint = PlatformFile::getReadCacheKey(path);
        if (entry.fingerprint == fingerprint)
        {
            return entry.face;
        }
        auto face = std::make_shared<FontFace>();
        if (!face->load(path, index))
        {
            face.reset();
        }
        replaceReferences(key, face);
        entry = {path, index, fingerprint, face};
        ++revision;
        return face;
    }
    void refresh()
    {
        // load 只更新已有条目，因此遍历时不会导致散列表重建。
        for (auto& [key, entry] : cache)
        {
            load(entry.path, entry.index);
        }
    }
    void discover()
    {
        if (discovered || !families.empty())
        {
            return;
        }
        discovered = true;
        for (const auto& path : systemFontPaths())
        {
            const auto face = load(path, 0);
            if (!face)
            {
                continue;
            }
            const auto name = "__system_" + std::to_string(families.size());
            families.emplace(name, FontFamily{face, {}, {}, {}});
            bindings[name][0] = cacheKey(path, 0);
            if (defaultFamily.empty())
            {
                defaultFamily = name;
            }
            else
            {
                fallbacks.push_back(name);
            }
        }
        ++revision;
    }
};

FontManager::FontManager() : m_impl(std::make_unique<Impl>()) {}
FontManager::~FontManager() = default;
FontManager& FontManager::defaultManager()
{
    static FontManager manager;
    return manager;
}
std::shared_ptr<FontFace> FontManager::loadFace(const std::filesystem::path& path, const int faceIndex)
{
    const std::lock_guard lock(m_impl->mutex);
    return m_impl->load(path, faceIndex);
}
bool FontManager::registerFamily(std::string name, FontFamily family)
{
    if (name.empty())
    {
        return false;
    }
    bool hasFace = false;
    for (const auto& face : {family.regular, family.bold, family.italic, family.boldItalic})
    {
        if (face && !face->isLoaded())
        {
            return false;
        }
        hasFace = hasFace || static_cast<bool>(face);
    }
    if (!hasFace)
    {
        return false;
    }
    const std::lock_guard lock(m_impl->mutex);
    std::array<std::string, 4> sources;
    const std::array faces{family.regular, family.bold, family.italic, family.boldItalic};
    for (std::size_t index = 0; index < faces.size(); ++index)
    {
        const auto& face = faces[index];
        if (!face)
        {
            continue;
        }
        const std::lock_guard faceLock(face->m_impl->mutex);
        const auto& native = *face->m_impl;
        sources[index] = Impl::cacheKey(native.path, native.index);
        m_impl->cache.try_emplace(sources[index], Impl::Entry{native.path, native.index, native.fingerprint, face});
    }
    if (m_impl->defaultFamily.empty())
    {
        m_impl->defaultFamily = name;
    }
    m_impl->bindings[name] = std::move(sources);
    m_impl->families[std::move(name)] = std::move(family);
    ++m_impl->revision;
    return true;
}
bool FontManager::setDefaultFamily(const std::string& name)
{
    const std::lock_guard lock(m_impl->mutex);
    if (!m_impl->families.contains(name))
    {
        return false;
    }
    if (m_impl->defaultFamily != name)
    {
        m_impl->defaultFamily = name;
        ++m_impl->revision;
    }
    return true;
}
void FontManager::setFallbackFamilies(std::vector<std::string> names)
{
    const std::lock_guard lock(m_impl->mutex);
    if (m_impl->fallbacks != names)
    {
        m_impl->fallbacks = std::move(names);
        ++m_impl->revision;
    }
}
std::shared_ptr<FontFace> FontManager::resolve(const FontStyle& style,
                                               const std::vector<std::uint32_t>& codepoints) const
{
    const std::lock_guard lock(m_impl->mutex);
    m_impl->discover();
    const auto findFamily = [&](const std::string& name) -> std::shared_ptr<FontFace>
    {
        const auto found = m_impl->families.find(name);
        return found == m_impl->families.end() ? nullptr : selectVariant(found->second, style);
    };
    auto primary = findFamily(style.family);
    if (!primary)
    {
        primary = findFamily(m_impl->defaultFamily);
    }
    if (covers(primary, codepoints))
    {
        return primary;
    }
    for (const auto& name : m_impl->fallbacks)
    {
        const auto face = findFamily(name);
        if (covers(face, codepoints))
        {
            return face;
        }
    }
    return primary;
}
std::uint64_t FontManager::getRevision() const
{
    const std::lock_guard lock(m_impl->mutex);
    return m_impl->revision;
}
bool FontManager::reloadChanged()
{
    const std::lock_guard lock(m_impl->mutex);
    const auto before = m_impl->revision;
    m_impl->refresh();
    return m_impl->revision != before;
}
void FontManager::clear()
{
    const std::lock_guard lock(m_impl->mutex);
    m_impl->cache.clear();
    m_impl->families.clear();
    m_impl->bindings.clear();
    m_impl->defaultFamily.clear();
    m_impl->fallbacks.clear();
    m_impl->discovered = false;
    ++m_impl->revision;
}

bool FontRasterizer::rasterizeSystemGlyph(const std::uint32_t codepoint, const int pixelHeight, RasterizedGlyph& glyph,
                                          const std::wstring& fontName)
{
    glyph = {};
    if (!validCodepoint(codepoint) || !fontSize(static_cast<float>(pixelHeight)))
    {
        return false;
    }
    auto& manager = FontManager::defaultManager();
    std::wstring lower = fontName;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](const auto value) { return std::towlower(value); });
    for (const auto& path : systemFontPaths())
    {
        const auto filename = path.filename().wstring();
        const bool preferred = (lower.find(L"arial") != std::wstring::npos && filename == L"arial.ttf") ||
                               (lower.find(L"yahei") != std::wstring::npos && filename == L"msyh.ttc") ||
                               (lower.find(L"segoe") != std::wstring::npos && filename == L"segoeui.ttf");
        if (preferred)
        {
            const auto face = manager.loadFace(path);
            const auto id = face ? face->getGlyphIndex(codepoint) : 0;
            if (id && face->rasterizeGlyph(id, static_cast<float>(pixelHeight), glyph))
            {
                return true;
            }
        }
    }
    const auto face = manager.resolve({}, {codepoint});
    const auto id = face ? face->getGlyphIndex(codepoint) : 0;
    return id && face->rasterizeGlyph(id, static_cast<float>(pixelHeight), glyph);
}
} // namespace tinyengine
