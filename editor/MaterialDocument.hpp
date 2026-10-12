#pragma once
#include "MiniEngine/Render/MaterialTypes.hpp"
#include <imgui.h>
#include <functional>

namespace MiniEngine
{
template<class Data> class MaterialDocument
{
public:
    using SaveRequest=std::function<bool(const std::string &,const Data &,std::vector<MaterialDiagnostic> &)>;
    const Data &GetDraft() const { return m_draft; }
    bool IsDirty() const { return m_dirty; }
    const std::string &GetPath() const { return m_path; }
    const std::vector<MaterialDiagnostic> &GetDiagnostics() const { return m_diagnostics; }
    void SetSaveRequest(SaveRequest callback) { m_save=std::move(callback); }
    void RefreshFromDisk(const Data &data)
    {
        m_saved=data;
        if (!m_dirty) { m_draft=data; m_externalConflict=false; m_diagnostics.clear(); }
        else { m_externalConflict=true; m_diagnostics={{MaterialDiagnosticSeverity::Warning,m_path,0,-1,"External reload: draft retained. Discard edits to use the current file before saving."}}; }
    }
    bool Save()
    {
        if (m_externalConflict) { m_diagnostics={{MaterialDiagnosticSeverity::Error,m_path,0,-1,"External reload conflict: discard the stale baseline before saving."}}; return false; }
        m_diagnostics.clear();
        if (!m_save || !m_save(m_path,m_draft,m_diagnostics)) return false;
        m_saved=m_draft; m_dirty=false; return true;
    }
    void Discard() { m_draft=m_saved; m_dirty=false; m_externalConflict=false; m_diagnostics.clear(); }
protected:
    bool OpenDocument(const std::string &path,const Data &data)
    {
        if (m_dirty && path==m_path) { m_visible=true; return true; }
        if (m_dirty && path!=m_path)
        { m_diagnostics={{MaterialDiagnosticSeverity::Warning,m_path,0,-1,"Save or discard this draft before switching assets."}}; return false; }
        m_path=path; m_draft=m_saved=data; m_dirty=false; m_externalConflict=false; m_visible=true; m_diagnostics.clear(); return true;
    }
    bool BeginDocument(const char *title)
    {
        ImGui::SetNextWindowSize({850,650},ImGuiCond_FirstUseEver);
        bool open=true; const bool contents=ImGui::Begin(title,&open,m_dirty ? ImGuiWindowFlags_UnsavedDocument : 0);
        if (!open) { if (m_dirty) m_closeRequested=true; else m_visible=false; }
        ImGui::TextUnformatted(m_path.c_str());
        if (ImGui::Button("Save / Apply")) Save(); ImGui::SameLine(); if (ImGui::Button("Discard edits")) Discard();
        for (const auto &d:m_diagnostics) ImGui::TextWrapped("%s (node %u, port %d)",d.message.c_str(),d.nodeId,d.port);
        return contents;
    }
    void EndDocument()
    {
        if (m_closeRequested) { ImGui::OpenPopup("Unsaved material"); m_closeRequested=false; }
        if (ImGui::BeginPopupModal("Unsaved material",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Save material changes before closing?");
            if (ImGui::Button("Save") && Save()) { m_visible=false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("Discard")) { Discard(); m_visible=false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::End();
    }
    Data m_draft{},m_saved{};
    std::string m_path;
    std::vector<MaterialDiagnostic> m_diagnostics;
    bool m_dirty=false,m_visible=false,m_closeRequested=false;
    bool m_externalConflict=false;
    SaveRequest m_save;
};
bool DrawMaterialValue(const char *label,MaterialParameterValue &value,const std::optional<Vec2> &range={});
}
