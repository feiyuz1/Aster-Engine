#include "EditorApp.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace MiniEngine
{
namespace
{
bool SameValue(const ReflectedProperty &property, const void *a, const void *b, int axis = -1)
{
    if (!a || !b) return false;
    switch (property.type)
    {
    case ReflectedPropertyType::Bool: return *static_cast<const bool *>(a) == *static_cast<const bool *>(b);
    case ReflectedPropertyType::UInt: return *static_cast<const std::uint32_t *>(a) == *static_cast<const std::uint32_t *>(b);
    case ReflectedPropertyType::Entity: return *static_cast<const Entity *>(a) == *static_cast<const Entity *>(b);
    case ReflectedPropertyType::Float: return *static_cast<const float *>(a) == *static_cast<const float *>(b);
    case ReflectedPropertyType::Enum: return *static_cast<const int *>(a) == *static_cast<const int *>(b);
    case ReflectedPropertyType::String: return *static_cast<const std::string *>(a) == *static_cast<const std::string *>(b);
    case ReflectedPropertyType::Vec3: {
        const auto &left = *static_cast<const Vec3 *>(a), &right = *static_cast<const Vec3 *>(b);
        return axis == 0 ? left.x == right.x : axis == 1 ? left.y == right.y : axis == 2 ? left.z == right.z
               : left.x == right.x && left.y == right.y && left.z == right.z;
    }
    }
    return false;
}
bool MatchesAsset(const ReflectedProperty &property, const AssetInfo &asset)
{
    // AnimationComponent::clipPath also accepts a blend tree; Next Clip remains a single animation.
    return asset.type == property.assetType || (property.assetType == "Material" && asset.type == "MaterialGraph") ||
           (property.assetType == "Animation" && asset.extension == ".anim") ||
           (property.assetType == "Animation" && property.name == "Clip" && asset.type == "BlendTree");
}
std::string SearchKey(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
}

std::vector<Entity> EditorApp::GetInspectorTargets() const
{
    std::vector<Entity> targets;
    const auto *world = m_engine.GetContext().world;
    if (!world) return targets;
    const auto selection = m_selection.empty() ? std::vector<Entity>{m_selected} : m_selection;
    for (const auto entity : selection)
        if (world->IsAlive(entity) && std::find(targets.begin(), targets.end(), entity) == targets.end()) targets.push_back(entity);
    return targets;
}

bool EditorApp::ApplyInspectorProperty(const ReflectedComponent &component, const ReflectedProperty &property,
                                       const void *value, bool continuous, int axis, bool record)
{
    if (!value || m_engine.IsPlaying() || m_packageFuture.valid()) return false;
    auto &world = *m_engine.GetContext().world;
    const auto targets = GetInspectorTargets();
    if (targets.empty() || std::any_of(targets.begin(), targets.end(), [&](Entity e) { return !component.has(world, e); })) return false;
    bool changed = false;
    for (const auto entity : targets)
    {
        const int enumValue = property.getEnum ? property.getEnum(world, entity) : 0;
        const auto *current = property.type == ReflectedPropertyType::Enum ? static_cast<const void *>(&enumValue)
                              : property.access ? property.access(world, entity) : nullptr;
        changed = !SameValue(property, current, value, axis) || changed;
    }
    if (!changed) return false;
    if (record) { if (continuous) RecordPropertyUndo(); else RecordUndo(); }
    for (const auto entity : targets)
    {
        void *current = property.access ? property.access(world, entity) : nullptr;
        if (property.type == ReflectedPropertyType::Enum) { property.setEnum(world, entity, *static_cast<const int *>(value)); continue; }
        if (!current) continue;
        switch (property.type)
        {
        case ReflectedPropertyType::Bool: *static_cast<bool *>(current) = *static_cast<const bool *>(value); break;
        case ReflectedPropertyType::UInt: *static_cast<std::uint32_t *>(current) = *static_cast<const std::uint32_t *>(value); break;
        case ReflectedPropertyType::Entity: *static_cast<Entity *>(current) = *static_cast<const Entity *>(value); break;
        case ReflectedPropertyType::Float: *static_cast<float *>(current) = *static_cast<const float *>(value); break;
        case ReflectedPropertyType::String: *static_cast<std::string *>(current) = *static_cast<const std::string *>(value); break;
        case ReflectedPropertyType::Vec3: {
            auto &target = *static_cast<Vec3 *>(current);
            const auto &edited = *static_cast<const Vec3 *>(value);
            if (axis == 0) target.x = edited.x;
            else if (axis == 1) target.y = edited.y;
            else if (axis == 2) target.z = edited.z;
            else target = edited;
            break;
        }
        case ReflectedPropertyType::Enum: break;
        }
    }
    return true;
}

bool EditorApp::ApplyInspectorAsset(const ReflectedComponent &component, const ReflectedProperty &property,
                                    const std::string &path, std::uint32_t fieldId)
{
    if (m_engine.IsPlaying() || m_packageFuture.valid()) return false;
    m_selectedAsset.clear();
    const auto *resources = m_engine.GetContext().resourceManager;
    auto asset = resources && !path.empty() ? resources->GetAssetRegistry().Find(path) : std::optional<AssetInfo>{};
    if (resources && !path.empty() && !asset)
        asset = resources->GetAssetRegistry().Find(resources->ResolveAssetPath(path));
    if (!path.empty() && (!asset || !MatchesAsset(property, *asset)))
    {
        std::string expected = LocalizeReflected(property.assetType);
        if (property.assetType == "Animation" && property.name == "Clip") expected += Localize(" / Blend Tree", " / 混合树");
        m_inspectorErrors[fieldId] = asset ? std::string(Localize("Expected asset type: ", "需要的资产类型：")) + expected
                                              : std::string(Localize("Asset not found: ", "未找到资产：")) + path;
        Notify(m_inspectorErrors[fieldId], FeedbackKind::Error);
        return false;
    }
    std::string storedPath = path;
    const auto projectRoot = m_engine.GetProjectSettings().projectFile.parent_path();
    if (!path.empty() && resources && !projectRoot.empty())
    {
        std::error_code error;
        const auto relative = std::filesystem::relative(std::filesystem::u8path(resources->ResolveAssetPath(path)), projectRoot, error);
        if (!error && !relative.empty() && *relative.begin() != "..") storedPath = relative.generic_u8string();
    }
    ApplyInspectorProperty(component, property, &storedPath, false);
    m_inspectorErrors.erase(fieldId);
    m_inspectorAssetDrafts.erase(fieldId);
    return true;
}

void EditorApp::ResetInspectorComponent(const ReflectedComponent &component)
{
    if (m_engine.IsPlaying() || m_packageFuture.valid()) return;
    World defaults;
    const auto entity = defaults.CreateEntity();
    if (!component.has(defaults, entity)) component.add(defaults, entity);
    auto &world = *m_engine.GetContext().world;
    const auto targets = GetInspectorTargets();
    if (targets.empty() || std::any_of(targets.begin(), targets.end(), [&](Entity e) { return !component.has(world, e); })) return;
    bool recorded = false;
    for (const auto &property : component.properties)
    {
        const int defaultEnum = property.getEnum ? property.getEnum(defaults, entity) : 0;
        const void *value = property.type == ReflectedPropertyType::Enum ? static_cast<const void *>(&defaultEnum)
                           : property.access ? property.access(defaults, entity) : nullptr;
        for (const auto target : targets)
        {
            const int currentEnum = property.getEnum ? property.getEnum(world, target) : 0;
            const void *current = property.type == ReflectedPropertyType::Enum ? static_cast<const void *>(&currentEnum)
                                 : property.access ? property.access(world, target) : nullptr;
            if (!recorded && !SameValue(property, value, current)) { RecordUndo(); recorded = true; }
        }
        ApplyInspectorProperty(component, property, value, false, -1, false);
    }
    m_inspectorAssetDrafts.clear();
    m_inspectorErrors.clear();
    if (recorded) Notify(Localize("Component reset. Ctrl+Z to undo.", "组件已重置，可用 Ctrl+Z 撤销。"), FeedbackKind::Success);
}

void EditorApp::DrawEntityInspector(World &world)
{
    const auto targets = GetInspectorTargets();
    if (targets.empty()) return;
    if (m_engine.IsPlaying()) ImGui::TextWrapped("%s", Localize("Stop play mode to edit properties.", "停止运行后可编辑属性。"));
    if (targets.size() > 1)
        ImGui::TextWrapped(Localize("Editing %zu objects. Shared properties only; -- means mixed values.",
                                   "正在编辑 %zu 个对象的共有属性；-- 表示值不同。"), targets.size());
    ImGui::BeginDisabled(m_engine.IsPlaying());
    const auto &registry = ReflectionRegistry::Get();
    std::vector<const ReflectedComponent *> components;
    for (const auto *name : {"Name", "Active", "Tag", "Layer"})
        if (const auto *component = registry.FindComponent(name)) components.push_back(component);
    for (const auto &component : registry.GetComponents())
        if (component.name != "Name" && component.name != "Active" && component.name != "Tag" && component.name != "Layer") components.push_back(&component);
    bool objectExpanded = true;
    for (const auto *entry : components)
    {
        const auto &component = *entry;
        if (std::any_of(targets.begin(), targets.end(), [&](Entity e) { return !component.has(world, e); })) continue;
        ImGui::PushID(component.name.c_str());
        const auto label = LocalizeReflected(component.name);
        const bool basic = component.name == "Name" || component.name == "Active" || component.name == "Tag" || component.name == "Layer";
        bool expanded = objectExpanded;
        if (!basic || component.name == "Name")
        {
            const float actionsX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 28;
            expanded = ImGui::CollapsingHeader(basic ? Localize("Object", "对象") : label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
            const bool contextClick = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            if (basic) objectExpanded = expanded;
            else
            {
                ImGui::SameLine(actionsX);
                if (ImGui::SmallButton("...") || contextClick) ImGui::OpenPopup("ComponentActions");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Localize("Component actions", "组件操作"));
            }
        }
        if (ImGui::BeginPopup("ComponentActions"))
        {
            if (ImGui::MenuItem(Localize("Reset to defaults", "重置为默认值"))) ResetInspectorComponent(component);
            if (component.removable && ImGui::MenuItem(Localize("Remove component", "移除组件")))
            {
                RecordUndo();
                for (const auto entity : targets) component.remove(world, entity);
                ImGui::EndPopup(); ImGui::PopID(); continue;
            }
            ImGui::EndPopup();
        }
        if (expanded && ImGui::BeginTable("Properties", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, std::clamp(ImGui::GetContentRegionAvail().x * .29f, 62.0f, 100.0f));
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            for (const auto &property : component.properties)
            {
                ImGui::PushID(property.name.c_str());
                const auto fieldId = ImGui::GetID("##Value");
                const auto propertyLabel = LocalizeReflected(property.name);
                void *value = property.access ? property.access(world, m_selected) : nullptr;
                int enumValue = property.getEnum ? property.getEnum(world, m_selected) : 0;
                const auto mixed = [&](int axis = -1) {
                    for (const auto target : targets)
                    {
                        const int otherEnum = property.getEnum ? property.getEnum(world, target) : 0;
                        const void *other = property.type == ReflectedPropertyType::Enum ? static_cast<const void *>(&otherEnum)
                                            : property.access ? property.access(world, target) : nullptr;
                        const void *active = property.type == ReflectedPropertyType::Enum ? static_cast<const void *>(&enumValue) : value;
                        if (!SameValue(property, active, other, axis)) return true;
                    }
                    return false;
                };
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding();
                ImGui::TextWrapped("%s", propertyLabel.c_str());
                if (mixed()) ImGui::TextDisabled("%s", Localize("Mixed", "多个值"));
                ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
                switch (property.type)
                {
                case ReflectedPropertyType::Bool: {
                    bool edited = value && *static_cast<bool *>(value);
                    ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, mixed());
                    if (ImGui::Checkbox("##Value", &edited)) ApplyInspectorProperty(component, property, &edited, false);
                    ImGui::PopItemFlag(); break;
                }
                case ReflectedPropertyType::UInt: {
                    auto edited = value ? *static_cast<std::uint32_t *>(value) : 0;
                    if (ImGui::InputScalar("##Value", ImGuiDataType_U32, &edited)) ApplyInspectorProperty(component, property, &edited);
                    break;
                }
                case ReflectedPropertyType::Entity: {
                    const auto current = value ? *static_cast<Entity *>(value) : InvalidEntity;
                    const auto *name = world.GetComponent<NameComponent>(current);
                    const std::string preview = mixed() ? "--" : name ? name->name : Localize("None", "无");
                    if (ImGui::BeginCombo("##Value", preview.c_str()))
                    {
                        if (ImGui::Selectable(Localize("None", "无"), current == InvalidEntity)) { const Entity none = InvalidEntity; ApplyInspectorProperty(component, property, &none, false); }
                        for (const auto entity : world.GetEntityManager().GetAliveEntities())
                        {
                            const auto *candidate = world.GetComponent<NameComponent>(entity);
                            const auto item = (candidate ? candidate->name : Localize("Entity", "实体")) + std::string("##") + std::to_string(entity);
                            if (ImGui::Selectable(item.c_str(), current == entity)) ApplyInspectorProperty(component, property, &entity, false);
                        }
                        ImGui::EndCombo();
                    }
                    break;
                }
                case ReflectedPropertyType::Float: {
                    float edited = value ? *static_cast<float *>(value) : 0;
                    if (ImGui::DragFloat("##Value", &edited, property.speed, property.hasRange ? property.minimum : 0,
                                         property.hasRange ? property.maximum : 0, mixed() ? "--" : "%.3f")) ApplyInspectorProperty(component, property, &edited);
                    break;
                }
                case ReflectedPropertyType::Vec3: {
                    Vec3 edited = value ? *static_cast<Vec3 *>(value) : Vec3{};
                    if (property.color)
                    {
                        if (ImGui::ColorEdit3("##Value", &edited.x)) ApplyInspectorProperty(component, property, &edited);
                    }
                    else
                    {
                        const float available = ImGui::GetContentRegionAvail().x;
                        const float gap = ImGui::GetStyle().ItemSpacing.x;
                        const float buttonWidth = ImGui::GetFrameHeight();
                        const float minimumValueWidth = ImGui::CalcTextSize("-00.000").x + ImGui::GetStyle().FramePadding.x * 2;
                        const bool stacked = available < 3 * (buttonWidth + minimumValueWidth) + gap * 5;
                        const float width = stacked ? available - buttonWidth - gap : (available - gap * 5 - buttonWidth * 3) / 3;
                        const char *axes[] = {"X", "Y", "Z"};
                        const ImVec4 colors[] = {{.65f,.19f,.23f,1}, {.20f,.47f,.28f,1}, {.20f,.36f,.65f,1}};
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            if (axis > 0 && !stacked) ImGui::SameLine();
                            ImGui::PushID(axis); ImGui::PushStyleColor(ImGuiCol_Button, colors[axis]);
                            if (ImGui::Button(axes[axis], {buttonWidth, 0}))
                            {
                                World defaults;
                                const auto defaultEntity = defaults.CreateEntity();
                                if (!component.has(defaults, defaultEntity)) component.add(defaults, defaultEntity);
                                if (const auto *defaultValue = static_cast<const Vec3 *>(property.access(defaults, defaultEntity)))
                                    (&edited.x)[axis] = (&defaultValue->x)[axis];
                                ApplyInspectorProperty(component, property, &edited, false, axis);
                            }
                            ImGui::PopStyleColor();
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Localize("Reset this axis", "重置此轴"));
                            ImGui::SameLine(); ImGui::SetNextItemWidth(std::max(24.0f, width));
                            const char *format = mixed(axis) ? "--" : component.name == "Transform" && property.name == "Rotation" ? "%.2f°" : "%.3f";
                            if (ImGui::DragFloat("##Value", &(&edited.x)[axis], property.speed, property.hasRange ? property.minimum : 0,
                                                 property.hasRange ? property.maximum : 0, format)) ApplyInspectorProperty(component, property, &edited, true, axis);
                            ImGui::PopID();
                        }
                    }
                    break;
                }
                case ReflectedPropertyType::String: {
                    if (!value) break;
                    auto edited = *static_cast<std::string *>(value);
                    if (!property.assetType.empty())
                    {
                        auto draft = m_inspectorAssetDrafts.find(fieldId);
                        if (draft != m_inspectorAssetDrafts.end()) edited = draft->second;
                    }
                    char buffer[2048]{}; std::snprintf(buffer, sizeof(buffer), "%s", edited.c_str());
                    if (property.assetType.empty())
                    {
                        if (ImGui::InputText("##Value", buffer, sizeof(buffer))) { edited = buffer; ApplyInspectorProperty(component, property, &edited); }
                    }
                    else
                    {
                        const bool submitted = ImGui::InputTextWithHint("##Value", mixed() ? "--" : Localize("Choose or drop an asset", "选择或拖入资产"), buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue);
                        if (ImGui::IsItemHovered() && !edited.empty()) ImGui::SetTooltip("%s", edited.c_str());
                        if (ImGui::IsItemEdited()) m_inspectorAssetDrafts[fieldId] = buffer;
                        if (submitted || ImGui::IsItemDeactivatedAfterEdit()) ApplyInspectorAsset(component, property, buffer, fieldId);
                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const auto *payload = ImGui::AcceptDragDropPayload("TINY_ASSET_PATH")) ApplyInspectorAsset(component, property, static_cast<const char *>(payload->Data), fieldId);
                            ImGui::EndDragDropTarget();
                        }
                        const char *chooseLabel = Localize("Choose...", "选择...");
                        const char *locateLabel = Localize("Locate", "定位");
                        const char *clearLabel = Localize("Clear", "清空");
                        const float controlWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                        const auto buttonWidth = [](const char *label) {
                            return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                        };
                        const bool buttonsInRow = buttonWidth(chooseLabel) + buttonWidth(locateLabel) + buttonWidth(clearLabel) +
                                                  ImGui::GetStyle().ItemSpacing.x * 2.0f <= controlWidth;
                        const auto referenceButton = [&](const char *label) {
                            return ImGui::Button(label, {buttonsInRow ? buttonWidth(label) : controlWidth, 0});
                        };
                        if (referenceButton(chooseLabel)) { m_assetPickerSearch.fill('\0'); ImGui::OpenPopup("AssetPicker"); }
                        if (buttonsInRow) ImGui::SameLine();
                        const auto &storedReference = *static_cast<const std::string *>(value);
                        ImGui::BeginDisabled(mixed() || storedReference.empty() || storedReference.rfind("builtin:", 0) == 0);
                        if (referenceButton(locateLabel)) LocateAsset(storedReference);
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("%s", Localize("Show the assigned asset in the asset browser", "在资产浏览器中显示已引用的资产"));
                        if (buttonsInRow) ImGui::SameLine();
                        if (referenceButton(clearLabel)) ApplyInspectorAsset(component, property, {}, fieldId);
                        const auto *viewport = ImGui::GetMainViewport();
                        ImGui::SetNextWindowSize({std::min(Scaled(420.0f), viewport->WorkSize.x * .9f),
                                                  std::min(Scaled(340.0f), viewport->WorkSize.y * .8f)}, ImGuiCond_Appearing);
                        if (ImGui::BeginPopup("AssetPicker"))
                        {
                            ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##Search", Localize("Search compatible assets...", "搜索匹配类型的资产..."), m_assetPickerSearch.data(), m_assetPickerSearch.size());
                            ImGui::BeginChild("CompatibleAssets", {0, 0}, ImGuiChildFlags_Borders);
                            if (const auto *resources = m_engine.GetContext().resourceManager)
                            {
                                auto assets = resources->GetAssetRegistry().GetAssets();
                                std::sort(assets.begin(), assets.end(), [](const auto &a, const auto &b) { return a.path < b.path; });
                                const auto search = SearchKey(m_assetPickerSearch.data());
                                for (const auto &asset : assets)
                                    if (MatchesAsset(property, asset) && (search.empty() || SearchKey(asset.path).find(search) != std::string::npos))
                                    {
                                        ImGui::PushID(asset.id.c_str());
                                        const auto name = std::filesystem::u8path(asset.path).filename().u8string();
                                        if (ImGui::Selectable(name.c_str(), resources->ResolveAssetPath(edited) == resources->ResolveAssetPath(asset.path)))
                                        { ApplyInspectorAsset(component, property, asset.path, fieldId); ImGui::CloseCurrentPopup(); }
                                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset.path.c_str());
                                        ImGui::PopID();
                                    }
                            }
                            ImGui::EndChild();
                            ImGui::EndPopup();
                        }
                        if (const auto error = m_inspectorErrors.find(fieldId); error != m_inspectorErrors.end())
                        { ImGui::PushStyleColor(ImGuiCol_Text, {1,.45f,.4f,1}); ImGui::TextWrapped("%s", error->second.c_str()); ImGui::PopStyleColor(); }
                    }
                    break;
                }
                case ReflectedPropertyType::Enum: {
                    const auto preview = mixed() ? std::string("--") : enumValue >= 0 && static_cast<std::size_t>(enumValue) < property.enumNames.size() ? LocalizeReflected(property.enumNames[enumValue]) : std::string(Localize("Unknown", "未知"));
                    if (ImGui::BeginCombo("##Value", preview.c_str()))
                    {
                        for (std::size_t i = 0; i < property.enumNames.size(); ++i)
                        { const auto option = LocalizeReflected(property.enumNames[i]); if (ImGui::Selectable(option.c_str(), enumValue == static_cast<int>(i))) { const int selected = static_cast<int>(i); ApplyInspectorProperty(component, property, &selected, false); } }
                        ImGui::EndCombo();
                    }
                    break;
                }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }
    if (ImGui::Button(Localize("Add Component...", "添加组件..."), {-1, 0})) { m_componentSearch.fill('\0'); ImGui::OpenPopup("AddComponentPopup"); }
    if (ImGui::BeginPopup("AddComponentPopup"))
    {
        ImGui::SetNextItemWidth(240); ImGui::InputTextWithHint("##Search", Localize("Search components...", "搜索组件..."), m_componentSearch.data(), m_componentSearch.size());
        const auto search = SearchKey(m_componentSearch.data());
        for (const auto &component : registry.GetComponents())
        {
            const auto label = LocalizeReflected(component.name);
            if (!component.removable || std::all_of(targets.begin(), targets.end(), [&](Entity e) { return component.has(world, e); }) ||
                (!search.empty() && SearchKey(label + " " + component.name).find(search) == std::string::npos)) continue;
            if (ImGui::MenuItem(label.c_str())) { RecordUndo(); for (const auto entity : targets) if (!component.has(world, entity)) component.add(world, entity); }
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
}
}
