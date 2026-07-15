#include "MiniEngine/Localization/Localization.hpp"

#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef TINYENGINE_HAS_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif
#include <Windows.h>
#endif

namespace MiniEngine
{
bool Utf8::Decode(const std::string &text, std::vector<std::uint32_t> &codepoints)
{
    codepoints.clear();
    for (std::size_t offset = 0; offset < text.size();)
    {
        const auto first = static_cast<std::uint8_t>(text[offset++]);
        std::uint32_t value = 0;
        int continuationCount = 0;
        if (first < 0x80)
            value = first;
        else if ((first & 0xe0u) == 0xc0u)
        {
            value = first & 0x1fu;
            continuationCount = 1;
        }
        else if ((first & 0xf0u) == 0xe0u)
        {
            value = first & 0x0fu;
            continuationCount = 2;
        }
        else if ((first & 0xf8u) == 0xf0u)
        {
            value = first & 0x07u;
            continuationCount = 3;
        }
        else
            return false;
        for (int index = 0; index < continuationCount; ++index)
        {
            if (offset >= text.size())
                return false;
            const auto next = static_cast<std::uint8_t>(text[offset++]);
            if ((next & 0xc0u) != 0x80u)
                return false;
            value = (value << 6u) | (next & 0x3fu);
        }
        if ((continuationCount == 1 && value < 0x80) || (continuationCount == 2 && value < 0x800) ||
            (continuationCount == 3 && value < 0x10000) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            return false;
        codepoints.push_back(value);
    }
    return true;
}

std::string Utf8::Encode(const std::vector<std::uint32_t> &codepoints)
{
    std::string result;
    for (const auto value : codepoints)
    {
        if (value <= 0x7f)
            result.push_back(static_cast<char>(value));
        else if (value <= 0x7ff)
        {
            result.push_back(static_cast<char>(0xc0u | (value >> 6u)));
            result.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
        }
        else if (value <= 0xffff && !(value >= 0xd800 && value <= 0xdfff))
        {
            result.push_back(static_cast<char>(0xe0u | (value >> 12u)));
            result.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3fu)));
            result.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
        }
        else if (value <= 0x10ffff)
        {
            result.push_back(static_cast<char>(0xf0u | (value >> 18u)));
            result.push_back(static_cast<char>(0x80u | ((value >> 12u) & 0x3fu)));
            result.push_back(static_cast<char>(0x80u | ((value >> 6u) & 0x3fu)));
            result.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
        }
    }
    return result;
}

bool FontFace::Load(const std::filesystem::path &path)
{
    m_path.clear();
    m_data = PlatformFile::ReadAllBytes(path);
    if (m_data.size() < 12)
        return false;
    const bool trueType = m_data[0] == 0 && m_data[1] == 1 && m_data[2] == 0 && m_data[3] == 0;
    const bool openType = std::equal(m_data.begin(), m_data.begin() + 4, "OTTO");
    if (!trueType && !openType)
    {
        m_data.clear();
        return false;
    }
    m_path = path;
    return true;
}

bool FontFace::IsLoaded() const
{
    return !m_data.empty();
}

const std::vector<std::uint8_t> &FontFace::GetData() const
{
    return m_data;
}

const std::filesystem::path &FontFace::GetPath() const
{
    return m_path;
}

bool FontRasterizer::RasterizeSystemGlyph(const std::uint32_t codepoint, const int pixelHeight, RasterizedGlyph &glyph,
                                          const std::wstring &fontName)
{
    glyph = {};
#ifdef _WIN32
    if (codepoint > 0xffff || (codepoint >= 0xd800 && codepoint <= 0xdfff) || pixelHeight <= 0)
        return false;
    HDC device = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, fontName.c_str());
    if (!device || !font)
    {
        if (font)
            DeleteObject(font);
        if (device)
            DeleteDC(device);
        return false;
    }
    const HGDIOBJ previous = SelectObject(device, font);
    MAT2 transform{{0, 1}, {0, 0}, {0, 0}, {0, 1}};
    GLYPHMETRICS metrics{};
    const DWORD size = GetGlyphOutlineW(device, codepoint, GGO_GRAY8_BITMAP, &metrics, 0, nullptr, &transform);
    if (size != GDI_ERROR)
    {
        glyph.pixels.resize(size);
        if (size > 0)
            GetGlyphOutlineW(device, codepoint, GGO_GRAY8_BITMAP, &metrics, size, glyph.pixels.data(), &transform);
        glyph.width = static_cast<int>(metrics.gmBlackBoxX);
        glyph.height = static_cast<int>(metrics.gmBlackBoxY);
        glyph.bearingX = metrics.gmptGlyphOrigin.x;
        glyph.bearingY = metrics.gmptGlyphOrigin.y;
        glyph.advance = metrics.gmCellIncX;
    }
    SelectObject(device, previous);
    DeleteObject(font);
    DeleteDC(device);
    return size != GDI_ERROR;
#else
    (void)fontName;
#ifdef TINYENGINE_HAS_FREETYPE
    if (codepoint > 0x10ffff || pixelHeight <= 0)
        return false;
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    if (FT_Init_FreeType(&library) != 0)
        return false;
    const char *configured = std::getenv("TINYENGINE_FONT_PATH");
    const char *candidates[] = {configured, "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/Library/Fonts/Arial Unicode.ttf"};
    for (const char *candidate : candidates)
        if (candidate && FT_New_Face(library, candidate, 0, &face) == 0)
            break;
    bool success = false;
    if (face && FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelHeight)) == 0 && FT_Load_Char(face, codepoint, FT_LOAD_RENDER) == 0)
    {
        const FT_GlyphSlot slot = face->glyph;
        glyph.width = static_cast<int>(slot->bitmap.width);
        glyph.height = static_cast<int>(slot->bitmap.rows);
        glyph.bearingX = slot->bitmap_left;
        glyph.bearingY = slot->bitmap_top;
        glyph.advance = static_cast<int>(slot->advance.x / 64);
        glyph.pixels.resize(static_cast<std::size_t>(glyph.width) * glyph.height);
        for (int y = 0; y < glyph.height; ++y)
            std::copy_n(slot->bitmap.buffer + static_cast<std::ptrdiff_t>(y) * slot->bitmap.pitch, glyph.width,
                        glyph.pixels.data() + static_cast<std::ptrdiff_t>(y) * glyph.width);
        success = true;
    }
    if (face)
        FT_Done_Face(face);
    FT_Done_FreeType(library);
    return success;
#else
    (void)codepoint;
    (void)pixelHeight;
    (void)fontName;
    return false;
#endif
#endif
}

bool StringTable::Load(const std::filesystem::path &path)
{
    std::istringstream input(PlatformFile::ReadAllText(path));
    std::string signature;
    int version = 0;
    if (!(input >> signature >> version) || signature != "TINYLOC" || version != 1)
        return false;
    m_values.clear();
    std::string token, locale, key, value;
    while (input >> token)
    {
        if (token != "entry" || !(input >> std::quoted(locale) >> std::quoted(key) >> std::quoted(value)))
            return false;
        Set(locale, key, value);
    }
    return true;
}

bool StringTable::Save(const std::filesystem::path &path) const
{
    std::ostringstream output;
    output << "TINYLOC 1\n";
    std::vector<std::string> locales;
    for (const auto &pair : m_values)
        locales.push_back(pair.first);
    std::sort(locales.begin(), locales.end());
    for (const auto &locale : locales)
    {
        std::vector<std::string> keys;
        for (const auto &pair : m_values.at(locale))
            keys.push_back(pair.first);
        std::sort(keys.begin(), keys.end());
        for (const auto &key : keys)
            output << "entry " << std::quoted(locale) << ' ' << std::quoted(key) << ' ' << std::quoted(m_values.at(locale).at(key)) << '\n';
    }
    return PlatformFile::WriteAllText(path, output.str());
}

void StringTable::Set(std::string locale, std::string key, std::string value)
{
    m_values[std::move(locale)][std::move(key)] = std::move(value);
}

std::string StringTable::Get(const std::string &locale, const std::string &key, const std::string &fallback) const
{
    const auto language = m_values.find(locale);
    if (language != m_values.end())
        if (const auto value = language->second.find(key); value != language->second.end())
            return value->second;
    const auto english = m_values.find("en");
    if (english != m_values.end())
        if (const auto value = english->second.find(key); value != english->second.end())
            return value->second;
    return fallback.empty() ? key : fallback;
}

std::string StringTable::Format(const std::string &locale, const std::string &key, const std::vector<std::string> &arguments) const
{
    std::string result = Get(locale, key);
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const std::string marker = "{" + std::to_string(index) + "}";
        for (std::size_t position = 0; (position = result.find(marker, position)) != std::string::npos;)
        {
            result.replace(position, marker.size(), arguments[index]);
            position += arguments[index].size();
        }
    }
    return result;
}
} // namespace MiniEngine
