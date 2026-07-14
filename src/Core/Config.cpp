#include "MiniEngine/Core/Config.hpp"

#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace MiniEngine
{
namespace
{
std::string Trim(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    return first < last ? std::string(first, last) : std::string{};
}
} // namespace

bool Config::Load(const std::string &path)
{
    if (!PlatformFile::Exists(path))
        return false;
    std::istringstream stream(PlatformFile::ReadAllText(path));
    if (!stream)
        return false;
    std::string line;
    while (std::getline(stream, line))
    {
        line = Trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';')
            continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        m_values[Trim(line.substr(0, equals))] = Trim(line.substr(equals + 1));
    }
    return true;
}

bool Config::Save(const std::string &path) const
{
    std::ostringstream stream;
    for (const auto &pair : m_values)
        stream << pair.first << '=' << pair.second << '\n';
    return PlatformFile::WriteAllText(path, stream.str());
}

void Config::Set(std::string key, std::string value)
{
    m_values[std::move(key)] = std::move(value);
}
std::string Config::GetString(const std::string &key, const std::string &fallback) const
{
    const auto found = m_values.find(key);
    return found == m_values.end() ? fallback : found->second;
}
int Config::GetInt(const std::string &key, const int fallback) const
{
    try
    {
        return std::stoi(GetString(key));
    }
    catch (...)
    {
        return fallback;
    }
}
float Config::GetFloat(const std::string &key, const float fallback) const
{
    try
    {
        return std::stof(GetString(key));
    }
    catch (...)
    {
        return fallback;
    }
}
bool Config::GetBool(const std::string &key, const bool fallback) const
{
    std::string value = GetString(key);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "true" || value == "1" || value == "yes" || value == "on")
        return true;
    if (value == "false" || value == "0" || value == "no" || value == "off")
        return false;
    return fallback;
}
const std::unordered_map<std::string, std::string> &Config::GetValues() const
{
    return m_values;
}
} // namespace MiniEngine
