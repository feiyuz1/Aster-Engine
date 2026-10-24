#include "asset_trash.h"
#include "editor_app.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/resource/asset_maintenance.h"
#include <algorithm>
#include <imgui.h>

namespace tinyengine
{
void EditorApp::loadAssetTrashHistory()
{
    const auto project = m_engine.getProjectSettings().projectFile.parent_path();
    if (m_assetTrashProject == project)
        return;
    m_assetTrashProject = project;
    m_assetDeleteHistory.clear();
    const auto directory = project / "saved/DeletedAssets";
    std::error_code error;
    std::vector<std::pair<std::filesystem::file_time_type, std::string>> manifests;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
    {
        std::error_code status;
        if (it->is_symlink(status) || !it->is_directory(status))
            continue;
        const auto path = it->path() / "manifest.txt";
        if (std::filesystem::is_regular_file(path, status) && !std::filesystem::is_symlink(path, status))
            manifests.emplace_back(std::filesystem::last_write_time(path, status), toUtf8GenericPath(path));
    }
    std::sort(manifests.begin(), manifests.end());
    for (const auto& manifest : manifests)
        m_assetDeleteHistory.push_back(manifest.second);
}

std::vector<std::string> EditorApp::AssetDeletionBlockers(const std::vector<std::string>& paths)
{
    std::vector<std::string> result;
    auto* resources = m_engine.GetContext().resourceManager;
    if (!resources)
        return {localize("Asset registry is unavailable.", "资产注册表不可用。")};
    const auto same = [&](const std::string& a, const std::string& b, const std::string& owner = std::string{})
    {
        if (a.empty() || b.empty())
            return false;
        std::error_code error;
        const auto left = fromUtf8Path(resources->resolveAssetPath(a, owner));
        const auto right = fromUtf8Path(resources->resolveAssetPath(b));
        if (std::filesystem::equivalent(left, right, error))
            return true;
        return left.lexically_normal() == right.lexically_normal();
    };
    const auto check = [&](const std::string& reference, const std::string& owner, const std::string& label)
    {
        for (const auto& path : paths)
            if (same(reference, path, owner))
                result.push_back(label + " → " + toUtf8Path(fromUtf8Path(path).filename()));
    };
    for (const auto& ref : AssetTrash::FindReferences(*resources, paths))
        result.push_back(ref.owner + " → " + ref.target);
    const auto inspectWorld = [&](World& world, const std::string& owner, const std::string& label)
    {
        for (const auto entity : world.GetEntityManager().GetAliveEntities())
            for (const auto& component : ReflectionRegistry::Get().GetComponents())
                if (component.has(world, entity))
                    for (const auto& property : component.properties)
                        if (property.type == ReflectedPropertyType::String && !property.assetType.empty() &&
                            property.access)
                            if (const auto* value = static_cast<const std::string*>(property.access(world, entity)))
                                check(*value, owner, label);
    };
    const auto currentScene = toUtf8GenericPath(m_engine.GetSceneManager().GetCurrentScene());
    if (auto* world = m_engine.GetContext().world)
        inspectWorld(*world, currentScene, localize("Current scene", "当前场景"));
    const auto inspectHistory =
        [&](const std::vector<std::string>& history, const std::string& owner, const char* label)
    {
        for (const auto& state : history)
        {
            World snapshot;
            if (SceneSerializer::deserialize(snapshot, state))
                inspectWorld(snapshot, owner, label);
            else if (!state.empty())
                result.push_back(localize("Cannot verify scene history. Reopen the project before deleting assets.",
                                          "无法验证场景历史引用，请重新打开项目后再删除资产。"));
        }
    };
    inspectHistory(m_undo, currentScene, localize("Scene undo history", "场景撤销历史"));
    inspectHistory(m_redo, currentScene, localize("Scene redo history", "场景重做历史"));
    for (std::size_t i = 0; i < m_sceneDocuments.size(); ++i)
    {
        const auto& document = m_sceneDocuments[i];
        check(toUtf8GenericPath(document.path), {}, localize("Open scene document", "已打开的场景文档"));
        if (i == m_activeSceneDocument || document.snapshot.empty())
            continue;
        World snapshot;
        if (SceneSerializer::deserialize(snapshot, document.snapshot))
            inspectWorld(snapshot, toUtf8GenericPath(document.path),
                         std::string(localize("Open scene: ", "已打开的场景：")) +
                             toUtf8Path(document.path.filename()));
        inspectHistory(document.undo, toUtf8GenericPath(document.path),
                       localize("Other scene undo history", "其他场景的撤销历史"));
        inspectHistory(document.redo, toUtf8GenericPath(document.path),
                       localize("Other scene redo history", "其他场景的重做历史"));
    }
    const auto& project = m_engine.getProjectSettings();
    for (const auto& reference : {project.startupScene, project.environmentTexture, project.inputBindings})
        if (!reference.empty())
            check(toUtf8GenericPath(project.resolve(reference)), {}, localize("Project settings", "项目设置"));
    const auto checkDirty = [&](const auto& editor)
    {
        if (editor.isDirty())
            check(editor.GetPath(), {},
                  localize("Unsaved material document; save or discard first", "材质尚未保存，请先保存或放弃修改"));
    };
    checkDirty(m_materialGraphEditor);
    checkDirty(m_materialAssetEditor);
    if (isShaderDirty())
    {
        check(m_openShaderAsset, {}, localize("Unsaved shader document", "尚未保存的着色器文档"));
        for (const auto& path : paths)
        {
            std::string text(m_shaderSource.empty() ? "" : m_shaderSource.data());
            const auto source = fromUtf8Path(resources->resolveAssetPath(path));
            if (AssetMaintenance::rewriteShaderIncludes(text, fromUtf8Path(m_openShaderAsset),
                                                        fromUtf8Path(m_openShaderAsset), source,
                                                        fromUtf8Path(toUtf8GenericPath(source) + ".trash-probe")))
                result.push_back(std::string(localize("Unsaved shader includes: ", "未保存的着色器引用：")) + path);
        }
    }
    const auto retainedMaterialReferences = [&](const auto& editor)
    {
        if ((!editor.isVisible() && !editor.isDirty()) ||
            (!editor.isDirty() &&
             std::any_of(paths.begin(), paths.end(), [&](const auto& path) { return same(editor.GetPath(), path); })))
            return;
        for (const auto& reference : editor.GetRetainedAssetReferences())
            check(reference, editor.GetPath(), localize("Material document and undo history", "材质文档及撤销历史"));
    };
    if (m_animationAssetEditor.isDirty())
    {
        check(m_animationAssetEditor.path(), {}, localize("Unsaved animation document", "动画尚未保存"));
    }
    const bool deletingCleanAnimation = !m_animationAssetEditor.isDirty() &&
        std::any_of(paths.begin(), paths.end(), [&](const auto& path) { return same(m_animationAssetEditor.path(), path); });
    if (!deletingCleanAnimation && (m_animationAssetEditor.isVisible() || m_animationAssetEditor.isDirty()))
    {
        for (const auto& reference : m_animationAssetEditor.retainedAssetPaths())
        {
            check(reference, {}, localize("Animation preview and undo history", "动画预览及撤销历史"));
        }
    }
    retainedMaterialReferences(m_materialGraphEditor);
    retainedMaterialReferences(m_materialAssetEditor);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

void EditorApp::requestDeleteAssets()
{
    if (m_engine.isPlaying() || m_packageFuture.valid())
        return;
    m_assetDeletePaths = m_selectedAssets.empty() ? std::vector<std::string>{m_selectedAsset} : m_selectedAssets;
    m_assetDeletePaths.erase(std::remove(m_assetDeletePaths.begin(), m_assetDeletePaths.end(), std::string{}),
                             m_assetDeletePaths.end());
    if (m_assetDeletePaths.empty())
        return;
    m_assetDeleteError.clear();
    m_assetDeleteRequested = true;
}

void EditorApp::drawAssetDeleteDialog()
{
    const auto* name = localize("Delete selected assets?###DeleteAssets", "删除所选资产？###DeleteAssets");
    if (m_assetDeleteRequested)
    {
        ImGui::OpenPopup(name);
        m_assetDeleteRequested = false;
    }
    ImGui::SetNextWindowSize({scaled(620), 0}, ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(name, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextWrapped(
        "%s", localize("Assets and their metadata will move to the project trash. Use Ctrl+Z in Assets to restore.",
                       "资产及元数据将移入项目回收站。在资产面板按 Ctrl+Z 可恢复。"));
    ImGui::BeginChild(
        "SelectedForDeletion",
        {0, std::min(scaled(130), ImGui::GetTextLineHeightWithSpacing() * (m_assetDeletePaths.size() + 1))},
        ImGuiChildFlags_Borders);
    for (const auto& path : m_assetDeletePaths)
        ImGui::TextUnformatted(toUtf8Path(fromUtf8Path(path).filename()).c_str());
    ImGui::EndChild();
    const auto blockers = AssetDeletionBlockers(m_assetDeletePaths);
    if (!blockers.empty())
    {
        ImGui::TextColored({1, .65f, .25f, 1}, "%s",
                           localize("Referenced assets cannot be deleted. Remove these references first:",
                                    "资产仍被引用，请先移除以下引用："));
        ImGui::BeginChild("DeletionReferences", {0, scaled(180)}, ImGuiChildFlags_Borders);
        for (const auto& ref : blockers)
            ImGui::TextWrapped("%s", ref.c_str());
        ImGui::EndChild();
    }
    if (!m_assetDeleteError.empty())
        ImGui::TextWrapped("%s", m_assetDeleteError.c_str());
    ImGui::BeginDisabled(!blockers.empty() || m_engine.isPlaying() || m_packageFuture.valid());
    if (ImGui::Button(localize("Move to Trash###ConfirmDeleteAssets", "移入回收站###ConfirmDeleteAssets")))
    {
        auto* resources = m_engine.GetContext().resourceManager;
        std::string manifest;
        if (resources && AssetTrash::moveToTrash(
                             *resources, toUtf8GenericPath(m_engine.getProjectSettings().projectFile.parent_path()),
                             m_assetDeletePaths, manifest, m_assetDeleteError))
        {
            m_assetDeleteHistory.push_back(manifest);
            if (auto* renderer = m_engine.GetContext().renderer)
                for (const auto& path : m_assetDeletePaths)
                    renderer->GetMaterialSystem().invalidateAsset(path);
            for (const auto& path : m_assetDeletePaths)
                resources->unloadUnused(path);
            const auto removed = [&](const std::string& path) {
                return std::find(m_assetDeletePaths.begin(), m_assetDeletePaths.end(), path) !=
                       m_assetDeletePaths.end();
            };
            if (removed(m_openMaterialGraphAsset))
            {
                m_materialGraphEditor.hide();
                m_openMaterialGraphAsset.clear();
            }
            if (removed(m_openMaterialAsset))
            {
                m_materialAssetEditor.hide();
                m_openMaterialAsset.clear();
            }
            if (removed(m_openShaderAsset))
            {
                m_openShaderAsset.clear();
                m_shaderEditorHidden = true;
            }
            if (removed(m_animationAssetEditor.path()))
            {
                m_animationAssetEditor = AnimationAssetEditor{};
            }
            if (removed(m_editingAsset))
                m_editingAsset.clear();
            if (removed(m_previewAsset))
            {
                m_previewAsset.clear();
                m_showAssetPreview = false;
            }
            m_selectedAsset.clear();
            m_selectedAssets.clear();
            m_assetReferencesDirty = true;
            for (const auto& path : m_assetDeletePaths)
                m_thumbnailJobs.erase(path);
            notify(localize("Assets moved to trash. Ctrl+Z in Assets restores them.",
                            "资产已移入回收站，在资产面板按 Ctrl+Z 可恢复。"),
                   FeedbackKind::Success);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(localize("Cancel###CancelDeleteAssets", "取消###CancelDeleteAssets")))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

bool EditorApp::restoreDeletedAssets()
{
    loadAssetTrashHistory();
    if (m_assetDeleteHistory.empty() || m_engine.isPlaying() || m_packageFuture.valid())
        return false;
    auto* resources = m_engine.GetContext().resourceManager;
    std::string error;
    if (!resources || !AssetTrash::Restore(*resources, m_assetDeleteHistory.back(), error))
    {
        notify(error, FeedbackKind::Error);
        return false;
    }
    m_assetDeleteHistory.pop_back();
    m_assetReferencesDirty = true;
    notify(localize("Deleted assets restored.", "已恢复删除的资产。"), FeedbackKind::Success);
    return true;
}
} // namespace tinyengine
