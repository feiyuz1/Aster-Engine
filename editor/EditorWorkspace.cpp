#include "EditorApp.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace MiniEngine
{
namespace { ImGuiID WorkspaceId() { return ImHashStr("TinyEngineWorkspace"); } }

bool EditorApp::BeginEditorPanel(const char *english, const char *chinese, bool &visible, Vec2 position, Vec2 size, bool maximized)
{
    const bool docking = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0;
    std::string label = std::string(Localize(english, chinese)) + "###" + english;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    bool *close = std::string(english)=="Scene View" ? nullptr : &visible;
    if (maximized)
    {
        // Separate presentation window keeps the underlying dock and saved layout intact.
        label += " Focus";
        const auto *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        flags |= ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
        close = nullptr;
    }
    else if (!docking)
    {
        ImGui::SetNextWindowPos({position.x, position.y}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({std::max(1.0f,size.x), std::max(1.0f,size.y)}, ImGuiCond_Always);
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    }
    const bool wasOpen = visible;
    const bool result = ImGui::Begin(label.c_str(), close, flags);
    if (wasOpen != visible) SaveEditorSettings();
    return result;
}

void EditorApp::DrawWorkspace()
{
    if (!(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)) return;
    const auto *viewport = ImGui::GetMainViewport();
    const auto id = WorkspaceId();
    if (m_resetWorkspace || !ImGui::DockBuilderGetNode(id))
    {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);
        ImGui::DockBuilderSetNodePos(id, viewport->WorkPos);
        ImGuiID center = id;
        const auto bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, nullptr, &center);
        const auto right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
        const auto left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.23f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Scene View", center);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Console / Stats", bottom);
        ImGui::DockBuilderDockWindow("Performance", bottom);
        ImGui::DockBuilderFinish(id);
        m_resetWorkspace = false;
    }
    ImGui::DockSpaceOverViewport(id, viewport, m_sceneMaximized ? ImGuiDockNodeFlags_KeepAliveOnly : ImGuiDockNodeFlags_None);
}

void EditorApp::ResetWorkspace()
{
    m_showHierarchy = m_showInspector = m_showAssets = m_showConsole = m_showPerformance = true;
    m_sceneMaximized = false;
    m_resetWorkspace = true;
    m_leftPanelRatio=.18f; m_rightPanelRatio=.24f; m_upperPanelRatio=.66f; m_assetsPanelRatio=.62f;
    SaveEditorSettings();
}

bool EditorApp::SaveWorkspace()
{
    const auto path = m_engine.GetProjectSettings().Resolve("Saved/EditorLayout.saved.ini");
    std::error_code error; std::filesystem::create_directories(path.parent_path(),error);
    if(error) return false;
    size_t size=0; const char *data=ImGui::SaveIniSettingsToMemory(&size);
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    output.write(data,static_cast<std::streamsize>(size)); output.close();
    if(!output) return false;
    std::ofstream panels(path.parent_path()/"EditorLayout.saved.panels",std::ios::trunc);
    panels << m_showHierarchy << ' ' << m_showInspector << ' ' << m_showAssets << ' ' << m_showConsole << ' ' << m_showPerformance;
    return panels.good() && SaveEditorSettings();
}

bool EditorApp::RestoreWorkspace()
{
    const auto path=m_engine.GetProjectSettings().Resolve("Saved/EditorLayout.saved.ini");
    std::ifstream input(path,std::ios::binary);
    if(!input) return false;
    const std::string data((std::istreambuf_iterator<char>(input)),{});
    if(data.empty() || data.find("[Docking][Data]")==std::string::npos) return false;
    // Remove live nodes so loaded settings can recreate the saved hierarchy.
    ImGui::DockBuilderRemoveNode(WorkspaceId());
    ImGui::LoadIniSettingsFromMemory(data.data(),data.size());
    std::ifstream panels(path.parent_path()/"EditorLayout.saved.panels");
    bool hierarchy, inspector, assets, console, performance;
    if(panels >> hierarchy >> inspector >> assets >> console >> performance)
    { m_showHierarchy=hierarchy; m_showInspector=inspector; m_showAssets=assets; m_showConsole=console; m_showPerformance=performance; }
    m_sceneMaximized=false; m_resetWorkspace=false;
    SaveEditorSettings();
    return true;
}

void EditorApp::ApplyEditorStyle()
{
    ImGui::GetStyle() = ImGuiStyle{};
    ImGui::StyleColorsDark();
    auto &style=ImGui::GetStyle();
    style.WindowPadding={10,10}; style.FramePadding={7,4}; style.ItemSpacing={7,6}; style.ItemInnerSpacing={5,4};
    style.CellPadding={5,4}; style.WindowRounding=4; style.ChildRounding=3; style.FrameRounding=3; style.GrabRounding=3;
    style.TabRounding=3; style.WindowBorderSize=1; style.FrameBorderSize=0;
    style.Colors[ImGuiCol_WindowBg]={.105f,.11f,.12f,1};
    style.Colors[ImGuiCol_ChildBg]={.085f,.09f,.10f,1};
    style.Colors[ImGuiCol_PopupBg]={.13f,.135f,.145f,.99f};
    style.Colors[ImGuiCol_TitleBg]={.085f,.09f,.10f,1};
    style.Colors[ImGuiCol_TitleBgActive]={.16f,.17f,.19f,1};
    style.Colors[ImGuiCol_FrameBg]={.16f,.17f,.19f,1};
    style.Colors[ImGuiCol_FrameBgHovered]={.22f,.24f,.27f,1};
    style.Colors[ImGuiCol_FrameBgActive]={.25f,.29f,.34f,1};
    style.Colors[ImGuiCol_Button]={.19f,.20f,.22f,1};
    style.Colors[ImGuiCol_ButtonHovered]={.27f,.30f,.34f,1};
    style.Colors[ImGuiCol_ButtonActive]={.23f,.39f,.56f,1};
    style.Colors[ImGuiCol_Header]={.20f,.22f,.25f,1};
    style.Colors[ImGuiCol_HeaderHovered]={.25f,.32f,.40f,1};
    style.Colors[ImGuiCol_HeaderActive]={.22f,.38f,.55f,1};
    style.Colors[ImGuiCol_Tab]={.13f,.14f,.16f,1};
    style.Colors[ImGuiCol_TabSelected]={.23f,.26f,.30f,1};
    style.Colors[ImGuiCol_TabSelectedOverline]={.35f,.62f,.89f,1};
    style.Colors[ImGuiCol_TabDimmedSelected]={.19f,.21f,.24f,1};
    style.Colors[ImGuiCol_CheckMark]={.38f,.67f,.95f,1};
    style.Colors[ImGuiCol_SliderGrab]={.38f,.60f,.82f,1};
    style.Colors[ImGuiCol_Separator]={.23f,.24f,.27f,1};
    style.Colors[ImGuiCol_Border]={.24f,.25f,.28f,.7f};
    style.Colors[ImGuiCol_Text]={.90f,.91f,.93f,1};
    style.Colors[ImGuiCol_TextDisabled]={.55f,.57f,.61f,1};
    style.Colors[ImGuiCol_DockingPreview]={.32f,.59f,.86f,.7f};
    style.ScaleAllSizes(m_uiScale);
    style.FontScaleMain=m_uiScale;
}

void EditorApp::DrawPerformance()
{
    const auto *viewport=ImGui::GetMainViewport();
    if(!BeginEditorPanel("Performance","性能",m_showPerformance,{viewport->WorkPos.x,viewport->WorkPos.y+viewport->WorkSize.y*.7f},{viewport->WorkSize.x,viewport->WorkSize.y*.3f}))
    { ImGui::End(); return; }
    const Renderer *renderer = m_engine.GetContext().renderer;
    if (renderer && ImGui::BeginTable("RendererStats", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    {
        const auto stat = [](const char *label, const std::string &value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", value.c_str());
        };
        stat(Localize("Draw Calls", "绘制调用"), std::to_string(renderer->GetLastDrawCallCount()));
        stat(Localize("Culled / LOD", "剔除 / LOD"),
             std::to_string(renderer->GetLastCulledCount()) + " / " + std::to_string(renderer->GetLastLodSwitchCount()));
        std::ostringstream gpu;
        gpu << std::fixed << std::setprecision(2) << (m_device ? m_device->GetLastGpuFrameMilliseconds() : 0.0f) << " ms";
        stat("GPU", gpu.str());
    const auto &profile = renderer->GetLastFrameProfile();
        std::ostringstream cpu;
        cpu << std::fixed << std::setprecision(2) << profile.totalMilliseconds << " ms (" << Localize("Queue", "队列") << ' '
            << profile.queueMilliseconds << " | " << Localize("Shadow", "阴影") << ' ' << profile.shadowMilliseconds << " | "
            << Localize("Scene/UI", "场景/UI") << ' ' << profile.sceneMilliseconds << ')';
        stat("CPU", cpu.str());
        stat(Localize("Render Graph", "渲染图"), std::to_string(profile.graphPasses) + Localize(" passes / ", " 个 Pass / ") +
                                                       std::to_string(profile.graphPhysicalResources) +
                                                       Localize(" physical resources", " 个物理资源"));
        stat(Localize("Cluster References", "光簇引用"), std::to_string(profile.clusteredLightReferences));
        stat("HZB", std::to_string((m_device ? m_device->GetHzbLevelCount() : 0)) + Localize(" levels", " 级"));
        stat(Localize("GPU Driven", "GPU 驱动"),
             (m_device && m_device->IsGpuDrivenCullingSupported())
                 ? "Compute + Indirect"
                 : ((m_device && m_device->IsIndirectDrawSupported()) ? Localize("CPU Culling + Indirect Draw", "CPU 剔除 + 间接绘制")
                                                        : Localize("Fallback Path", "回退路径")));
        ImGui::EndTable();
    }
    ImGui::End();
}
} // namespace MiniEngine
