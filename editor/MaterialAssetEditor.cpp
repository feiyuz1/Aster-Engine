#include "MaterialAssetEditor.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
namespace MiniEngine
{
namespace
{
std::string ParameterLabel(const MaterialParameterDecl &parameter, bool chinese)
{
    const auto &display = parameter.displayName.empty() ? parameter.name : parameter.displayName;
    if (!chinese)
        return display;
    const std::pair<const char *, const char *> labels[] = {{"BaseColor", "基础颜色"},
                                                            {"Base Color", "基础颜色"},
                                                            {"Metallic", "金属度"},
                                                            {"Roughness", "粗糙度"},
                                                            {"Emissive", "自发光"},
                                                            {"Normal", "法线"},
                                                            {"NormalTS", "切线法线"},
                                                            {"AO", "环境遮蔽"},
                                                            {"Opacity", "不透明度"},
                                                            {"BaseMap", "基础颜色贴图"},
                                                            {"NormalMap", "法线贴图"},
                                                            {"MetallicMap", "金属度贴图"},
                                                            {"RoughnessMap", "粗糙度贴图"},
                                                            {"AOMap", "环境遮蔽贴图"},
                                                            {"EmissiveMap", "自发光贴图"},
                                                            {"PackedMap", "通道打包贴图"},
                                                            {"PackedLayout", "打包通道排列"},
                                                            {"Scalar parameter", "标量参数"},
                                                            {"Vector parameter", "向量参数"},
                                                            {"Texture parameter", "纹理参数"},
                                                            {"Parameter", "参数"}};
    for (const auto &entry : labels)
        if (display == entry.first)
            return entry.second;
    return display;
}
} // namespace
bool DrawMaterialValue(const char *label, MaterialParameterValue &value, const std::optional<Vec2> &range)
{
    const float speed = range ? std::max(.001f, (range->y - range->x) / 200.f) : .01f;
    const float low = range ? range->x : 0, high = range ? range->y : 0;
    if (auto v = std::get_if<float>(&value))
        return ImGui::DragFloat(label, v, speed, low, high);
    if (auto v = std::get_if<Vec2>(&value))
        return ImGui::DragFloat2(label, &v->x, speed, low, high);
    if (auto v = std::get_if<Vec3>(&value))
        return ImGui::DragFloat3(label, &v->x, speed, low, high);
    if (auto v = std::get_if<Vec4>(&value))
        return ImGui::DragFloat4(label, &v->x, speed, low, high);
    auto &text = std::get<std::string>(value);
    std::array<char, 2048> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
    if (ImGui::InputText(label, buffer.data(), buffer.size()))
    {
        text = buffer.data();
        return true;
    }
    return false;
}
bool MaterialAssetEditor::Open(const std::string &path, const MaterialAssetData &data, const std::vector<MaterialParameterDecl> &parameters)
{
    if (!OpenDocument(path, data))
        return false;
    m_parameters = parameters;
    return true;
}
bool MaterialAssetEditor::SetOverride(const std::string &name, const MaterialParameterValue &value)
{
    const auto p = std::find_if(m_parameters.begin(), m_parameters.end(), [&](const auto &p) { return p.name == name; });
    if (p == m_parameters.end() || p->type != GetMaterialParameterType(value) || !IsFiniteMaterialValue(value))
        return false;
    auto stored = value;
    if (auto texture = std::get_if<std::string>(&stored); texture && !texture->empty() && std::filesystem::u8path(*texture).is_absolute())
    {
        std::error_code error;
        const auto relative = std::filesystem::relative(std::filesystem::u8path(*texture),
                                                        std::filesystem::absolute(std::filesystem::u8path(m_path)).parent_path(), error);
        if (error || relative.is_absolute())
            return false;
        *texture = relative.generic_u8string();
    }
    m_draft.overrides[name] = std::move(stored);
    m_dirty = SerializeMaterialAsset(m_draft) != SerializeMaterialAsset(m_saved);
    return true;
}
bool MaterialAssetEditor::ResetOverride(const std::string &name)
{
    if (!m_draft.overrides.erase(name))
        return false;
    m_dirty = SerializeMaterialAsset(m_draft) != SerializeMaterialAsset(m_saved);
    return true;
}
std::vector<std::string> MaterialAssetEditor::GetRetainedAssetReferences() const
{
    std::set<std::string> references;
    const auto collect = [&](const MaterialAssetData &data) {
        for (const auto &path : {data.parentPath, data.definitionPath})
            if (!path.empty())
                references.insert(path);
        for (const auto &entry : data.overrides)
            if (const auto *path = std::get_if<std::string>(&entry.second); path && !path->empty())
                references.insert(*path);
    };
    collect(m_draft);
    collect(m_saved);
    return {references.begin(), references.end()};
}
bool MaterialAssetEditor::CreateInstance(const std::string &path, const std::string &parent, std::string &error)
{
    MaterialAssetData data;
    data.formatVersion = 2;
    const auto reference = parent == "builtin:PBR"
                               ? parent
                               : std::filesystem::relative(std::filesystem::absolute(std::filesystem::u8path(parent)),
                                                           std::filesystem::absolute(std::filesystem::u8path(path)).parent_path())
                                     .generic_u8string();
    if (parent == "builtin:PBR" || std::filesystem::u8path(parent).extension() == ".matgraph")
        data.definitionPath = reference;
    else
        data.parentPath = reference;
    if (!PlatformFile::WriteAllText(std::filesystem::u8path(path), SerializeMaterialAsset(data)))
    {
        error = "Unable to create instance";
        return false;
    }
    return true;
}
void MaterialAssetEditor::Draw()
{
    if (!m_visible)
    {
        m_focused = false;
        return;
    }
    if (BeginDocument(T("Material instance###MaterialAssetEditor", "材质实例###MaterialAssetEditor")))
    {
        ImGui::TextWrapped(T("Parent material: %s", "父材质：%s"),
                           (m_draft.parentPath.empty() ? m_draft.definitionPath : m_draft.parentPath).c_str());
        ImGui::TextWrapped("%s", T("Enable an override to replace the inherited value. Clear it to inherit from the parent.",
                                   "勾选覆盖以修改继承值；取消勾选即可恢复父材质的数值。"));
        ImGui::Separator();
        for (const auto &p : m_parameters)
        {
            if (p.name == "PackedEnabled" || p.name == "MetallicMapEnabled" || p.name == "RoughnessMapEnabled" || p.name == "AOMapEnabled")
                continue;
            const auto override = m_draft.overrides.find(p.name);
            if (override != m_draft.overrides.end() && GetMaterialParameterType(override->second) != p.type)
                continue;
            ImGui::PushID(p.name.c_str());
            bool enabled = m_draft.overrides.count(p.name) != 0;
            if (ImGui::Checkbox(T("Override###Override", "覆盖###Override"), &enabled))
            {
                if (enabled)
                    SetOverride(p.name, p.defaultValue);
                else
                    ResetOverride(p.name);
            }
            ImGui::SameLine();
            auto value = enabled ? m_draft.overrides.at(p.name) : p.defaultValue;
            ImGui::BeginDisabled(!enabled);
            const auto label = ParameterLabel(p, m_chinese) + "###Value";
            if (DrawMaterialValue(label.c_str(), value, p.range))
                SetOverride(p.name, value);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        std::string reset;
        for (const auto &entry : m_draft.overrides)
            if (std::none_of(m_parameters.begin(), m_parameters.end(),
                             [&](const auto &p) { return p.name == entry.first && p.type == GetMaterialParameterType(entry.second); }))
            {
                ImGui::PushID(entry.first.c_str());
                ImGui::TextDisabled(T("Inactive override: %s", "未生效的覆盖：%s"), entry.first.c_str());
                ImGui::SameLine();
                if (ImGui::Button(T("Remove inactive###RemoveInactive", "移除无效覆盖###RemoveInactive")))
                    reset = entry.first;
                ImGui::PopID();
            }
        if (!reset.empty())
            ResetOverride(reset);
        m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    }
    EndDocument();
}
} // namespace MiniEngine
