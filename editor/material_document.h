#pragma once
#include "tinyengine/render/material_types.h"
#include <algorithm>
#include <functional>
#include <imgui.h>
#include <utility>

namespace tinyengine
{
template <class Data> class MaterialDocument
{
  public:
    using SaveRequest = std::function<bool(const std::string&, const Data&, std::vector<MaterialDiagnostic>&)>;
    virtual ~MaterialDocument() = default;
    const Data& GetDraft() const
    {
        return m_draft;
    }
    bool isDirty() const
    {
        return m_dirty;
    }
    const std::string& GetPath() const
    {
        return m_path;
    }
    const std::vector<MaterialDiagnostic>& GetDiagnostics() const
    {
        return m_diagnostics;
    }
    void setSaveRequest(SaveRequest callback)
    {
        m_save = std::move(callback);
    }
    void setChinese(bool chinese)
    {
        m_chinese = chinese;
    }
    bool isFocused() const
    {
        return m_visible && m_focused;
    }
    bool isVisible() const
    {
        return m_visible;
    }
    void hide()
    {
        m_visible = false;
        m_focused = false;
        m_closeRequested = false;
    }
    void setApplyRequest(std::function<bool(const std::string&)> callback, bool canApply = true)
    {
        m_apply = std::move(callback);
        m_canApply = canApply;
    }
    bool apply()
    {
        if (!m_apply || !m_canApply || (m_dirty && !Save()))
            return false;
        if (m_apply(m_path))
            return true;
        m_diagnostics.push_back(
            {MaterialDiagnosticSeverity::Error, m_path, 0, -1, "Unable to apply material to the selected mesh."});
        return false;
    }
    void refreshFromDisk(const Data& data)
    {
        m_saved = data;
        if (!m_dirty)
        {
            m_draft = data;
            m_externalConflict = false;
            m_diagnostics.clear();
            onDocumentReset();
        }
        else
        {
            m_externalConflict = true;
            m_diagnostics = {{MaterialDiagnosticSeverity::Warning, m_path, 0, -1,
                              "External reload: draft retained. Discard edits to use the current file before saving."}};
        }
    }
    bool Save()
    {
        if (m_externalConflict)
        {
            m_diagnostics = {{MaterialDiagnosticSeverity::Error, m_path, 0, -1,
                              "External reload conflict: discard the stale baseline before saving."}};
            return false;
        }
        m_diagnostics.clear();
        if (!validateDraft())
            return false;
        if (!m_save || !m_save(m_path, m_draft, m_diagnostics))
            return false;
        m_saved = m_draft;
        m_dirty = false;
        return true;
    }
    void Discard()
    {
        m_draft = m_saved;
        m_dirty = false;
        m_externalConflict = false;
        m_diagnostics.clear();
        onDocumentReset();
    }

  protected:
    const char* T(const char* english, const char* chinese) const
    {
        return m_chinese ? chinese : english;
    }
    virtual bool validateDraft()
    {
        return true;
    }
    virtual void onDocumentReset() {}
    std::string diagnosticText(const std::string& message) const
    {
        if (!m_chinese)
            return message;
        const std::pair<const char*, const char*> translations[] = {
            {"External reload: draft retained. Discard edits to use the current file before saving.",
             "文件已在外部更新。当前草稿已保留；请放弃修改以载入最新文件。"},
            {"External reload conflict: discard the stale baseline before saving.",
             "文件存在外部修改冲突。请先放弃旧草稿，再保存最新版本。"},
            {"Save or discard this draft before switching assets.", "请先保存或放弃当前草稿，再切换资源。"},
            {"Unable to apply material to the selected mesh.", "无法将材质应用到选中的网格。"},
            {"Material requires exactly one SurfaceOutput", "材质必须且只能有一个表面输出节点"},
            {"Missing node, invalid port or duplicate input link", "节点缺失、端口无效或输入存在重复连线"},
            {"Reachable material graph cycle", "材质图中存在循环连接"},
            {"Unreachable cycle ignored", "已忽略未连接到输出的循环"},
            {"Only UV0 is supported", "当前仅支持 UV0"},
            {"Parameter node does not match its declaration", "参数节点与参数声明不匹配"},
            {"ComponentMask channels exceed the numeric input dimension", "分量遮罩超出了输入向量的维度"},
            {"TextureSample requires Texture2D and Vec2 UV", "纹理采样需要二维纹理和二维 UV 坐标"},
            {"Numeric inputs must have matching dimensions or Float broadcast", "数值输入需要维度相同，或使用标量广播"},
            {"Clamp minimum and maximum must be Float", "限制节点的最小值和最大值必须为标量"},
            {"Lerp alpha must be Float", "插值系数必须为标量"},
            {"Node requires a numeric input", "此节点需要数值输入"},
            {"Invalid numeric Constant", "常量的数值或类型无效"},
            {"Unsupported graph version or invalid render settings", "材质图版本不受支持或渲染设置无效"},
            {"Material exceeds 64 numeric or 8 texture parameters", "材质参数超过 64 个数值或 8 个纹理的限制"},
            {"Invalid, duplicate or mismatched parameter declaration: ", "参数声明无效、重名或类型不匹配："},
            {"Missing parameter declaration: ", "缺少参数声明："},
            {"Expected ", "需要的类型："},
            {", got ", "，实际类型："}};
        std::string text = message;
        for (const auto& entry : translations)
        {
            std::size_t offset = 0;
            while ((offset = text.find(entry.first, offset)) != std::string::npos)
            {
                text.replace(offset, std::char_traits<char>::length(entry.first), entry.second);
                offset += std::char_traits<char>::length(entry.second);
            }
        }
        return text;
    }
    bool openDocument(const std::string& path, const Data& data)
    {
        if (m_dirty && path == m_path)
        {
            m_visible = true;
            return true;
        }
        if (m_dirty && path != m_path)
        {
            m_diagnostics = {{MaterialDiagnosticSeverity::Warning, m_path, 0, -1,
                              "Save or discard this draft before switching assets."}};
            return false;
        }
        m_path = path;
        m_draft = m_saved = data;
        m_dirty = false;
        m_externalConflict = false;
        m_visible = true;
        m_focused = false;
        m_diagnostics.clear();
        onDocumentReset();
        return true;
    }
    bool beginDocument(const char* title)
    {
        const auto* viewport = ImGui::GetMainViewport();
        const ImVec2 initialSize{std::max(1.f, std::min(1200.f, viewport->WorkSize.x - 32.f)),
                                 std::max(1.f, std::min(760.f, viewport->WorkSize.y - 32.f))};
        ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos({viewport->WorkPos.x + (viewport->WorkSize.x - initialSize.x) * .5f,
                                 viewport->WorkPos.y + (viewport->WorkSize.y - initialSize.y) * .5f},
                                ImGuiCond_FirstUseEver);
        bool open = true;
        const bool contents = ImGui::Begin(title, &open, m_dirty ? ImGuiWindowFlags_UnsavedDocument : 0);
        m_focused = m_visible && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (!open)
        {
            if (m_dirty)
                m_closeRequested = true;
            else
                hide();
        }
        if (!contents)
            return false;
        ImGui::TextUnformatted(m_path.c_str());
        if (ImGui::Button(T("Save###SaveMaterial", "保存###SaveMaterial")))
            Save();
        ImGui::SameLine();
        if (ImGui::Button(T("Discard edits###DiscardMaterial", "放弃修改###DiscardMaterial")))
            Discard();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_apply || !m_canApply);
        if (ImGui::Button(T("Apply to selected mesh###ApplyMaterial", "应用到选中网格###ApplyMaterial")))
            apply();
        ImGui::EndDisabled();
        if ((!m_apply || !m_canApply) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(
                "%s", T("Select a mesh in the scene to apply this material.", "请先在场景中选择网格，再应用此材质。"));
        ImGui::BeginChild("MaterialStatus", {0, 38}, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollWithMouse);
        if (m_diagnostics.empty())
            ImGui::TextDisabled("%s", m_dirty ? T("Draft has unsaved changes.", "草稿有尚未保存的修改。")
                                              : T("Material is saved.", "材质已保存。"));
        for (const auto& d : m_diagnostics)
            ImGui::TextWrapped(T("%s (node %u, port %d)", "%s（节点 %u，端口 %d）"), diagnosticText(d.message).c_str(),
                               d.nodeId, d.port);
        ImGui::EndChild();
        return contents;
    }
    void endDocument()
    {
        const auto& io = ImGui::GetIO();
        if (m_visible && m_focused && io.KeyCtrl && !io.WantTextInput &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
            ImGui::IsKeyPressed(ImGuiKey_S, false))
            Save();
        const auto dialog = T("Unsaved material###MaterialCloseDialog", "材质尚未保存###MaterialCloseDialog");
        if (m_closeRequested)
        {
            ImGui::OpenPopup(dialog);
            m_closeRequested = false;
        }
        if (ImGui::BeginPopupModal(dialog, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(T("Save material changes before closing?", "关闭前是否保存材质修改？"));
            if (ImGui::Button(T("Save###Save", "保存###Save")) && Save())
            {
                hide();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(T("Discard###Discard", "放弃###Discard")))
            {
                Discard();
                hide();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(T("Cancel###Cancel", "取消###Cancel")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::End();
    }
    Data m_draft{}, m_saved{};
    std::string m_path;
    std::vector<MaterialDiagnostic> m_diagnostics;
    bool m_dirty = false, m_visible = false, m_closeRequested = false, m_focused = false, m_chinese = false,
         m_canApply = true;
    bool m_externalConflict = false;
    SaveRequest m_save;
    std::function<bool(const std::string&)> m_apply;
};
bool drawMaterialValue(const char* label, MaterialParameterValue& value, const std::optional<Vec2>& range = {});
} // namespace tinyengine
