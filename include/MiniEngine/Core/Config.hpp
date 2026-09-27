// 文件：include/MiniEngine/Core/Config.hpp
// 作用：读取、查询和保存引擎及项目的键值配置。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：公开声明。

#pragma once

#include <string>
#include <unordered_map>

namespace MiniEngine
{
class Config
{
  public:
    /// @brief 加载数据或资源。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Load(const std::string &path);
    /// @brief 保存当前数据。
    /// @param path 文件、资源或目录路径。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Save(const std::string &path) const;
    /// @brief 设置目标对象或值。
    /// @param key 用于查找数据的键。
    /// @param value 要写入、比较或计算的值。
    void Set(std::string key, std::string value);
    /// @brief 获取 `String`。
    /// @param key 用于查找数据的键。
    /// @param fallback 查找失败时返回的默认值。
    /// @return 返回对应的文本或路径。
    std::string GetString(const std::string &key, const std::string &fallback = {}) const;
    /// @brief 获取 `Int`。
    /// @param key 用于查找数据的键。
    /// @param fallback 查找失败时返回的默认值。
    /// @return 返回计算得到的数值、数量或索引。
    int GetInt(const std::string &key, int fallback = 0) const;
    /// @brief 获取 `Float`。
    /// @param key 用于查找数据的键。
    /// @param fallback 查找失败时返回的默认值。
    /// @return 返回计算得到的数值、数量或索引。
    float GetFloat(const std::string &key, float fallback = 0.0f) const;
    /// @brief 获取 `Bool`。
    /// @param key 用于查找数据的键。
    /// @param fallback 查找失败时返回的默认值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool GetBool(const std::string &key, bool fallback = false) const;
    /// @brief 获取 `Values`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::unordered_map<std::string, std::string> &GetValues() const;

  private:
    std::unordered_map<std::string, std::string> m_values;
};
} // 结束 MiniEngine 命名空间
