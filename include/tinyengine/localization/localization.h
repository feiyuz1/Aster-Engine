// 文件：include/tinyengine/localization/localization.h
// 作用：提供中英文界面文本表、语言切换和键值回退机制。
// 约定：文本键是稳定标识；缺失翻译时回退而不是返回空字符串。
// 类型：公开声明。

#pragma once

#include "tinyengine/text/font.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace tinyengine
{
class Utf8
{
  public:
    /// @brief 解码输入数据并验证其有效性。
    /// @param text 要处理的 UTF-8 或普通文本。
    /// @param codepoints Unicode 码点序列。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    static bool decode(const std::string& text, std::vector<std::uint32_t>& codepoints);
    /// @brief 将输入数据编码为目标格式。
    /// @param codepoints Unicode 码点序列。
    /// @return 返回对应的文本或路径。
    static std::string encode(const std::vector<std::uint32_t>& codepoints);
};

class StringTable
{
  public:
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool load(const std::filesystem::path& path);
    /// @brief 保存当前数据。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Save(const std::filesystem::path& path) const;
    /// @brief 设置目标对象或值。
    /// @param locale 语言与地区标识。
    /// @param key 用于查找数据的键。
    /// @param value 要写入、比较或计算的值。
    void set(std::string locale, std::string key, std::string value);
    /// @brief 获取目标对象或值。
    /// @param locale 语言与地区标识。
    /// @param key 用于查找数据的键。
    /// @param fallback 查找失败时返回的默认值。
    /// @return 返回对应的文本或路径。
    std::string Get(const std::string& locale, const std::string& key, const std::string& fallback = {}) const;
    /// @brief 格式化文本并替换参数占位符。
    /// @param locale 语言与地区标识。
    /// @param key 用于查找数据的键。
    /// @param arguments 命令、脚本或格式化参数集合。
    /// @return 返回对应的文本或路径。
    std::string format(const std::string& locale, const std::string& key,
                       const std::vector<std::string>& arguments) const;

  private:
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_values;
};
} // namespace tinyengine
