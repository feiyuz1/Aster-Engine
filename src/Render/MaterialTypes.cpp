#include "MiniEngine/Render/MaterialTypes.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace MiniEngine
{
bool IsMaterialParameterName(const std::string& name)
{
    const auto letter = [](unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    if (name.empty() || name.rfind("te_", 0) == 0 || !letter(static_cast<unsigned char>(name[0]))) return false;
    for (unsigned char c : name) if (!letter(c) && !(c >= '0' && c <= '9')) return false;
    return true;
}
MaterialParameterType GetMaterialParameterType(const MaterialParameterValue& value)
{
    return static_cast<MaterialParameterType>(value.index());
}
const char* MaterialParameterTypeName(MaterialParameterType type)
{
    static const char* names[] = {"Float", "Vec2", "Vec3", "Vec4", "Texture2D"};
    const auto index = static_cast<std::size_t>(type);
    return index < 5 ? names[index] : "Invalid";
}
bool ParseMaterialParameterType(const std::string& text, MaterialParameterType& type)
{
    for (int i = 0; i < 5; ++i) if (text == MaterialParameterTypeName(static_cast<MaterialParameterType>(i)))
    { type = static_cast<MaterialParameterType>(i); return true; }
    return false;
}
Vec4 MaterialNumericSlot(const MaterialParameterValue& value)
{
    if (const auto* v = std::get_if<float>(&value)) return {*v, 0, 0, 0};
    if (const auto* v = std::get_if<Vec2>(&value)) return {v->x, v->y, 0, 0};
    if (const auto* v = std::get_if<Vec3>(&value)) return {v->x, v->y, v->z, 0};
    if (const auto* v = std::get_if<Vec4>(&value)) return *v;
    return {};
}
bool IsFiniteMaterialValue(const MaterialParameterValue& value)
{
    const auto v = MaterialNumericSlot(value);
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
}
bool ReadMaterialValue(std::istream& input, MaterialParameterType type, MaterialParameterValue& value)
{
    Vec4 v{};
    if (type == MaterialParameterType::Texture2D)
    {
        std::string path;
        if (!(input >> std::quoted(path))) return false;
        value = std::move(path);
        return true;
    }
    const int count = static_cast<int>(type) + 1;
    if (count < 1 || count > 4 || !(input >> v.x)) return false;
    if (count >= 2 && !(input >> v.y)) return false;
    if (count >= 3 && !(input >> v.z)) return false;
    if (count >= 4 && !(input >> v.w)) return false;
    switch (type)
    {
    case MaterialParameterType::Float: value = v.x; break;
    case MaterialParameterType::Vec2: value = Vec2{v.x, v.y}; break;
    case MaterialParameterType::Vec3: value = Vec3{v.x, v.y, v.z}; break;
    case MaterialParameterType::Vec4: value = v; break;
    default: return false;
    }
    return IsFiniteMaterialValue(value);
}
void WriteMaterialValue(std::ostream& output, const MaterialParameterValue& value)
{
    if (const auto* path = std::get_if<std::string>(&value)) { output << std::quoted(*path); return; }
    const auto v = MaterialNumericSlot(value);
    output << v.x;
    if (value.index() >= 1) output << ' ' << v.y;
    if (value.index() >= 2) output << ' ' << v.z;
    if (value.index() >= 3) output << ' ' << v.w;
}
bool ParseMaterialAsset(const std::string& source, MaterialAssetData& data, std::string& error)
{
    MaterialAssetData candidate;
    std::istringstream input(source);
    std::string header;
    int version = 0;
    if (!(input >> header >> version) || header != "TINY_MAT" || version != 2)
    { error = "Unsupported material version (expected TINY_MAT 2)"; return false; }
    candidate.formatVersion = 2;
    std::string rest;
    std::getline(input, rest);
    if (rest.find_first_not_of(" \t\r") != std::string::npos) { error = "Invalid material header"; return false; }
    std::string line;
    std::size_t number = 1;
    while (std::getline(input, line))
    {
        ++number;
        std::istringstream fields(line);
        std::string key;
        if (!(fields >> key) || key[0] == '#') continue;
        bool valid = true;
        if (key == "definition" || key == "parent")
        {
            auto& path = key == "definition" ? candidate.definitionPath : candidate.parentPath;
            valid = path.empty() && static_cast<bool>(fields >> std::quoted(path)) && !path.empty();
        }
        else if (key == "parameter" || key == "texture")
        {
            std::string name, typeName;
            MaterialParameterType type = MaterialParameterType::Texture2D;
            valid = static_cast<bool>(fields >> std::quoted(name)) && IsMaterialParameterName(name) &&
                    candidate.overrides.count(name) == 0;
            if (key == "parameter") valid = valid && static_cast<bool>(fields >> typeName) && ParseMaterialParameterType(typeName, type);
            MaterialParameterValue value;
            valid = valid && ReadMaterialValue(fields, type, value);
            if (valid) candidate.overrides.emplace(name, std::move(value));
        }
        else valid = false;
        std::string extra;
        if (!valid || ((fields >> extra) && extra[0] != '#'))
        { error = "Invalid or duplicate material field at line " + std::to_string(number); return false; }
    }
    if (candidate.definitionPath.empty() == candidate.parentPath.empty())
    { error = "Material requires exactly one definition or parent"; return false; }
    data = std::move(candidate);
    error.clear();
    return true;
}
std::string SerializeMaterialAsset(const MaterialAssetData& data)
{
    if (data.formatVersion == 1) return data.legacyData.source;
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<float>::max_digits10) << "TINY_MAT 2\n";
    if (!data.definitionPath.empty()) out << "definition " << std::quoted(data.definitionPath) << '\n';
    if (!data.parentPath.empty()) out << "parent " << std::quoted(data.parentPath) << '\n';
    for (const auto& entry : data.overrides)
    {
        const auto type = GetMaterialParameterType(entry.second);
        out << (type == MaterialParameterType::Texture2D ? "texture " : "parameter ") << std::quoted(entry.first) << ' ';
        if (type != MaterialParameterType::Texture2D) out << MaterialParameterTypeName(type) << ' ';
        WriteMaterialValue(out, entry.second);
        out << '\n';
    }
    return out.str();
}
} // namespace MiniEngine
