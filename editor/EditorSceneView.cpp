#include "EditorApp.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#include "MiniEngine/Project/Project.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace MiniEngine
{
namespace
{
Vec3 CameraForward(const Vec3 &rotation)
{
    const float pitch = DegreesToRadians(rotation.x), yaw = DegreesToRadians(rotation.y);
    return Normalize(Vec3{std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)});
}

Vec3 TransformPoint(const Mat4 &matrix, const Vec3 &point)
{
    return {matrix.At(0, 0) * point.x + matrix.At(0, 1) * point.y + matrix.At(0, 2) * point.z + matrix.At(0, 3),
            matrix.At(1, 0) * point.x + matrix.At(1, 1) * point.y + matrix.At(1, 2) * point.z + matrix.At(1, 3),
            matrix.At(2, 0) * point.x + matrix.At(2, 1) * point.y + matrix.At(2, 2) * point.z + matrix.At(2, 3)};
}

bool ProjectToScreen(const FrameView &view, const Vec3 &point, float x, float y, float width, float height, ImVec2 &screen)
{
    const Mat4 matrix = view.projection * view.view;
    const float clipX = matrix.At(0, 0) * point.x + matrix.At(0, 1) * point.y + matrix.At(0, 2) * point.z + matrix.At(0, 3);
    const float clipY = matrix.At(1, 0) * point.x + matrix.At(1, 1) * point.y + matrix.At(1, 2) * point.z + matrix.At(1, 3);
    const float w = matrix.At(3, 0) * point.x + matrix.At(3, 1) * point.y + matrix.At(3, 2) * point.z + matrix.At(3, 3);
    if (w <= 0.0001f)
        return false;
    screen = {x + (clipX / w * 0.5f + 0.5f) * width, y + (0.5f - clipY / w * 0.5f) * height};
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

Aabb TransformBounds(const Aabb &bounds, const Mat4 &matrix)
{
    const Vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
    const Vec3 extent = (bounds.maximum - bounds.minimum) * 0.5f;
    const Vec3 worldCenter = TransformPoint(matrix, center);
    const Vec3 worldExtent{
        std::abs(matrix.At(0, 0)) * extent.x + std::abs(matrix.At(0, 1)) * extent.y + std::abs(matrix.At(0, 2)) * extent.z,
        std::abs(matrix.At(1, 0)) * extent.x + std::abs(matrix.At(1, 1)) * extent.y + std::abs(matrix.At(1, 2)) * extent.z,
        std::abs(matrix.At(2, 0)) * extent.x + std::abs(matrix.At(2, 1)) * extent.y + std::abs(matrix.At(2, 2)) * extent.z};
    return {worldCenter - worldExtent, worldCenter + worldExtent};
}

bool EntityBounds(const World &world, Entity entity, ResourceManager *resources, Aabb &bounds)
{
    const Mat4 matrix = world.GetWorldMatrix(entity);
    if (const auto *mesh = world.GetComponent<MeshComponent>(entity); mesh && resources && !mesh->meshPath.empty())
        if (const auto resource = resources->Load<MeshResource>(mesh->meshPath))
        {
            bounds = TransformBounds(resource->GetBounds(), matrix);
            return true;
        }
    if (const auto *collider = world.GetComponent<ColliderComponent>(entity))
    {
        const Vec3 extent = collider->shape == ColliderShape::Box ? collider->halfExtents
            : Vec3{collider->radius, collider->shape == ColliderShape::Capsule
                ? collider->height * 0.5f + collider->radius : collider->radius, collider->radius};
        bounds = TransformBounds({extent * -1.0f, extent}, matrix);
        return true;
    }
    return false;
}

bool IntersectRayAabb(const Ray &ray, const Aabb &bounds, float &distance)
{
    float nearDistance = -std::numeric_limits<float>::max(), farDistance = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis)
    {
        const float origin = axis == 0 ? ray.origin.x : axis == 1 ? ray.origin.y : ray.origin.z;
        const float direction = axis == 0 ? ray.direction.x : axis == 1 ? ray.direction.y : ray.direction.z;
        const float minimum = axis == 0 ? bounds.minimum.x : axis == 1 ? bounds.minimum.y : bounds.minimum.z;
        const float maximum = axis == 0 ? bounds.maximum.x : axis == 1 ? bounds.maximum.y : bounds.maximum.z;
        if (std::abs(direction) < 1e-6f)
        {
            if (origin < minimum || origin > maximum)
                return false;
            continue;
        }
        float a = (minimum - origin) / direction, b = (maximum - origin) / direction;
        if (a > b)
            std::swap(a, b);
        nearDistance = std::max(nearDistance, a);
        farDistance = std::min(farDistance, b);
        if (nearDistance > farDistance)
            return false;
    }
    // A camera inside a large collider still places on its forward surface.
    distance = nearDistance > 0.0001f ? nearDistance : farDistance;
    return distance > 0.0001f && std::isfinite(distance);
}

void DrawWorldLine(ImDrawList *draw, const FrameView &view, Vec3 a, Vec3 b,
                   float x, float y, float width, float height, ImU32 color, float thickness = 1.0f)
{
    const auto depth = [&view](const Vec3 &point) {
        return -(view.view.At(2, 0) * point.x + view.view.At(2, 1) * point.y + view.view.At(2, 2) * point.z + view.view.At(2, 3));
    };
    const float da = depth(a), db = depth(b), nearPlane = 0.05f;
    if (da < nearPlane && db < nearPlane)
        return;
    if (da < nearPlane)
        a = a + (b - a) * ((nearPlane - da) / (db - da));
    else if (db < nearPlane)
        b = a + (b - a) * ((nearPlane - da) / (db - da));
    ImVec2 start, end;
    if (ProjectToScreen(view, a, x, y, width, height, start) && ProjectToScreen(view, b, x, y, width, height, end))
        draw->AddLine(start, end, color, thickness);
}

void DrawBounds(ImDrawList *draw, const FrameView &view, const Aabb &bounds,
                float x, float y, float width, float height, ImU32 color, float thickness = 1.5f)
{
    std::array<Vec3, 8> corners{};
    for (int corner = 0; corner < 8; ++corner)
        corners[corner] = {corner & 1 ? bounds.maximum.x : bounds.minimum.x,
                           corner & 2 ? bounds.maximum.y : bounds.minimum.y,
                           corner & 4 ? bounds.maximum.z : bounds.minimum.z};
    for (int corner = 0; corner < 8; ++corner)
        for (int axis = 0; axis < 3; ++axis)
            if (!(corner & (1 << axis)))
                DrawWorldLine(draw, view, corners[corner], corners[corner | (1 << axis)], x, y, width, height, color, thickness);
}

// Clip before perspective division: distant grid segments must not emit enormous UI geometry.
bool ClipGridLine(const Mat4 &matrix, Vec3 &a, Vec3 &b)
{
    const auto clip=[&](Vec3 p) { return std::array<float,4>{
        matrix.At(0,0)*p.x+matrix.At(0,1)*p.y+matrix.At(0,2)*p.z+matrix.At(0,3),
        matrix.At(1,0)*p.x+matrix.At(1,1)*p.y+matrix.At(1,2)*p.z+matrix.At(1,3),
        matrix.At(2,0)*p.x+matrix.At(2,1)*p.y+matrix.At(2,2)*p.z+matrix.At(2,3),
        matrix.At(3,0)*p.x+matrix.At(3,1)*p.y+matrix.At(3,2)*p.z+matrix.At(3,3)}; };
    const auto start=clip(a),end=clip(b); float first=0,last=1;
    for(int axis=0;axis<3;++axis) for(float sign : {-1.0f,1.0f})
    {
        const float da=start[3]+sign*start[axis],db=end[3]+sign*end[axis];
        if(da<0 && db<0) return false;
        if(da<0) first=std::max(first,da/(da-db));
        else if(db<0) last=std::min(last,da/(da-db));
        if(first>last) return false;
    }
    const Vec3 delta=b-a; b=a+delta*last; a+=delta*first;
    return true;
}

void DrawGroundGrid(ImDrawList *draw, const FrameView &view, float x, float y, float width, float height)
{
    const Mat4 matrix=view.projection*view.view;
    const auto smooth=[](float t) { t=std::clamp(t,0.0f,1.0f); return t*t*(3-2*t); };
    const auto line=[&](Vec3 a,Vec3 b,ImU32 color) {
        if(ClipGridLine(matrix,a,b)) DrawWorldLine(draw,view,a,b,x,y,width,height,color);
    };
    // Every level stays anchored to the world origin. Camera motion only changes opacity,
    // never the unit size or the footprint of an existing level. Larger levels fill the horizon.
    const float pixelsPerUnit=height*std::abs(view.projection.At(1,1))*.5f/std::max(.05f,std::abs(view.cameraPosition.y));
    for(float step : {.01f,.1f,1.0f,10.0f,100.0f,1000.0f,10000.0f})
    {
        if(pixelsPerUnit*step<=3) continue;
        const float radius=step*80,segmentLength=step*8;
        const int baseAlpha=step<1 ? 35 : step==1 ? 60 : 80;
        const auto opacity=[&](Vec3 p,int axis,ImVec2 &screen) {
            const float cx=matrix.At(0,0)*p.x+matrix.At(0,2)*p.z+matrix.At(0,3);
            const float cy=matrix.At(1,0)*p.x+matrix.At(1,2)*p.z+matrix.At(1,3);
            const float cw=matrix.At(3,0)*p.x+matrix.At(3,2)*p.z+matrix.At(3,3);
            if(cw<=.0001f) return 0.0f;
            screen={x+(cx/cw*.5f+.5f)*width,y+(.5f-cy/cw*.5f)*height};
            const auto derivative=[&](int column) { return ImVec2{
                width*.5f*(matrix.At(0,column)*cw-cx*matrix.At(3,column))/(cw*cw),
                -height*.5f*(matrix.At(1,column)*cw-cy*matrix.At(3,column))/(cw*cw)}; };
            const auto dx=derivative(0),dz=derivative(2),tangent=axis==0 ? dz : dx;
            // Screen-space separation perpendicular to the line, including foreshortening.
            const float spacing=step*std::abs(dx.x*dz.y-dx.y*dz.x)/std::max(.0001f,std::hypot(tangent.x,tangent.y));
            const float radialX=p.x-view.cameraPosition.x,radialZ=p.z-view.cameraPosition.z;
            return smooth((spacing-3)/15)*(1-smooth((std::hypot(radialX,radialZ)/radius-.35f)/.55f));
        };
        for(int axis=0;axis<2;++axis)
        {
            const float across=axis==0 ? view.cameraPosition.x : view.cameraPosition.z;
            const float along=axis==0 ? view.cameraPosition.z : view.cameraPosition.x;
            const double center=std::floor(static_cast<double>(across)/step);
            for(int offset=-80;offset<=80;++offset)
            {
                const double index=center+offset;
                // Each line belongs to exactly one level; origin axes are drawn separately.
                if(std::abs(std::remainder(index,10.0))<.01) continue;
                const float coordinate=static_cast<float>(index*step);
                if(std::abs(coordinate-across)>=radius*.9f) continue;
                Vec3 start=axis==0 ? Vec3{coordinate,0,along-radius} : Vec3{along-radius,0,coordinate};
                Vec3 end=axis==0 ? Vec3{coordinate,0,along+radius} : Vec3{along+radius,0,coordinate};
                // Clip each whole line once, then tessellate only its visible, world-anchored span.
                if(!ClipGridLine(matrix,start,end)) continue;
                float minimum=axis==0 ? start.z : start.x,maximum=axis==0 ? end.z : end.x;
                if(minimum>maximum) std::swap(minimum,maximum);
                const double firstSegment=std::floor(static_cast<double>(minimum)/segmentLength);
                const int segmentCount=static_cast<int>(std::ceil((maximum-minimum)/segmentLength))+1;
                for(int segment=0;segment<segmentCount;++segment)
                {
                    const float segmentStart=static_cast<float>((firstSegment+segment)*segmentLength);
                    const float from=std::max(minimum,segmentStart),to=std::min(maximum,segmentStart+segmentLength);
                    if(to<=from) continue;
                    Vec3 a=axis==0 ? Vec3{coordinate,0,from} : Vec3{from,0,coordinate};
                    Vec3 b=axis==0 ? Vec3{coordinate,0,to} : Vec3{to,0,coordinate};
                    ImVec2 screenA{},screenB{};
                    const int alpha=static_cast<int>(baseAlpha*(opacity(a,axis,screenA)+opacity(b,axis,screenB))*.5f);
                    if(alpha>0) draw->AddLine(screenA,screenB,IM_COL32(165,174,184,alpha));
                }
            }
        }
    }
    // Use the same plane/projection as the grid, with no snapped camera-relative origin.
    line({0,0,view.cameraPosition.z-10000},{0,0,view.cameraPosition.z+10000},IM_COL32(90,130,235,160));
    line({view.cameraPosition.x-10000,0,0},{view.cameraPosition.x+10000,0,0},IM_COL32(230,95,90,160));
}

Vec3 RootsAnchor(const World &world, const std::vector<Entity> &roots)
{
    Vec3 anchor{};
    for (Entity root : roots)
    {
        const Mat4 matrix = world.GetWorldMatrix(root);
        anchor += Vec3{matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)};
    }
    return roots.empty() ? anchor : anchor / static_cast<float>(roots.size());
}

// Only this temporary World is deserialized for the ghost; the editor World is never changed by a preview.
bool AssetPreviewBounds(const AssetInfo &asset, ResourceManager *resources, Aabb &bounds)
{
    if (asset.type == "Mesh" && resources)
    {
        if (const auto mesh = resources->Load<MeshResource>(asset.path))
        {
            bounds = mesh->GetBounds();
            return true;
        }
    }
    else if (asset.type == "Prefab")
    {
        World preview;
        if (!SceneSerializer::Load(preview, std::filesystem::u8path(asset.path)))
            return false;
        std::vector<Entity> roots;
        for (const Entity entity : preview.GetEntityManager().GetAliveEntities())
        {
            const auto *relationship = preview.GetComponent<RelationshipComponent>(entity);
            if (!relationship || !preview.IsAlive(relationship->parent))
                roots.push_back(entity);
        }
        const Vec3 anchor = RootsAnchor(preview, roots);
        bool any = false;
        for (const Entity entity : preview.GetEntityManager().GetAliveEntities())
        {
            if (const auto *active = preview.GetComponent<ActiveComponent>(entity); active && !active->active)
                continue;
            Aabb part;
            if (!EntityBounds(preview, entity, resources, part))
            {
                const Mat4 matrix = preview.GetWorldMatrix(entity);
                const Vec3 position{matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)};
                part = {position - Vec3{0.15f, 0.15f, 0.15f}, position + Vec3{0.15f, 0.15f, 0.15f}};
            }
            part.minimum -= anchor;
            part.maximum -= anchor;
            if (!any)
                bounds = part;
            else
            {
                bounds.minimum = {std::min(bounds.minimum.x, part.minimum.x), std::min(bounds.minimum.y, part.minimum.y),
                                  std::min(bounds.minimum.z, part.minimum.z)};
                bounds.maximum = {std::max(bounds.maximum.x, part.maximum.x), std::max(bounds.maximum.y, part.maximum.y),
                                  std::max(bounds.maximum.z, part.maximum.z)};
            }
            any = true;
        }
        return any;
    }
    return false;
}

bool OrientationHovered(float x, float y, float width, float height)
{
    return width >= 150.0f && height >= 150.0f &&
        ImGui::IsMouseHoveringRect({x + width - 148.0f, y + 8.0f}, {x + width - 8.0f, y + 148.0f});
}
}

void EditorApp::DrawSceneView()
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const auto layout = GetWorkspaceLayout(viewport->WorkSize.x, viewport->WorkSize.y);
    const float left = viewport->WorkPos.x + (m_sceneMaximized ? 0.0f : layout.left + 3.0f), top = viewport->WorkPos.y;
    const float width = m_sceneMaximized ? viewport->WorkSize.x : viewport->WorkSize.x - layout.left - layout.right - 6.0f;
    const float height = m_sceneMaximized ? viewport->WorkSize.y : layout.upper - 3.0f;
    bool visible = true;
    if (!BeginEditorPanel("Scene View", "场景视图", visible, {left, top}, {width, height}, m_sceneMaximized))
    {
        m_viewCubeActive = m_sceneOrbitActive = m_scenePanActive = m_sceneAltOrbitActive = m_marqueeActive = false;
        m_activeGizmoAxis = -1;
        m_gizmoUndoRecorded = false;
        ImGui::End();
        return;
    }
    m_sceneCommandsFocused |= ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::BeginDisabled(m_engine.IsPlaying() || m_packageFuture.valid());
    DrawSceneTabs();
    ImGui::EndDisabled();
    const char *pivotNames[] = {Localize("Center", "中心"), Localize("Active", "活动项"), Localize("Individual", "各自")};
    const float toolbarWidth = ImGui::GetContentRegionAvail().x;
    const bool compactToolbar = toolbarWidth < 520.0f;
    const auto sameLineIfFits = [](float nextWidth) {
        const float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth <= rightEdge)
            ImGui::SameLine();
    };
    const char *modeNames[]={Localize("Lit", "光照"), Localize("Unlit", "无光照"), Localize("Wireframe", "线框")};
    if (ImGui::Button((std::string(modeNames[static_cast<int>(m_viewportRenderMode)])+" v###SceneViewMode").c_str()))
        ImGui::OpenPopup("SceneViewOptions");
    if (ImGui::BeginPopup("SceneViewOptions"))
    {
        for (int mode=0;mode<3;++mode)
            if (ImGui::MenuItem(modeNames[mode],nullptr,static_cast<int>(m_viewportRenderMode)==mode))
                m_viewportRenderMode=static_cast<ViewportRenderMode>(mode);
        ImGui::EndPopup();
    }
    sameLineIfFits(75);
    if (ImGui::Button(Localize("Show v", "显示 v"))) ImGui::OpenPopup("SceneShowFlags");
    if (ImGui::BeginPopup("SceneShowFlags"))
    {
        if (ImGui::MenuItem(Localize("Ground grid", "地面网格"),nullptr,&m_showSceneGrid)) SaveEditorSettings();
        if (ImGui::MenuItem(Localize("Selected bounds", "选中包围盒"),nullptr,&m_showSelectionBounds)) SaveEditorSettings();
        ImGui::EndPopup();
    }
    const char *focusLabel = m_sceneMaximized ? (compactToolbar ? Localize("Restore", "还原") : Localize("Restore", "还原布局")) :
        compactToolbar ? Localize("Maximize", "放大") : Localize("Focus View", "放大视图");
    sameLineIfFits(ImGui::CalcTextSize(focusLabel).x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button(focusLabel))
        m_sceneMaximized = !m_sceneMaximized;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("F11");
    // Frequently used transform controls remain visible, wrapping onto additional rows on narrow panels.
    ImGui::Checkbox(Localize("Snap", "吸附"), &m_gizmoSnap);
    sameLineIfFits(ImGui::CalcTextSize(Localize("Snap Settings...", "吸附设置...")).x + ImGui::GetStyle().FramePadding.x*2);
    if (ImGui::Button(Localize("Snap Settings...", "吸附设置..."))) ImGui::OpenPopup("GizmoSnapSettings");
    if (ImGui::BeginPopup("GizmoSnapSettings"))
    {
        ImGui::SetNextItemWidth(140);
        ImGui::DragFloat(Localize("Move step", "平移步长"), &m_translateSnap, .05f, .001f, 100.0f, "%.3g", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(140);
        ImGui::DragFloat(Localize("Rotation step", "旋转步长"), &m_rotateSnap, .5f, .1f, 180.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SetNextItemWidth(140);
        ImGui::DragFloat(Localize("Scale step", "缩放步长"), &m_scaleSnap, .01f, .001f, 10.0f, "%.3g", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndPopup();
    }
    const char *spaceLabel = m_gizmoSpace == GizmoSpace::World ? Localize("World", "世界轴") : Localize("Local", "局部轴");
    sameLineIfFits(ImGui::CalcTextSize(spaceLabel).x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button(spaceLabel))
        m_gizmoSpace = m_gizmoSpace == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Localize("Move and rotation axes. Scale always uses local axes.", "平移和旋转的坐标轴；缩放始终使用局部轴。"));
    sameLineIfFits(120.0f);
    int pivot = static_cast<int>(m_pivotMode);
    ImGui::SetNextItemWidth(std::min(120.0f, toolbarWidth));
    if (ImGui::Combo("##ScenePivot", &pivot, pivotNames, 3))
        m_pivotMode = static_cast<PivotMode>(pivot);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Localize("Transform pivot: center, active entity, or each entity", "变换轴心：中心、活动实体或各自轴心"));
    sameLineIfFits(170.0f);
    ImGui::SetNextItemWidth(std::min(95.0f, std::max(45.0f, toolbarWidth - 65.0f)));
    ImGui::DragFloat(Localize("Speed", "速度"), &m_cameraSpeed, 0.05f, 0.05f, 20.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Localize("Camera movement and dolly speed. Shift moves faster.", "相机移动和推进速度；按 Shift 加速。"));
    ImGui::TextWrapped("%s", Localize("WASD/QE move | RMB look | Alt+LMB orbit | MMB pan | F frame",
                                      "WASD/QE 移动 | 右键转向 | Alt+左键环绕 | 中键平移 | F 聚焦"));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", Localize("Arrows move, rings rotate, cubes scale. Click the view to move the camera; Shift moves faster. F11 focuses the viewport.",
                                        "箭头平移、圆环旋转、方块缩放。点击视图后可移动相机，Shift 加速；F11 放大或还原视图。"));
    const ImVec2 content = ImGui::GetContentRegionAvail();
    const ImVec2 available{std::max(1.0f, content.x), std::max(1.0f, content.y)};
    // ImGui 可用区域是逻辑像素；场景渲染目标按 DPI 放大到物理像素，高分屏上视口保持清晰。
    const int targetWidth = std::max(1, static_cast<int>(available.x * m_dpiScale)),
              targetHeight = std::max(1, static_cast<int>(available.y * m_dpiScale));
    m_device->ResizeSceneRenderTarget(targetWidth, targetHeight);
    const ImVec2 imageMinimum = ImGui::GetCursorScreenPos();
    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(m_device->GetSceneColorTexture())), available, {0, 1}, {1, 0});
    const bool hovered = ImGui::IsItemHovered(m_viewCubeActive ? ImGuiHoveredFlags_AllowWhenBlockedByActiveItem : ImGuiHoveredFlags_None);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        ImGui::SetWindowFocus();
    const auto &io = ImGui::GetIO();
    const bool sceneInputAllowed = !m_engine.IsPlaying() && !m_packageFuture.valid() && !io.WantTextInput && !io.AppFocusLost &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool orientationHovered = m_viewCubeActive || (hovered && OrientationHovered(imageMinimum.x, imageMinimum.y, available.x, available.y));
    UpdateSceneCamera(hovered && !orientationHovered, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
    const bool navigating = m_viewCubeActive || m_sceneOrbitActive || m_scenePanActive || m_sceneAltOrbitActive ||
        (io.KeyAlt && m_activeGizmoAxis < 0);
    const ImGuiPayload *dragPayload = ImGui::GetDragDropPayload();
    const bool draggingAsset = dragPayload && dragPayload->IsDataType("TINY_ASSET_PATH");
    if (!draggingAsset)
    {
        m_scenePreviewAsset.clear();
        m_scenePreviewBoundsValid = false;
    }
    // Keep this target adjacent to Image: overlays never replace the image's drag/drop hit region.
    if (sceneInputAllowed && !navigating && ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("TINY_ASSET_PATH",
                ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect))
        {
            std::string path(static_cast<const char *>(payload->Data), static_cast<std::size_t>(payload->DataSize));
            if (!path.empty() && path.back() == '\0')
                path.pop_back();
            const auto asset = m_engine.GetContext().resourceManager->GetAssetRegistry().Find(path);
            if (asset)
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const float normalizedX = (mouse.x - imageMinimum.x) / available.x;
                const float normalizedY = (mouse.y - imageMinimum.y) / available.y;
                World &world = *m_engine.GetContext().world;
                ImDrawList *draw = ImGui::GetWindowDrawList();
                const FrameView &view = m_engine.GetContext().renderer->GetFrameView();
                draw->PushClipRect(imageMinimum, {imageMinimum.x + available.x, imageMinimum.y + available.y}, true);
                if (asset->type == "Material" || asset->type == "MaterialGraph")
                {
                    const Entity target = PickEntity(normalizedX, normalizedY);
                    auto *mesh = world.IsAlive(target) ? world.GetComponent<MeshComponent>(target) : nullptr;
                    Aabb bounds;
                    if (mesh && EntityBounds(world, target, m_engine.GetContext().resourceManager, bounds))
                        DrawBounds(draw, view, bounds, imageMinimum.x, imageMinimum.y, available.x, available.y,
                                   IM_COL32(90, 235, 190, 255), 2.5f);
                    draw->PopClipRect();
                    if (mesh)
                    {
                        const auto *name = world.GetComponent<NameComponent>(target);
                        ImGui::SetTooltip("%s: %s\n%s", Localize("Apply material to", "应用材质到"),
                            name ? name->name.c_str() : Localize("Mesh", "网格"), path.c_str());
                        if (payload->IsDelivery())
                        {
                            if (ApplyMaterialToMeshes(path, {target}))
                            {
                                m_selected = target;
                                m_selection = {target};
                            }
                        }
                    }
                    else
                        ImGui::SetTooltip("%s", Localize("Point at a mesh to apply this material", "将指针移到网格上以应用材质"));
                }
                else if (asset->type == "Mesh" || asset->type == "Prefab")
                {
                    Vec3 placement;
                    const bool valid = GetScenePlacement(normalizedX, normalizedY, placement);
                    if (m_scenePreviewAsset != path)
                    {
                        m_scenePreviewAsset = path;
                        m_scenePreviewBoundsValid = AssetPreviewBounds(*asset, m_engine.GetContext().resourceManager, m_scenePreviewBounds);
                    }
                    if (valid)
                    {
                        if (m_scenePreviewBoundsValid)
                            DrawBounds(draw, view, {m_scenePreviewBounds.minimum + placement, m_scenePreviewBounds.maximum + placement},
                                imageMinimum.x, imageMinimum.y, available.x, available.y, IM_COL32(100, 235, 195, 220), 2.0f);
                        ImVec2 marker;
                        if (ProjectToScreen(view, placement, imageMinimum.x, imageMinimum.y, available.x, available.y, marker))
                        {
                            draw->AddCircle(marker, 8.0f, IM_COL32(120, 255, 205, 255), 24, 2.0f);
                            draw->AddLine({marker.x - 13, marker.y}, {marker.x + 13, marker.y}, IM_COL32(120, 255, 205, 255));
                            draw->AddLine({marker.x, marker.y - 13}, {marker.x, marker.y + 13}, IM_COL32(120, 255, 205, 255));
                        }
                    }
                    draw->PopClipRect();
                    if (valid)
                    {
                        ImGui::SetTooltip("%s\n%s: %.2f, %.2f, %.2f\n%s", Localize("Release to place", "松开放置"),
                            Localize("Position", "位置"), placement.x, placement.y, placement.z,
                            Localize("Preview: bounds on surface / ground", "预览：表面或地面上的包围盒"));
                        if (payload->IsDelivery())
                            PlaceAssetInScene(*asset, &placement);
                    }
                    else
                        ImGui::SetTooltip("%s", Localize("No surface here. Point toward an object or the ground.", "此处没有可用表面，请指向对象或地面。"));
                }
                else
                {
                    draw->PopClipRect();
                    ImGui::SetTooltip("%s", Localize("This asset cannot be placed in the scene", "此资产不能直接放入场景"));
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    DrawSceneOverlays(imageMinimum.x, imageMinimum.y, available.x, available.y, hovered && !draggingAsset);
    m_engine.GetContext().renderer->SetViewportRenderMode(m_viewportRenderMode);
    m_engine.GetContext().renderer->SetCameraOverride(!m_engine.IsPlaying(), m_editorCameraPosition, m_editorCameraRotation,
                                                      m_editorCameraFov);
    if (!sceneInputAllowed || draggingAsset)
        m_activeGizmoAxis = -1;
    const bool gizmoConsumed = sceneInputAllowed && !navigating && !draggingAsset &&
        DrawGizmo(imageMinimum.x, imageMinimum.y, available.x, available.y, hovered && !orientationHovered);
    if (sceneInputAllowed && !navigating && !draggingAsset && !orientationHovered && hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !gizmoConsumed)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        m_selectedAsset.clear();
        m_marqueeActive = true;
        m_marqueeStart = {mouse.x, mouse.y};
        const float normalizedX = (mouse.x - imageMinimum.x) / std::max(1.0f, available.x);
        const float normalizedY = (mouse.y - imageMinimum.y) / std::max(1.0f, available.y);
        const bool ctrl = ImGui::GetIO().KeyCtrl != 0;
        // 同一像素位置的连续点击在多个重叠命中间循环；移动到新位置或框选后重新计算命中集合。
        const auto hits = PickEntityHits(normalizedX, normalizedY);
        const bool sameSpot = m_lastPickValid && hits == m_lastPickHits && std::abs(mouse.x - m_lastPickScreen.x) < 3.0f &&
                              std::abs(mouse.y - m_lastPickScreen.y) < 3.0f;
        Entity picked = InvalidEntity;
        if (sameSpot && !m_lastPickHits.empty() && !ctrl)
        {
            m_lastPickIndex = (m_lastPickIndex + 1) % m_lastPickHits.size();
            picked = m_lastPickHits[m_lastPickIndex];
        }
        else
        {
            m_lastPickHits = hits;
            m_lastPickScreen = {mouse.x, mouse.y};
            m_lastPickValid = !m_lastPickHits.empty();
            m_lastPickIndex = 0;
            picked = m_lastPickHits.empty() ? InvalidEntity : m_lastPickHits.front();
        }
        if (ctrl && picked != InvalidEntity)
        {
            const auto selected = std::find(m_selection.begin(), m_selection.end(), picked);
            if (selected == m_selection.end())
                m_selection.push_back(picked);
            else
                m_selection.erase(selected);
            m_selected = m_selection.empty() ? InvalidEntity : m_selection.back();
        }
        else
        {
            m_selected = picked;
            m_selection = picked == InvalidEntity ? std::vector<Entity>{} : std::vector<Entity>{picked};
        }
    }
    if (!sceneInputAllowed || navigating || draggingAsset)
        m_marqueeActive = false;
    if (m_marqueeActive)
    {
        const ImVec2 mouse = ImGui::GetMousePos();
        const ImVec2 start{m_marqueeStart.x, m_marqueeStart.y};
        const ImVec2 minimum{std::min(start.x, mouse.x), std::min(start.y, mouse.y)};
        const ImVec2 maximum{std::max(start.x, mouse.x), std::max(start.y, mouse.y)};
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (maximum.x - minimum.x > 3.0f || maximum.y - minimum.y > 3.0f))
        {
            ImGui::GetWindowDrawList()->AddRectFilled(minimum, maximum, IM_COL32(60, 135, 240, 35));
            ImGui::GetWindowDrawList()->AddRect(minimum, maximum, IM_COL32(90, 165, 255, 220));
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (maximum.x - minimum.x > 5.0f || maximum.y - minimum.y > 5.0f)
            {
                std::vector<Entity> marquee;
                World &world = *m_engine.GetContext().world;
                for (const Entity entity : world.GetEntityManager().GetAliveEntities())
                {
                    const Mat4 matrix = world.GetWorldMatrix(entity);
                    ImVec2 screen;
                    if (ProjectToScreen(m_engine.GetContext().renderer->GetFrameView(), {matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)},
                                        imageMinimum.x, imageMinimum.y, available.x, available.y, screen) &&
                        screen.x >= minimum.x && screen.x <= maximum.x && screen.y >= minimum.y && screen.y <= maximum.y)
                        marquee.push_back(entity);
                }
                if (ImGui::GetIO().KeyCtrl)
                {
                    for (const Entity entity : marquee)
                        if (std::find(m_selection.begin(), m_selection.end(), entity) == m_selection.end())
                            m_selection.push_back(entity);
                }
                else
                    m_selection = std::move(marquee);
                m_selected = m_selection.empty() ? InvalidEntity : m_selection.back();
                // 框选产生的新选择集使下一次点击应重新计算命中，不复用旧的循环队列。
                m_lastPickValid = false;
            }
            m_marqueeActive = false;
        }
    }
    ImGui::End();
}

void EditorApp::UpdateSceneCamera(const bool hovered, const bool focused)
{
    auto &io = ImGui::GetIO();
    const bool blocked = m_viewCubeActive || m_engine.IsPlaying() || m_packageFuture.valid() || io.WantTextInput || ImGui::IsAnyItemActive() ||
                         io.KeyCtrl || io.KeySuper || m_activeGizmoAxis >= 0 || m_marqueeActive || io.AppFocusLost ||
                         ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (blocked)
    {
        m_sceneOrbitActive = false;
        m_scenePanActive = false;
        m_sceneAltOrbitActive = false;
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        m_sceneOrbitActive = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        m_scenePanActive = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !io.KeyAlt)
        m_sceneAltOrbitActive = false;
    const bool captured = m_sceneOrbitActive || m_scenePanActive || m_sceneAltOrbitActive;
    const bool beganPan = hovered && !captured && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    const bool beganOrbit = hovered && !captured && !beganPan && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool beganLook = hovered && !captured && !beganPan && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    if (beganPan || beganOrbit)
    {
        if (const World *world = m_engine.GetContext().world; world && world->IsAlive(m_selected))
        {
            const Mat4 matrix = world->GetWorldMatrix(m_selected);
            Aabb bounds;
            m_sceneOrbitPivot = EntityBounds(*world, m_selected, m_engine.GetContext().resourceManager, bounds)
                ? (bounds.minimum + bounds.maximum) * 0.5f : Vec3{matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)};
        }
        else if (Dot(m_sceneOrbitPivot - m_editorCameraPosition, CameraForward(m_editorCameraRotation)) <= 0.001f)
            m_sceneOrbitPivot = m_editorCameraPosition + CameraForward(m_editorCameraRotation) *
                std::max(5.0f, Length(m_sceneOrbitPivot - m_editorCameraPosition));
        m_scenePanActive = beganPan;
        m_sceneAltOrbitActive = beganOrbit;
    }
    if (beganLook)
        m_sceneOrbitActive = true;
    if (m_sceneOrbitActive && !beganLook)
    {
        // ImGui 指针坐标已是逻辑像素；捕获鼠标时忽略进入视图的首帧位移。
        m_editorCameraRotation.y += io.MouseDelta.x * 0.18f;
        m_editorCameraRotation.x = std::clamp(m_editorCameraRotation.x - io.MouseDelta.y * 0.18f, -89.0f, 89.0f);
    }
    if (m_sceneAltOrbitActive && !beganOrbit)
    {
        // Rotate the camera-to-pivot offset rather than snapping an off-center selection to the view direction.
        const Vec3 offset = m_editorCameraPosition - m_sceneOrbitPivot;
        const float distance = std::max(0.05f, Length(offset));
        const Vec3 aim = Normalize(m_sceneOrbitPivot - m_editorCameraPosition);
        const float previousPitch = std::asin(std::clamp(aim.y, -1.0f, 1.0f)) * 180.0f / Pi;
        const float nextPitch = std::clamp(previousPitch - io.MouseDelta.y * 0.18f, -89.0f, 89.0f);
        const float nextYaw = std::atan2(aim.x, -aim.z) * 180.0f / Pi + io.MouseDelta.x * 0.18f;
        // Preserve the angular offset to an off-center pivot; beginning an orbit never reframes the view.
        m_editorCameraRotation.x = std::clamp(m_editorCameraRotation.x + nextPitch - previousPitch, -89.0f, 89.0f);
        m_editorCameraRotation.y += io.MouseDelta.x * 0.18f;
        m_editorCameraPosition = m_sceneOrbitPivot - CameraForward({nextPitch, nextYaw, 0}) * distance;
    }
    const Vec3 forward = CameraForward(m_editorCameraRotation), right = Normalize(Cross(forward, {0, 1, 0}));
    const Vec3 up = Normalize(Cross(right, forward));
    Vec3 cameraFocus = m_sceneOrbitPivot;
    if (const World *w = m_engine.GetContext().world; w && w->IsAlive(m_selected))
    {
        const Mat4 matrix = w->GetWorldMatrix(m_selected);
        cameraFocus = {matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)};
    }
    const float focusDistance = std::max(0.5f, Length(cameraFocus - m_editorCameraPosition));
    if (m_scenePanActive && !beganPan)
    {
        float viewportHeight = 720.0f;
        if (m_device)
            viewportHeight = static_cast<float>(m_device->GetSceneTargetHeight()) / std::max(0.01f, m_dpiScale);
        else if (io.DisplaySize.y > 0)
            viewportHeight = io.DisplaySize.y;
        const float worldPerPixel = 2.0f * focusDistance * std::tan(DegreesToRadians(m_editorCameraFov) * 0.5f) /
                                    std::max(1.0f, viewportHeight);
        const Vec3 delta = (right * -io.MouseDelta.x + up * io.MouseDelta.y) * worldPerPixel;
        m_editorCameraPosition += delta;
        m_sceneOrbitPivot += delta;
    }
    const float speed = std::clamp(m_cameraSpeed, 0.05f, 20.0f);
    if ((focused || m_sceneOrbitActive) && !m_scenePanActive && !m_sceneAltOrbitActive && !io.KeyAlt)
    {
        Vec3 movement{};
        if (ImGui::IsKeyDown(ImGuiKey_W)) movement += forward;
        if (ImGui::IsKeyDown(ImGuiKey_S)) movement -= forward;
        if (ImGui::IsKeyDown(ImGuiKey_D)) movement += right;
        if (ImGui::IsKeyDown(ImGuiKey_A)) movement -= right;
        if (ImGui::IsKeyDown(ImGuiKey_E)) movement.y += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) movement.y -= 1.0f;
        if (Length(movement) > 0.0f)
        {
            const Vec3 delta = Normalize(movement) * (std::max(0.5f, focusDistance * 0.6f) * speed *
                (io.KeyShift ? 3.0f : 1.0f) * std::min(io.DeltaTime, 0.05f));
            m_editorCameraPosition += delta;
            m_sceneOrbitPivot += delta;
        }
    }
    if (hovered && !m_scenePanActive && !m_sceneAltOrbitActive && !io.KeyAlt && std::abs(io.MouseWheel) > 0.0f)
    {
        const float requested = io.MouseWheel * std::max(0.05f, focusDistance * 0.12f) * speed;
        m_editorCameraPosition += forward * std::min(requested, std::max(0.05f, focusDistance - 0.1f));
    }
}

bool EditorApp::GetScenePlacement(const float normalizedX, const float normalizedY, Vec3 &position) const
{
    const auto &context = m_engine.GetContext();
    if (!context.world || !std::isfinite(normalizedX) || !std::isfinite(normalizedY) ||
        normalizedX < 0.0f || normalizedX > 1.0f || normalizedY < 0.0f || normalizedY > 1.0f)
        return false;
    float aspect = 16.0f / 9.0f;
    if (m_device)
        aspect = m_device->GetAspectRatio();
    else if (context.renderer)
    {
        const auto &projection = context.renderer->GetFrameView().projection;
        if (std::abs(projection.At(0, 0)) > 1e-6f)
            aspect = std::abs(projection.At(1, 1) / projection.At(0, 0));
    }
    if (!std::isfinite(aspect) || aspect < 0.01f)
        aspect = 16.0f / 9.0f;
    const Vec3 forward = CameraForward(m_editorCameraRotation);
    const Vec3 right = Normalize(Cross(forward, {0, 1, 0})), up = Normalize(Cross(right, forward));
    const float tangent = std::tan(DegreesToRadians(m_editorCameraFov) * 0.5f);
    const Ray ray{m_editorCameraPosition, Normalize(forward + right * ((normalizedX * 2.0f - 1.0f) * tangent * aspect) +
                                                   up * ((1.0f - normalizedY * 2.0f) * tangent))};
    float closest = std::numeric_limits<float>::max();
    for (const Entity entity : context.world->GetEntityManager().GetAliveEntities())
    {
        if (const auto *active = context.world->GetComponent<ActiveComponent>(entity); active && !active->active)
            continue;
        Aabb bounds;
        float distance;
        if (EntityBounds(*context.world, entity, context.resourceManager, bounds) &&
            IntersectRayAabb(ray, bounds, distance) && distance < closest)
            closest = distance;
    }
    if (closest == std::numeric_limits<float>::max() && std::abs(ray.direction.y) > 1e-6f)
    {
        const float groundDistance = -ray.origin.y / ray.direction.y;
        if (groundDistance > 0.0001f)
            closest = groundDistance;
    }
    if (closest == std::numeric_limits<float>::max())
        return false;
    position = ray.origin + ray.direction * closest;
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

void EditorApp::DrawViewCube(float x, float y, float width, float height, bool hovered)
{
    const auto degrees = [](float radians) { return radians * (180.0f / Pi); };
    const auto &io = ImGui::GetIO();
    bool blocked = width < 150 || height < 150 || m_engine.IsPlaying() || m_packageFuture.valid() ||
        io.AppFocusLost || io.WantTextInput || io.KeyAlt || io.KeyCtrl || io.KeySuper ||
        m_sceneOrbitActive || m_scenePanActive || m_sceneAltOrbitActive ||
        m_activeGizmoAxis >= 0 || m_marqueeActive ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (width < 150 || height < 150) { m_viewCubeActive=false; return; }
    const ImVec2 cursor=ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({x+width-148,y+8});
    ImGui::BeginDisabled(blocked);
    ImGui::InvisibleButton("##ViewCube",{140,140});
    ImGui::EndDisabled();
    blocked |= ImGui::IsAnyItemActive() && !ImGui::IsItemActive();
    ImGui::SetCursorScreenPos({cursor.x,cursor.y-ImGui::GetStyle().ItemSpacing.y});
    ImGui::Dummy({0,0});
    if (blocked) m_viewCubeActive=false;
    const ImVec2 center{x + width - 78, y + 78}, mouse = ImGui::GetMousePos();
    const Vec3 forward = CameraForward(m_editorCameraRotation), right = Normalize(Cross(forward, {0, 1, 0}));
    const Vec3 up = Normalize(Cross(right, forward));
    const auto project = [&](Vec3 point) { return ImVec2{center.x + Dot(point, right)*56, center.y - Dot(point, up)*56}; };
    struct Face { Vec3 normal, u, v; const char *en, *zh; };
    const std::array<Face,6> faces{{
        {{0,0,1},{1,0,0},{0,1,0},"FRONT","前"}, {{0,0,-1},{-1,0,0},{0,1,0},"BACK","后"},
        {{1,0,0},{0,0,-1},{0,1,0},"RIGHT","右"}, {{-1,0,0},{0,0,1},{0,1,0},"LEFT","左"},
        {{0,1,0},{1,0,0},{0,0,-1},"TOP","上"}, {{0,-1,0},{1,0,0},{0,0,1},"BOTTOM","下"}}};
    const Vec3 rayOrigin = right*((mouse.x-center.x)/56) + up*((center.y-mouse.y)/56) - forward*3;
    int pointed = -1, cellU = 0, cellV = 0;
    Vec3 direction{};
    for (int face=0; face<6; ++face)
    {
        const auto &f=faces[face]; const float facing=Dot(f.normal,forward);
        if (facing >= -.0001f) continue;
        const Vec3 hit=rayOrigin + forward*((.5f-Dot(rayOrigin,f.normal))/facing);
        const float u=Dot(hit,f.u), v=Dot(hit,f.v);
        if (std::abs(u) <= .5f && std::abs(v) <= .5f)
        {
            pointed=face; cellU=u < -.27f ? -1 : u > .27f ? 1 : 0; cellV=v < -.27f ? -1 : v > .27f ? 1 : 0;
            direction=Normalize(f.normal + f.u*static_cast<float>(cellU) + f.v*static_cast<float>(cellV));
            break;
        }
    }
    const ImVec2 home{x+width-130,y+25};
    const bool homeHovered=std::abs(mouse.x-home.x)<12 && std::abs(mouse.y-home.y)<12;
    const bool interactive=hovered && !blocked && !m_viewCubeActive;
    ImDrawList *draw=ImGui::GetWindowDrawList();
    for (int face=0; face<6; ++face)
    {
        const auto &f=faces[face]; const float facing=-Dot(f.normal,forward);
        if (facing <= .0001f) continue;
        const auto point=[&](float u,float v) { return project(f.normal*.5f + f.u*u + f.v*v); };
        constexpr float splits[]={-.5f,-.27f,.27f,.5f};
        const int shade=static_cast<int>(150+facing*57);
        draw->AddQuadFilled(point(-.5f,-.5f),point(.5f,-.5f),point(.5f,.5f),point(-.5f,.5f),IM_COL32(shade,shade+4,shade+9,245));
        for (int u=0; u<3; ++u) for (int v=0; v<3; ++v)
        {
            if (interactive && pointed==face && cellU==u-1 && cellV==v-1)
                draw->AddQuadFilled(point(splits[u],splits[v]),point(splits[u+1],splits[v]),
                    point(splits[u+1],splits[v+1]),point(splits[u],splits[v+1]),IM_COL32(93,170,235,255));
        }
        // Bevel lines make the face, edge and corner targets legible without six overlapping labels.
        for (float edge : {-.27f,.27f})
        {
            draw->AddLine(point(edge,-.5f),point(edge,.5f),IM_COL32(70,80,93,48));
            draw->AddLine(point(-.5f,edge),point(.5f,edge),IM_COL32(70,80,93,48));
        }
        draw->AddQuad(point(-.5f,-.5f),point(.5f,-.5f),point(.5f,.5f),point(-.5f,.5f),IM_COL32(55,64,76,230),1.3f);
        if (facing>.35f)
        {
            const char *label=Localize(f.en,f.zh); const float fontSize=m_interfaceLanguage==InterfaceLanguage::English ? 10.0f : 15.0f;
            const auto size=ImGui::GetFont()->CalcTextSizeA(fontSize,1000,0,label); const auto at=project(f.normal*.5f);
            draw->AddText(ImGui::GetFont(),fontSize,{at.x-size.x*.5f,at.y-size.y*.5f},IM_COL32(28,37,49,255),label);
        }
    }
    draw->AddRectFilled({home.x-12,home.y-12},{home.x+12,home.y+12},IM_COL32(35,42,52,190),4);
    const ImU32 homeColor=interactive && homeHovered ? IM_COL32(100,185,255,255) : IM_COL32(220,229,240,245);
    draw->AddPolyline(std::array<ImVec2,3>{{{home.x-8,home.y},{home.x,home.y-7},{home.x+8,home.y}}}.data(),3,homeColor,ImDrawFlags_None,1.5f);
    draw->AddRect({home.x-5,home.y},{home.x+5,home.y+7},homeColor,1,ImDrawFlags_None,1.5f);
    // A restrained XYZ triad under the cube keeps the engine's Y-up convention explicit.
    const ImVec2 axisOrigin{center.x-22,center.y+53};
    for (int axis=0; axis<3; ++axis)
    {
        const Vec3 a=axis==0 ? Vec3{1,0,0} : axis==1 ? Vec3{0,1,0} : Vec3{0,0,1};
        const ImVec2 end{axisOrigin.x+Dot(a,right)*17,axisOrigin.y-Dot(a,up)*17};
        const ImU32 color=axis==0 ? IM_COL32(228,116,110,230) : axis==1 ? IM_COL32(114,204,142,230) : IM_COL32(118,166,236,230);
        draw->AddLine(axisOrigin,end,color,1.5f);
        if (std::hypot(end.x-axisOrigin.x,end.y-axisOrigin.y)>5) draw->AddText({end.x+2,end.y-6},color,axis==0 ? "X" : axis==1 ? "Y" : "Z");
    }
    if (interactive && (pointed>=0 || homeHovered))
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", homeHovered ? Localize("Home view","默认视角") :
            Localize("Drag to orbit | Click a face, edge or corner to align","拖拽环绕 | 点击面、边或角切换方向"));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_viewCubeActive=true; m_viewCubeDragged=false; m_viewCubeHome=homeHovered; m_viewCubeDirection=direction;
            m_viewCubeMouseStart=m_viewCubeMouseLast={mouse.x,mouse.y}; m_viewCubePivot=m_sceneOrbitPivot;
            const auto &context=m_engine.GetContext();
            if (context.world && context.world->IsAlive(m_selected))
            {
                Aabb bounds; const auto matrix=context.world->GetWorldMatrix(m_selected);
                m_viewCubePivot=EntityBounds(*context.world,m_selected,context.resourceManager,bounds) ?
                    (bounds.minimum+bounds.maximum)*.5f : Vec3{matrix.At(0,3),matrix.At(1,3),matrix.At(2,3)};
            }
            else if (Dot(m_viewCubePivot-m_editorCameraPosition,forward)<=.001f)
                m_viewCubePivot=m_editorCameraPosition+forward*5;
        }
    }
    if (!m_viewCubeActive) return;
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (!m_viewCubeHome && std::hypot(mouse.x-m_viewCubeMouseStart.x,mouse.y-m_viewCubeMouseStart.y)>3)
        m_viewCubeDragged=true;
    if (m_viewCubeDragged && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const float dx=mouse.x-m_viewCubeMouseLast.x, dy=mouse.y-m_viewCubeMouseLast.y;
        const Vec3 aim=Normalize(m_viewCubePivot-m_editorCameraPosition);
        const float distance=std::max(.5f,Length(m_viewCubePivot-m_editorCameraPosition));
        const float oldPitch=degrees(std::asin(std::clamp(aim.y,-1.0f,1.0f)));
        const float yaw=degrees(std::atan2(aim.x,-aim.z));
        const float pitch=std::clamp(oldPitch-dy*.4f,-89.0f,89.0f);
        m_editorCameraRotation.x=std::clamp(m_editorCameraRotation.x+pitch-oldPitch,-89.0f,89.0f);
        m_editorCameraRotation.y+=dx*.4f;
        m_editorCameraPosition=m_viewCubePivot-CameraForward({pitch,yaw+dx*.4f,0})*distance;
        m_sceneOrbitPivot=m_viewCubePivot;
        m_viewCubeMouseLast={mouse.x,mouse.y};
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (!m_viewCubeDragged && hovered && ((m_viewCubeHome && homeHovered) || (!m_viewCubeHome && pointed>=0)))
        {
            const float distance=std::max(.5f,Length(m_viewCubePivot-m_editorCameraPosition));
            const Vec3 aim=m_viewCubeHome ? CameraForward({-25,-35,0}) : m_viewCubeDirection*-1;
            m_editorCameraRotation={std::clamp(degrees(std::asin(std::clamp(aim.y,-1.0f,1.0f))),-89.0f,89.0f),
                                    degrees(std::atan2(aim.x,-aim.z)),0};
            m_editorCameraPosition=m_viewCubePivot-CameraForward(m_editorCameraRotation)*distance;
            m_sceneOrbitPivot=m_viewCubePivot;
        }
        m_viewCubeActive=false;
    }
}

void EditorApp::DrawSceneOverlays(float x, float y, float width, float height, bool hovered)
{
    const auto &context = m_engine.GetContext();
    if (!context.renderer || !context.world)
        return;
    const FrameView &view = context.renderer->GetFrameView();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({x, y}, {x + width, y + height}, true);
    if (m_showSceneGrid && !m_engine.IsPlaying())
        DrawGroundGrid(draw,view,x,y,width,height);
    if (m_showSelectionBounds && !m_engine.IsPlaying())
    {
        std::vector<Entity> selected = m_selection;
        if (selected.empty() && context.world->IsAlive(m_selected))
            selected.push_back(m_selected);
        for (Entity entity : selected)
        {
            if (!context.world->IsAlive(entity))
                continue;
            Aabb bounds;
            const ImU32 color = entity == m_selected ? IM_COL32(255, 193, 75, 240) : IM_COL32(100, 175, 255, 215);
            if (EntityBounds(*context.world, entity, context.resourceManager, bounds))
                DrawBounds(draw, view, bounds, x, y, width, height, color);
            else
            {
                const Mat4 matrix = context.world->GetWorldMatrix(entity);
                ImVec2 point;
                if (ProjectToScreen(view, {matrix.At(0, 3), matrix.At(1, 3), matrix.At(2, 3)}, x, y, width, height, point))
                {
                    draw->AddLine({point.x - 5, point.y}, {point.x + 5, point.y}, color, 1.5f);
                    draw->AddLine({point.x, point.y - 5}, {point.x, point.y + 5}, color, 1.5f);
                }
            }
        }
    }
    DrawViewCube(x, y, width, height, hovered);
    draw->PopClipRect();
}

bool EditorApp::PlaceAssetInScene(const AssetInfo &asset, const Vec3 *position)
{
    if (m_engine.IsPlaying() || m_packageFuture.valid() ||
        (position && (!std::isfinite(position->x) || !std::isfinite(position->y) || !std::isfinite(position->z))))
        return false;
    World &world = *m_engine.GetContext().world;
    if (asset.type == "Prefab")
    {
        const std::string snapshot = m_engine.GetSceneManager().CreateSnapshot();
        const auto roots = Prefab::Instantiate(world, std::filesystem::u8path(asset.path));
        if (roots.empty())
        {
            Notify(std::string(Localize("Unable to place prefab: ", "无法放置预制体：")) + asset.path, FeedbackKind::Error);
            return false;
        }
        if (position)
        {
            const Vec3 offset = *position - RootsAnchor(world, roots);
            for (Entity root : roots)
                if (auto *transform = world.GetComponent<TransformComponent>(root))
                    transform->position += offset;
        }
        CommitUndoSnapshot(snapshot);
        m_selection = roots;
        m_selected = roots.front();
    }
    else if (asset.type == "Mesh")
    {
        RecordUndo();
        m_selected = world.CreateEntity(std::filesystem::u8path(asset.path).stem().u8string());
        world.AddComponent(m_selected, MeshComponent{asset.path, "assets/default.mat"});
        if (position)
            world.GetComponent<TransformComponent>(m_selected)->position = *position;
        m_selection = {m_selected};
    }
    else
        return false;
    m_selectedAsset.clear();
    m_inspectorAssetDrafts.clear();
    m_inspectorErrors.clear();
    Notify(std::string(Localize("Placed in scene: ", "已放入场景：")) + asset.path, FeedbackKind::Success);
    return true;
}

}
