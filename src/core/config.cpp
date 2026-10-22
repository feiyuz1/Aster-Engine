// 文件：src/core/config.cpp
// 作用：读取、查询和保存引擎及项目的键值配置。
// 约定：核心层不依赖上层业务，修改公开契约时需检查所有模块。
// 类型：实现文件。

#include "tinyengine/core/config.h"

#include "tinyengine/platform/platform.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace tinyengine
{
namespace
{
std::string trim(std::string value)
{
    const auto first =
        std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last =
        std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    return first < last ? std::string(first, last) : std::string{};
}
} // namespace

bool Config::load(const std::string& path)
{
    if (!PlatformFile::exists(fromUtf8Path(path)))
        return false;
    std::istringstream stream(PlatformFile::readAllText(fromUtf8Path(path)));
    if (!stream)
        return false;
    std::string line;
    while (std::getline(stream, line))
    {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';')
            continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        m_values[trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }
    return true;
}

bool Config::Save(const std::string& path) const
{
    std::ostringstream stream;
    for (const auto& pair : m_values)
        stream << pair.first << '=' << pair.second << '\n';
    return PlatformFile::writeAllText(fromUtf8Path(path), stream.str());
}

void Config::set(std::string key, std::string value)
{
    m_values[std::move(key)] = std::move(value);
}
std::string Config::getString(const std::string& key, const std::string& fallback) const
{
    const auto found = m_values.find(key);
    return found == m_values.end() ? fallback : found->second;
}
int Config::getInt(const std::string& key, const int fallback) const
{
    try
    {
        return std::stoi(getString(key));
    }
    catch (...)
    {
        return fallback;
    }
}
float Config::getFloat(const std::string& key, const float fallback) const
{
    try
    {
        return std::stof(getString(key));
    }
    catch (...)
    {
        return fallback;
    }
}
bool Config::getBool(const std::string& key, const bool fallback) const
{
    std::string value = getString(key);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "true" || value == "1" || value == "yes" || value == "on")
        return true;
    if (value == "false" || value == "0" || value == "no" || value == "off")
        return false;
    return fallback;
}
const std::unordered_map<std::string, std::string>& Config::GetValues() const
{
    return m_values;
}
} // namespace tinyengine
