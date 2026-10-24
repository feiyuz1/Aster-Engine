#include "editor_app.h"
#include "tinyengine/ecs/components.h"
#include "tinyengine/platform/platform.h"
#include <algorithm>

namespace tinyengine
{
bool EditorApp::canApplyMaterialToSelection() const
{
    if (m_engine.isPlaying() || m_packageFuture.valid())
        return false;
    const auto* world = m_engine.GetContext().world;
    const auto targets = GetInspectorTargets();
    return world && std::any_of(targets.begin(), targets.end(),
                                [&](Entity e) { return world->GetComponent<MeshComponent>(e) != nullptr; });
}

bool EditorApp::applyMaterialToMeshes(const std::string& path, const std::vector<Entity>& targets)
{
    if (m_engine.isPlaying() || m_packageFuture.valid())
        return false;
    auto* resources = m_engine.GetContext().resourceManager;
    auto* world = m_engine.GetContext().world;
    if (!resources || !world)
        return false;
    const auto resolved = resources->resolveAssetPath(path);
    const auto asset = resources->getAssetRegistry().Find(resolved);
    if (!asset || asset->type != "Material")
        return false;
    auto stored = resolved;
    std::error_code error;
    const auto root = m_engine.getProjectSettings().projectFile.parent_path();
    const auto relative = std::filesystem::relative(fromUtf8Path(resolved), root, error);
    if (!error && !root.empty() && !relative.empty() && *relative.begin() != "..")
        stored = toUtf8GenericPath(relative);
    std::vector<MeshComponent*> meshes;
    bool hasTarget = false;
    for (const auto entity : targets)
        if (world->isAlive(entity))
            if (auto* mesh = world->GetComponent<MeshComponent>(entity))
            {
                hasTarget = true;
                if (mesh->materialPath != stored && std::find(meshes.begin(), meshes.end(), mesh) == meshes.end())
                    meshes.push_back(mesh);
            }
    if (meshes.empty())
        return hasTarget;
    recordUndo();
    for (auto* mesh : meshes)
        mesh->materialPath = stored;
    m_selectedAsset.clear();
    m_selectedAssets.clear();
    notify(std::string(localize("Applied material: ", "已应用材质：")) + stored, FeedbackKind::Success);
    return true;
}
} // namespace tinyengine
