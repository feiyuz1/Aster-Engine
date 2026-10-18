#include "EditorApp.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include <algorithm>

namespace MiniEngine
{
bool EditorApp::CanApplyMaterialToSelection() const
{
    if (m_engine.IsPlaying() || m_packageFuture.valid())
        return false;
    const auto *world = m_engine.GetContext().world;
    const auto targets = GetInspectorTargets();
    return world && std::any_of(targets.begin(), targets.end(), [&](Entity e) { return world->GetComponent<MeshComponent>(e) != nullptr; });
}

bool EditorApp::ApplyMaterialToMeshes(const std::string &path, const std::vector<Entity> &targets)
{
    if (m_engine.IsPlaying() || m_packageFuture.valid())
        return false;
    auto *resources = m_engine.GetContext().resourceManager;
    auto *world = m_engine.GetContext().world;
    if (!resources || !world)
        return false;
    const auto resolved = resources->ResolveAssetPath(path);
    const auto asset = resources->GetAssetRegistry().Find(resolved);
    if (!asset || (asset->type != "Material" && asset->type != "MaterialGraph"))
        return false;
    auto stored = resolved;
    std::error_code error;
    const auto root = m_engine.GetProjectSettings().projectFile.parent_path();
    const auto relative = std::filesystem::relative(std::filesystem::u8path(resolved), root, error);
    if (!error && !root.empty() && !relative.empty() && *relative.begin() != "..")
        stored = relative.generic_u8string();
    std::vector<MeshComponent *> meshes;
    bool hasTarget = false;
    for (const auto entity : targets)
        if (world->IsAlive(entity))
            if (auto *mesh = world->GetComponent<MeshComponent>(entity))
            {
                hasTarget = true;
                if (mesh->materialPath != stored && std::find(meshes.begin(), meshes.end(), mesh) == meshes.end())
                    meshes.push_back(mesh);
            }
    if (meshes.empty())
        return hasTarget;
    RecordUndo();
    for (auto *mesh : meshes)
        mesh->materialPath = stored;
    m_selectedAsset.clear();
    m_selectedAssets.clear();
    Notify(std::string(Localize("Applied material: ", "已应用材质：")) + stored, FeedbackKind::Success);
    return true;
}
} // namespace MiniEngine
