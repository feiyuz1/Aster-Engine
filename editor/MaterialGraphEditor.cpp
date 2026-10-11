#include "MaterialGraphEditor.hpp"
#include "MiniEngine/Render/MaterialCompiler.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
namespace MiniEngine
{
namespace {
std::vector<const char *> Inputs(MaterialNodeType type)
{
    switch (type) {
    case MaterialNodeType::SurfaceOutput:return {"Base Color (Vec3)","Metallic (Float)","Roughness (Float)","Normal TS (Vec3)","AO (Float)","Emissive (Vec3)","Opacity (Float)"};
    case MaterialNodeType::Add:case MaterialNodeType::Multiply:return {"A","B"};
    case MaterialNodeType::Lerp:return {"A","B","Alpha"};
    case MaterialNodeType::Clamp:return {"Value","Min","Max"};
    case MaterialNodeType::TextureSample:return {"Texture2D","UV (Vec2)"};
    case MaterialNodeType::ComponentMask:return {"Vector"};
    case MaterialNodeType::NormalDecode:return {"RGB (Vec3)"};
    case MaterialNodeType::OneMinus:return {"Value"}; default:return {};
    }
}
void TextField(const char *label,std::string &text,bool &dirty)
{ std::array<char,256> buffer{}; std::snprintf(buffer.data(),buffer.size(),"%s",text.c_str()); if (ImGui::InputText(label,buffer.data(),buffer.size())) { text=buffer.data(); dirty=true; } }
}
std::uint32_t MaterialGraphEditor::AddNode(MaterialNodeType type,const std::string &name)
{ m_dirty=true; m_selected=m_draft.graph.AddNode(type,name,{40.f+static_cast<float>(m_draft.graph.GetNodes().size()%3)*230.f,50.f+static_cast<float>(m_draft.graph.GetNodes().size()/3)*230.f}); if (type==MaterialNodeType::ComponentMask) m_draft.graph.GetNodes().back().channelMask="xyz"; return m_selected; }
bool MaterialGraphEditor::DisconnectPort(std::uint32_t to,std::uint32_t input)
{ auto &links=m_draft.graph.GetLinks(); const auto old=links.size(); links.erase(std::remove_if(links.begin(),links.end(),[&](const auto &l) { return l.toNode==to && l.toInput==input; }),links.end()); m_dirty=m_dirty || old!=links.size(); return old!=links.size(); }
bool MaterialGraphEditor::ConnectPorts(std::uint32_t from,std::uint32_t output,std::uint32_t to,std::uint32_t input)
{
    auto candidate=m_draft; auto &links=candidate.graph.GetLinks();
    links.erase(std::remove_if(links.begin(),links.end(),[&](const auto &l) { return l.toNode==to && l.toInput==input; }),links.end());
    links.push_back({from,to,input,output}); const auto result=MaterialCompiler{}.Compile(candidate); m_diagnostics=result.diagnostics;
    if (!result.Success()) return false; m_draft=std::move(candidate); m_dirty=true; return true;
}
bool MaterialGraphEditor::DeleteNode(std::uint32_t id)
{
    auto &nodes=m_draft.graph.GetNodes(); const auto old=nodes.size(); nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[&](const auto &n) { return n.id==id; }),nodes.end());
    if (nodes.size()==old) return false;
    auto &links=m_draft.graph.GetLinks(); links.erase(std::remove_if(links.begin(),links.end(),[&](const auto &l) { return l.fromNode==id || l.toNode==id; }),links.end());
    m_dirty=true; if (m_selected==id) m_selected=0; if (m_linkFrom==id) m_linkFrom=0; return true;
}
void MaterialGraphEditor::DrawCanvas()
{
    ImGui::BeginChild("Node canvas",{0,360},true,ImGuiWindowFlags_HorizontalScrollbar);
    const auto origin=ImGui::GetCursorScreenPos(); auto *draw=ImGui::GetWindowDrawList();
    std::array<std::uint32_t,3> pending{};
    const auto node=[&](auto id) { const auto &nodes=m_draft.graph.GetNodes(); return std::find_if(nodes.begin(),nodes.end(),[&](const auto &n) { return n.id==id; }); };
    for (const auto &link:m_draft.graph.GetLinks())
    {
        auto from=node(link.fromNode),to=node(link.toNode); if (from==m_draft.graph.GetNodes().end() || to==m_draft.graph.GetNodes().end()) continue;
        ImVec2 a{origin.x+from->position.x+200,origin.y+from->position.y+48},b{origin.x+to->position.x,origin.y+to->position.y+48+link.toInput*22};
        draw->AddBezierCubic(a,{a.x+60,a.y},{b.x-60,b.y},b,IM_COL32(100,180,255,255),2);
    }
    for (auto &n:m_draft.graph.GetNodes())
    {
        const auto ports=Inputs(n.type); ImVec2 pos{origin.x+n.position.x,origin.y+n.position.y};
        draw->AddRectFilled(pos,{pos.x+200,pos.y+70+ports.size()*22},IM_COL32(38,48,65,255),5);
        ImGui::PushID(static_cast<int>(n.id)); ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("Header",{200,32});
        if (ImGui::IsItemClicked()) m_selected=n.id;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) { n.position.x+=ImGui::GetIO().MouseDelta.x; n.position.y+=ImGui::GetIO().MouseDelta.y; m_dirty=true; }
        draw->AddText({pos.x+8,pos.y+8},IM_COL32_WHITE,n.name.c_str());
        if (n.type!=MaterialNodeType::SurfaceOutput)
        {
            ImGui::SetCursorScreenPos({pos.x+185,pos.y+40}); ImGui::InvisibleButton("Output 0",{16,16});
            if (ImGui::IsItemClicked()) m_linkFrom=n.id;
            draw->AddCircleFilled({pos.x+200,pos.y+48},5,m_linkFrom==n.id ? IM_COL32(255,200,50,255) : IM_COL32(100,180,255,255));
        }
        for (std::size_t port=0;port<ports.size();++port)
        {
            ImGui::PushID(static_cast<int>(port)); ImGui::SetCursorScreenPos({pos.x,pos.y+40+port*22}); ImGui::InvisibleButton("Input",{16,16});
            if (ImGui::IsItemClicked() && m_linkFrom) { pending={m_linkFrom,n.id,static_cast<std::uint32_t>(port)}; m_linkFrom=0; }
            if (ImGui::IsItemClicked(1)) DisconnectPort(n.id,static_cast<std::uint32_t>(port));
            draw->AddCircleFilled({pos.x,pos.y+48+port*22},5,IM_COL32(120,220,150,255)); draw->AddText({pos.x+18,pos.y+40+port*22},IM_COL32_WHITE,ports[port]); ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(origin); ImGui::Dummy({1600,1400}); ImGui::EndChild();
    if (pending[0]) ConnectPorts(pending[0],0,pending[1],pending[2]);
}
void MaterialGraphEditor::DrawProperties()
{
    const auto selected=std::find_if(m_draft.graph.GetNodes().begin(),m_draft.graph.GetNodes().end(),[&](const auto &n) { return n.id==m_selected; });
    if (selected!=m_draft.graph.GetNodes().end())
    {
        auto &n=*selected; TextField("Name",n.name,m_dirty); TextField("Parameter name",n.parameterName,m_dirty);
        if (n.type==MaterialNodeType::ComponentMask) TextField("Components (xyzw)",n.channelMask,m_dirty);
        if (n.type==MaterialNodeType::Constant)
        {
            int type=static_cast<int>(n.constantType); if (ImGui::Combo("Constant type",&type,"Float\0Vec2\0Vec3\0Vec4\0")) { n.constantType=static_cast<MaterialParameterType>(type); m_dirty=true; }
            MaterialParameterValue value=n.constantType==MaterialParameterType::Float ? MaterialParameterValue(n.value.x) : n.constantType==MaterialParameterType::Vec2 ? MaterialParameterValue(Vec2{n.value.x,n.value.y}) : n.constantType==MaterialParameterType::Vec3 ? MaterialParameterValue(Vec3{n.value.x,n.value.y,n.value.z}) : MaterialParameterValue(n.value);
            if (DrawMaterialValue("Value",value)) { n.value=MaterialNumericSlot(value); m_dirty=true; }
        }
        if (ImGui::Button("Delete selected node")) DeleteNode(n.id);
    }
    if (ImGui::CollapsingHeader("Parameter declarations",ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::Button("Add parameter")) { MaterialParameterDecl p; p.name="Parameter"+std::to_string(m_draft.parameters.size()+1); m_draft.parameters.push_back(p); m_dirty=true; }
        std::size_t erase=m_draft.parameters.size();
        for (std::size_t i=0;i<m_draft.parameters.size();++i)
        {
            auto &p=m_draft.parameters[i]; ImGui::PushID(static_cast<int>(i)); TextField("Identifier",p.name,m_dirty); TextField("Display",p.displayName,m_dirty); TextField("Group",p.group,m_dirty);
            int type=static_cast<int>(p.type); if (ImGui::Combo("Type",&type,"Float\0Vec2\0Vec3\0Vec4\0Texture2D\0"))
            { p.type=static_cast<MaterialParameterType>(type); p.sampling=TextureSampling::Linear; p.defaultValue=type==0 ? MaterialParameterValue(0.f) : type==1 ? MaterialParameterValue(Vec2{}) : type==2 ? MaterialParameterValue(Vec3{}) : type==3 ? MaterialParameterValue(Vec4{}) : MaterialParameterValue(std::string{}); m_dirty=true; }
            if (DrawMaterialValue("Default",p.defaultValue,p.range)) m_dirty=true;
            if (p.type==MaterialParameterType::Texture2D) { int sampling=static_cast<int>(p.sampling); if (ImGui::Combo("Sampling",&sampling,"sRGB\0Linear\0Normal\0")) { p.sampling=static_cast<TextureSampling>(sampling); m_dirty=true; } }
            if (ImGui::Button("Remove parameter")) erase=i; ImGui::PopID();
        }
        if (erase<m_draft.parameters.size()) { m_draft.parameters.erase(m_draft.parameters.begin()+erase); m_dirty=true; }
    }
}
void MaterialGraphEditor::Draw()
{
    if (!m_visible) return;
    if (BeginDocument("Material graph / 材质节点图"))
    {
        if (ImGui::Button("Add node")) ImGui::OpenPopup("Nodes");
        if (ImGui::BeginPopup("Nodes"))
        {
            for (auto type:{MaterialNodeType::SurfaceOutput,MaterialNodeType::Constant,MaterialNodeType::ScalarParameter,MaterialNodeType::VectorParameter,MaterialNodeType::TextureParameter,MaterialNodeType::TextureSample,MaterialNodeType::TexCoord,MaterialNodeType::Add,MaterialNodeType::Multiply,MaterialNodeType::Lerp,MaterialNodeType::OneMinus,MaterialNodeType::Clamp,MaterialNodeType::ComponentMask,MaterialNodeType::NormalDecode})
                if (ImGui::MenuItem(MaterialNodeTypeName(type))) AddNode(type,MaterialNodeTypeName(type));
            ImGui::EndPopup();
        }
        int shading=static_cast<int>(m_draft.renderState.shadingModel),blend=static_cast<int>(m_draft.renderState.blendMode);
        if (ImGui::Combo("Shading",&shading,"Lit\0Unlit\0")) { m_draft.renderState.shadingModel=static_cast<MaterialShadingModel>(shading); m_dirty=true; }
        if (ImGui::Combo("Blend",&blend,"Opaque\0Masked\0Alpha\0")) { m_draft.renderState.blendMode=static_cast<MaterialBlendMode>(blend); m_dirty=true; }
        if (ImGui::Checkbox("Two sided",&m_draft.renderState.twoSided) || ImGui::SliderFloat("Mask cutoff",&m_draft.renderState.maskCutoff,0,1)) m_dirty=true;
        ImGui::TextUnformatted("Click an output, then an input to connect. Right click input to disconnect.");
        DrawCanvas(); DrawProperties(); m_draft.graph.GetParameters()=m_draft.parameters; m_draft.graph.GetRenderState()=m_draft.renderState;
    }
    EndDocument();
}
}
