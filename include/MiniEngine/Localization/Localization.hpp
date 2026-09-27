// 文件：include/MiniEngine/Localization/Localization.hpp
// 作用：提供中英文界面文本表、语言切换和键值回退机制。
// 约定：文本键是稳定标识；缺失翻译时回退而不是返回空字符串。
// 类型：公开声明。

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
    /// @brief 解码输入数据并验证其有效性。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @param codepoints Unicode 码点序列。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool Decode(const std::string &text, std::vector<std::uint32_t> &codepoints);
    /// @brief 将输入数据编码为目标格式。
    /// @param codepoints Unicode 码点序列。
    /// @return 返回对应的文本或路径。
    static std::string Encode(const std::vector<std::uint32_t> &codepoints);
};

class FontFace
{
  public:
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::filesystem::path &path);
    /// @brief 判断是否满足 `Loaded` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsLoaded() const;
    /// @brief 获取 `Data`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<std::uint8_t> &GetData() const;
    /// @brief 获取 `Path`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
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
    /// @brief 从系统字体栅格化指定 Unicode 字形。
    /// @param codepoint 要处理的 Unicode 码点。
    /// @param pixelHeight 目标尺寸、数量、索引或限制值。
    /// @param glyph 接收调用结果的输出参数。
    /// @param fontName 用于查找或标识对象的名称。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool RasterizeSystemGlyph(std::uint32_t codepoint, int pixelHeight, RasterizedGlyph &glyph,
                                     const std::wstring &fontName = L"Microsoft YaHei UI");
};

class StringTable
{
  public:
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::filesystem::path &path);
    /// @brief 保存当前数据。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Save(const std::filesystem::path &path) const;
    /// @brief 设置目标对象或值。
    /// @param locale 语言与地区标识。
    /// @param key 用于查找数据的键。
    /// @param value 要写入、比较或计算的值。
    void Set(std::string locale, std::string key, std::string value);
    /// @brief 获取目标对象或值。
    /// @param locale 语言与地区标识。
    /// @param key 用于查找数据的键。
    /// @param fallback 查找失败时返回的默认值。
    /// @return 返回对应的文本或路径。
    std::string Get(const std::string &locale, const std::string &key, const std::string &fallback = {}) const;
    /// @brief 格式化文本并替换参数占位符。
    /// @param locale 语言与地区标识。
    /// @param key 用于查找数据的键。
    /// @param arguments 命令、脚本或格式化参数集合。
    /// @return 返回对应的文本或路径。
    std::string Format(const std::string &locale, const std::string &key, const std::vector<std::string> &arguments) const;

  private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_values;
};
} // 结束 MiniEngine 命名空间
