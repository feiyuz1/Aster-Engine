#include "MaterialAssetEditor.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
namespace MiniEngine
{
bool DrawMaterialValue(const char *label,MaterialParameterValue &value,const std::optional<Vec2> &range)
{
    const float speed=range ? std::max(.001f,(range->y-range->x)/200.f) : .01f;
    const float low=range ? range->x : 0, high=range ? range->y : 0;
    if (auto v=std::get_if<float>(&value)) return ImGui::DragFloat(label,v,speed,low,high);
    if (auto v=std::get_if<Vec2>(&value)) return ImGui::DragFloat2(label,&v->x,speed,low,high);
    if (auto v=std::get_if<Vec3>(&value)) return ImGui::DragFloat3(label,&v->x,speed,low,high);
    if (auto v=std::get_if<Vec4>(&value)) return ImGui::DragFloat4(label,&v->x,speed,low,high);
    auto &text=std::get<std::string>(value); std::array<char,2048> buffer{}; std::snprintf(buffer.data(),buffer.size(),"%s",text.c_str());
    if (ImGui::InputText(label,buffer.data(),buffer.size())) { text=buffer.data(); return true; } return false;
}
bool MaterialAssetEditor::Open(const std::string &path,const MaterialAssetData &data,const std::vector<MaterialParameterDecl> &parameters)
{ if (!OpenDocument(path,data)) return false; m_parameters=parameters; return true; }
bool MaterialAssetEditor::SetOverride(const std::string &name,const MaterialParameterValue &value)
{
    const auto p=std::find_if(m_parameters.begin(),m_parameters.end(),[&](const auto &p) { return p.name==name; });
    if (p==m_parameters.end() || p->type!=GetMaterialParameterType(value) || !IsFiniteMaterialValue(value)) return false;
    auto stored=value;
    if (auto texture=std::get_if<std::string>(&stored); texture && !texture->empty() && std::filesystem::u8path(*texture).is_absolute())
    {
        std::error_code error;
        const auto relative=std::filesystem::relative(std::filesystem::u8path(*texture),std::filesystem::absolute(std::filesystem::u8path(m_path)).parent_path(),error);
        if (error || relative.is_absolute()) return false;
        *texture=relative.generic_u8string();
    }
    m_draft.overrides[name]=std::move(stored); m_dirty=true; return true;
}
bool MaterialAssetEditor::ResetOverride(const std::string &name)
{ if (!m_draft.overrides.erase(name)) return false; m_dirty=true; return true; }
bool MaterialAssetEditor::CreateInstance(const std::string &path,const std::string &parent,std::string &error)
{
    MaterialAssetData data; data.formatVersion=2;
    const auto reference=parent=="builtin:PBR" ? parent : std::filesystem::relative(std::filesystem::absolute(std::filesystem::u8path(parent)),std::filesystem::absolute(std::filesystem::u8path(path)).parent_path()).generic_u8string();
    if (parent=="builtin:PBR" || std::filesystem::u8path(parent).extension()==".matgraph") data.definitionPath=reference; else data.parentPath=reference;
    if (!PlatformFile::WriteAllText(std::filesystem::u8path(path),SerializeMaterialAsset(data))) { error="Unable to create instance"; return false; } return true;
}
void MaterialAssetEditor::Draw()
{
    if (!m_visible) return;
    if (BeginDocument("Material instance / 材质实例"))
    {
        ImGui::TextWrapped("Parent: %s",(m_draft.parentPath.empty() ? m_draft.definitionPath : m_draft.parentPath).c_str());
        for (const auto &p:m_parameters)
        {
            if (p.name=="PackedEnabled" || p.name=="MetallicMapEnabled" || p.name=="RoughnessMapEnabled" || p.name=="AOMapEnabled") continue;
            const auto override=m_draft.overrides.find(p.name);
            if (override!=m_draft.overrides.end() && GetMaterialParameterType(override->second)!=p.type) continue;
            ImGui::PushID(p.name.c_str());
            bool enabled=m_draft.overrides.count(p.name)!=0;
            if (ImGui::Checkbox("Override",&enabled)) { if (enabled) SetOverride(p.name,p.defaultValue); else ResetOverride(p.name); }
            ImGui::SameLine(); auto value=enabled ? m_draft.overrides.at(p.name) : p.defaultValue;
            ImGui::BeginDisabled(!enabled);
            if (DrawMaterialValue(p.displayName.empty() ? p.name.c_str() : p.displayName.c_str(),value,p.range)) SetOverride(p.name,value);
            ImGui::EndDisabled(); ImGui::PopID();
        }
        std::string reset;
        for (const auto &entry:m_draft.overrides)
            if (std::none_of(m_parameters.begin(),m_parameters.end(),[&](const auto &p) { return p.name==entry.first && p.type==GetMaterialParameterType(entry.second); }))
            { ImGui::PushID(entry.first.c_str()); ImGui::TextDisabled("Inactive override: %s",entry.first.c_str()); ImGui::SameLine(); if (ImGui::Button("Remove inactive")) reset=entry.first; ImGui::PopID(); }
        if (!reset.empty()) ResetOverride(reset);
    }
    EndDocument();
}
}
