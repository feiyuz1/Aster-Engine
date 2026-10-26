// 文件：include/tinyengine/text/font.h
// 作用：统一字体资源、字形度量、原生塑形以及字体家族与回退服务。
#pragma once

#include "tinyengine/resource/resource.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tinyengine
{
struct FontStyle
{
    std::string family;
    float size = 18.0f;
    int weight = 400;
    bool italic = false;
    std::string language = "und";
};

// 相对基线的距离；descender 是向下的非负距离，单位为像素。
struct FontMetrics
{
    float ascender = 0;
    float descender = 0;
    float lineGap = 0;
    float lineHeight = 0;
};

// 不生成位图即可查询的字形墨迹度量，适用于 CPU 排版边界计算。
struct FontGlyphMetrics
{
    float width = 0;
    float height = 0;
    float bearingX = 0;
    float bearingY = 0;
    float advanceX = 0;
};

// 紧密排列的单通道覆盖率，bearingY 表示字形顶边高于基线的距离。
struct RasterizedGlyph
{
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advance = 0;
};

// cluster 是相对于输入 UTF-8 的字节偏移；位置值采用像素单位。
struct FontShapingGlyph
{
    std::uint32_t glyphId = 0;
    std::size_t cluster = 0;
    float advanceX = 0;
    float advanceY = 0;
    float offsetX = 0;
    float offsetY = 0;
};

class FontFace
{
  public:
    FontFace();
    ~FontFace();
    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;
    /// @brief 从物理或挂载路径加载 TTF、OTF、TTC；失败后对象处于未加载状态。
    bool load(const std::filesystem::path& path, int faceIndex = 0);
    bool isLoaded() const;
    const std::vector<std::uint8_t>& GetData() const;
    const std::filesystem::path& GetPath() const;
    /// @brief 获取每次成功加载产生的唯一标识；未加载时为零。
    std::uint64_t getId() const;
    std::uint32_t getGlyphIndex(std::uint32_t codepoint) const;
    /// @brief 使用有限正字号（最大 1024 像素，精度为 1/64 像素）获取度量。
    FontMetrics getMetrics(float pixelSize) const;
    FontGlyphMetrics getGlyphMetrics(std::uint32_t glyphId, float pixelSize) const;
    bool rasterizeGlyph(std::uint32_t glyphId, float pixelSize, RasterizedGlyph& glyph) const;
    /// @brief 返回视觉顺序的字形；每个字形必须与当前字体实例共同使用。
    std::vector<FontShapingGlyph> shape(std::string_view utf8, float pixelSize, bool rightToLeft = false,
                                        std::string_view language = "und", std::string_view script = "",
                                        bool ligatures = true) const;
    /// @brief 在完整行的前后文中塑形一个样式片段，返回相对片段起点的 cluster。
    std::vector<FontShapingGlyph> shapeContext(std::string_view text, std::size_t begin, std::size_t length,
                                               float pixelSize, bool rightToLeft = false,
                                               std::string_view language = "und", std::string_view script = "",
                                               bool ligatures = true) const;

  private:
    friend class FontManager;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

struct FontFamily
{
    std::shared_ptr<FontFace> regular, bold, italic, boldItalic;
};

class FontAsset final : public Resource
{
  public:
    /// @brief 加载资源并发布状态；资源字节由共享字体实例持有。
    bool load(const std::string& path) override;
    void Unload() override;
    std::size_t getByteSize() const override;
    std::shared_ptr<FontFace> getFace() const;

  private:
    mutable std::mutex m_mutex;
    std::shared_ptr<FontFace> m_face;
};

class FontManager
{
  public:
    FontManager();
    ~FontManager();
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;
    static FontManager& defaultManager();
    /// @brief 缓存路径、集合索引和读取上下文；源变化时产生新字体并更新家族引用。
    std::shared_ptr<FontFace> loadFace(const std::filesystem::path& path, int faceIndex = 0);
    bool registerFamily(std::string name, FontFamily family);
    bool setDefaultFamily(const std::string& name);
    void setFallbackFamilies(std::vector<std::string> names);
    /// @brief 先选样式，再检查完整序列覆盖；缺字时保留主字体以显示缺字符号。
    std::shared_ptr<FontFace> resolve(const FontStyle& style, const std::vector<std::uint32_t>& codepoints = {}) const;
    /// @brief 检测缓存字体源的变化并更新家族引用；应在帧边界按需轮询。
    bool reloadChanged();
    /// @brief 返回服务修订号，供排版缓存失效使用；本查询不访问文件系统。
    std::uint64_t getRevision() const;
    void clear();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class FontRasterizer
{
  public:
    /// @brief 兼容系统字体入口，统一使用 FreeType 栅格化路径。
    static bool rasterizeSystemGlyph(std::uint32_t codepoint, int pixelHeight, RasterizedGlyph& glyph,
                                     const std::wstring& fontName = L"Microsoft YaHei UI");
};
} // namespace tinyengine
