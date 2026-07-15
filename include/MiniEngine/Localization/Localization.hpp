#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
class Utf8
{
  public:
    static bool Decode(const std::string &text, std::vector<std::uint32_t> &codepoints);
    static std::string Encode(const std::vector<std::uint32_t> &codepoints);
};

class FontFace
{
  public:
    bool Load(const std::filesystem::path &path);
    bool IsLoaded() const;
    const std::vector<std::uint8_t> &GetData() const;
    const std::filesystem::path &GetPath() const;

  private:
    std::filesystem::path m_path;
    std::vector<std::uint8_t> m_data;
};

struct RasterizedGlyph
{
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advance = 0;
};

class FontRasterizer
{
  public:
    static bool RasterizeSystemGlyph(std::uint32_t codepoint, int pixelHeight, RasterizedGlyph &glyph,
                                     const std::wstring &fontName = L"Microsoft YaHei UI");
};

class StringTable
{
  public:
    bool Load(const std::filesystem::path &path);
    bool Save(const std::filesystem::path &path) const;
    void Set(std::string locale, std::string key, std::string value);
    std::string Get(const std::string &locale, const std::string &key, const std::string &fallback = {}) const;
    std::string Format(const std::string &locale, const std::string &key, const std::vector<std::string> &arguments) const;

  private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_values;
};
} // namespace MiniEngine
