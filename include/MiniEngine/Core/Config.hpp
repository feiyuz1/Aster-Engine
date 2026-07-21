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
    bool Load(const std::string &path);
    bool Save(const std::string &path) const;
    void Set(std::string key, std::string value);
    std::string GetString(const std::string &key, const std::string &fallback = {}) const;
    int GetInt(const std::string &key, int fallback = 0) const;
    float GetFloat(const std::string &key, float fallback = 0.0f) const;
    bool GetBool(const std::string &key, bool fallback = false) const;
    const std::unordered_map<std::string, std::string> &GetValues() const;

  private:
    std::unordered_map<std::string, std::string> m_values;
};
} // namespace MiniEngine
