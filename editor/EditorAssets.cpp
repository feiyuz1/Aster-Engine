#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

#include "EditorApp.hpp"
#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Render/SdlGlRenderDevice.hpp"
#include "MiniEngine/Resource/AssetMaintenance.hpp"
#include "MiniEngine/Resource/MaterialAssetReferences.hpp"
#include "MiniEngine/ECS/SceneSerializer.hpp"
#ifdef TINYENGINE_HAS_ASSIMP
#include "MiniEngine/Import/AssimpImporter.hpp"
#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif
#endif
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <string_view>

namespace MiniEngine
{
namespace
{
std::string Lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
std::filesystem::path NormalizedAssetPath(const std::filesystem::path &path)
{
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error) normalized = std::filesystem::absolute(path, error).lexically_normal();
    return error ? path.lexically_normal() : normalized;
}

std::string AssetPathKey(const std::filesystem::path &path)
{
    auto key = NormalizedAssetPath(path).generic_u8string();
#ifdef _WIN32
    key = Lowercase(std::move(key));
#endif
    return key;
}

bool AssetPathWithin(const std::filesystem::path &path, const std::filesystem::path &folder)
{
    const auto key = AssetPathKey(path);
    auto prefix = AssetPathKey(folder);
    if (key == prefix) return true;
    if (prefix.empty()) return false;
    if (prefix.back() != '/') prefix += '/';
    return key.compare(0, prefix.size(), prefix) == 0;
}

// Truncate at a UTF-8 boundary and measure the current scaled font, rather than byte count.
std::string FitAssetText(const std::string &text, float width)
{
    if (width <= 0.0f) return {};
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    const float suffixWidth = ImGui::CalcTextSize("...").x;
    if (suffixWidth > width) return {};
    std::size_t length = text.size();
    while (length > 0)
    {
        --length;
        while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) --length;
        if (ImGui::CalcTextSize(text.data(), text.data() + length).x + suffixWidth <= width)
            return text.substr(0, length) + "...";
    }
    return "...";
}

void DrawAssetIcon(ImDrawList *draw, ImVec2 origin, float size, const std::string &type)
{
    const bool folder = type == "Folder";
    const ImU32 color = folder ? IM_COL32(203, 170, 100, 255)
                       : type == "Texture" ? IM_COL32(103, 169, 128, 255)
                       : type == "Mesh" || type == "Prefab" ? IM_COL32(110, 159, 211, 255)
                       : type == "Material" || type == "MaterialGraph" ? IM_COL32(192, 136, 172, 255)
                       : type == "Shader" ? IM_COL32(213, 164, 109, 255)
                       : type == "Audio" ? IM_COL32(164, 146, 209, 255)
                       : IM_COL32(146, 163, 183, 255);
    const float inset = size * .13f;
    if (folder)
    {
        draw->AddRectFilled({origin.x + inset, origin.y + size * .22f},
                            {origin.x + size * .55f, origin.y + size * .46f}, color, size * .06f);
        draw->AddRectFilled({origin.x + inset, origin.y + size * .37f},
                            {origin.x + size - inset, origin.y + size * .81f}, color, size * .06f);
        return;
    }
    draw->AddRectFilled(origin, {origin.x + size, origin.y + size}, IM_COL32(38, 43, 51, 230), size * .13f);
    if (type == "Mesh" || type == "Prefab")
    {
        const ImVec2 top{origin.x + size * .5f, origin.y + size * .18f};
        const ImVec2 left{origin.x + size * .2f, origin.y + size * .36f};
        const ImVec2 right{origin.x + size * .8f, origin.y + size * .36f};
        const ImVec2 middle{origin.x + size * .5f, origin.y + size * .54f};
        draw->AddQuad(top, right, middle, left, color, 1.5f);
        draw->AddLine(left, {left.x, origin.y + size * .7f}, color, 1.5f);
        draw->AddLine(right, {right.x, origin.y + size * .7f}, color, 1.5f);
        draw->AddLine(middle, {middle.x, origin.y + size * .87f}, color, 1.5f);
        draw->AddLine({left.x, origin.y + size * .7f}, {middle.x, origin.y + size * .87f}, color, 1.5f);
        draw->AddLine({right.x, origin.y + size * .7f}, {middle.x, origin.y + size * .87f}, color, 1.5f);
    }
    else if (type == "Texture")
    {
        draw->AddRect({origin.x + inset, origin.y + inset}, {origin.x + size - inset, origin.y + size - inset}, color);
        draw->AddCircleFilled({origin.x + size * .68f, origin.y + size * .31f}, size * .09f, color);
        draw->AddTriangleFilled({origin.x + size * .18f, origin.y + size * .78f},
                                {origin.x + size * .46f, origin.y + size * .38f},
                                {origin.x + size * .76f, origin.y + size * .78f}, color);
    }
    else if (type == "Material" || type == "MaterialGraph")
    {
        draw->AddCircleFilled({origin.x + size * .5f, origin.y + size * .5f}, size * .32f, color);
        draw->AddCircle({origin.x + size * .43f, origin.y + size * .43f}, size * .17f, IM_COL32(239, 218, 233, 255));
    }
    else
    {
        const char *code = type == "Shader" ? "<>" : type == "Audio" ? "AU" : type == "Scene" ? "SC"
                         : type == "Animation" || type == "AnimationGraph" || type == "BlendTree" ? "AN"
                         : type == "Skeleton" ? "SK" : type == "VFX" ? "FX" : type == "BehaviorTree" ? "AI"
                         : type == "Font" ? "Aa" : type == "Environment" ? "EN" : type == "Input" ? "IN" : "DA";
        const float fontSize = std::min(ImGui::GetFontSize(), size * .48f);
        const auto textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, 1000.0f, 0.0f, code);
        draw->AddText(ImGui::GetFont(), fontSize,
                      {origin.x + (size - textSize.x) * .5f, origin.y + (size - textSize.y) * .5f}, color, code);
    }
}
#ifdef TINYENGINE_HAS_ASSIMP
std::string ValidateModelSource(const std::filesystem::path &source)
{
    if (source.empty())
        return "Select a source model file.";
    if (!std::filesystem::is_regular_file(source))
        return "The selected model file does not exist.";
    std::ifstream input(source, std::ios::binary);
    if (!input)
        return "The selected model file cannot be read.";
    if (!AssimpImporter::CanImport(source))
        return "The selected file extension is not supported by Assimp.";
    return {};
}

const char *ModelSourceErrorText(const std::string &error, const bool simplifiedChinese)
{
    if (!simplifiedChinese)
        return error.c_str();
    if (error == "Select a source model file.")
        return "请选择源模型文件。";
    if (error == "The selected model file does not exist.")
        return "选择的模型文件不存在。";
    if (error == "The selected model file cannot be read.")
        return "选择的模型文件无法读取。";
    if (error == "The selected file extension is not supported by Assimp.")
        return "Assimp 不支持所选文件的扩展名。";
    return error.c_str();
}

#ifdef _WIN32
struct NativeFileDialogResult
{
    std::optional<std::filesystem::path> path;
    std::string error;
};

NativeFileDialogResult OpenModelFileDialog(const HWND owner, const std::filesystem::path &initialDirectory, const bool simplifiedChinese)
{
    NativeFileDialogResult result;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    {
        result.error = "Windows COM initialization failed.";
        return result;
    }

    IFileOpenDialog *dialog = nullptr;
    HRESULT status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(status) || !dialog)
    {
        if (uninitialize)
            CoUninitialize();
        result.error = "Windows could not create the model file picker.";
        return result;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    dialog->SetTitle(simplifiedChinese ? L"选择要导入的模型" : L"Select a model to import");

    std::vector<std::string> supported = AssimpImporter::GetSupportedExtensions();
    for (auto &extension : supported)
    {
        extension = Lowercase(extension);
        if (!extension.empty() && extension.front() != '.')
            extension.insert(extension.begin(), '.');
    }
    std::sort(supported.begin(), supported.end());
    supported.erase(std::unique(supported.begin(), supported.end()), supported.end());

    const auto buildPattern = [&supported](const std::vector<std::string> &extensions) {
        std::wstring pattern;
        for (const auto &requested : extensions)
            if (std::find(supported.begin(), supported.end(), requested) != supported.end())
            {
                if (!pattern.empty())
                    pattern += L';';
                const std::wstring wide(requested.begin(), requested.end());
                pattern += L'*' + wide;
            }
        return pattern;
    };
    const std::wstring allSupported = buildPattern(supported);
    std::vector<std::wstring> names;
    std::vector<std::wstring> patterns;
    const auto addFilter = [&names, &patterns](std::wstring name, std::wstring pattern) {
        if (!pattern.empty())
        {
            names.push_back(std::move(name));
            patterns.push_back(std::move(pattern));
        }
    };
    addFilter(simplifiedChinese ? L"所有支持的模型" : L"All supported models", allSupported);
    addFilter(L"FBX", buildPattern({".fbx"}));
    addFilter(L"glTF 2.0", buildPattern({".gltf", ".glb"}));
    addFilter(L"Wavefront OBJ", buildPattern({".obj"}));
    addFilter(simplifiedChinese ? L"所有文件" : L"All files", L"*.*");
    std::vector<COMDLG_FILTERSPEC> filters;
    filters.reserve(names.size());
    for (std::size_t index = 0; index < names.size(); ++index)
        filters.push_back({names[index].c_str(), patterns[index].c_str()});
    if (!filters.empty())
        dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());

    if (!initialDirectory.empty() && std::filesystem::is_directory(initialDirectory))
    {
        IShellItem *folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialDirectory.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder)
        {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    status = dialog->Show(owner);
    if (SUCCEEDED(status))
    {
        IShellItem *item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item)
        {
            PWSTR selected = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected)) && selected)
            {
                result.path = std::filesystem::path(selected).lexically_normal();
                CoTaskMemFree(selected);
            }
            item->Release();
        }
        if (!result.path)
            result.error = "Windows returned no model file from the picker.";
    }
    else if (status != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        result.error = "The Windows model file picker failed.";

    dialog->Release();
    if (uninitialize)
        CoUninitialize();
    return result;
}
#endif
#endif

} // namespace

void EditorApp::LocateAsset(const std::string &path)
{
    const auto *resources = m_engine.GetContext().resourceManager;
    if (!resources || path.empty() || path.rfind("builtin:", 0) == 0) return;
    const auto &registry = resources->GetAssetRegistry();
    auto asset = registry.Find(path);
    if (!asset) asset = registry.Find(resources->ResolveAssetPath(path));
    if (!asset) asset = registry.FindById(path);
    const auto root = NormalizedAssetPath(m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory));
    if (!asset || !AssetPathWithin(std::filesystem::u8path(asset->path), root))
    {
        Notify(std::string(Localize("Unable to locate asset: ", "无法定位资产：")) + path, FeedbackKind::Warning);
        return;
    }
    // Highlight in the browser without transferring ownership away from the current inspector.
    m_assetFolder = NormalizedAssetPath(std::filesystem::u8path(asset->path).parent_path());
    m_selectedAssets = {asset->path};
    m_assetSearch.fill('\0');
    m_assetTypeFilters.clear();
    m_showAssets = true;
    m_sceneMaximized = false;
    m_assetRevealPending = true;
    m_focusAssets = true;
}

void EditorApp::PollAssetImport()
{
#ifdef TINYENGINE_HAS_ASSIMP
    if (m_activeImportTask == 0) return;
    const auto task = m_importQueue.Get(m_activeImportTask);
    if (task.status == AssetImportStatus::Queued || task.status == AssetImportStatus::Running) return;
    if (task.status == AssetImportStatus::Completed)
    {
        if (auto *resources = m_engine.GetContext().resourceManager)
            resources->GetAssetRegistry().Scan(m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory));
        if (!task.result.prefabPath.empty())
        {
            m_selectedAsset = task.result.prefabPath.lexically_normal().generic_u8string();
            m_selectedAssets = {m_selectedAsset};
            m_assetFolder = task.result.prefabPath.parent_path().lexically_normal();
            m_assetRevealPending = true;
        }
        Notify(Localize(
            "Model import completed. The Prefab is selected; double-click it, drag it to Scene View, or use Place in Scene.",
            "模型导入完成，已选中 Prefab；可双击、拖到场景视图，或使用“放入场景”。"), FeedbackKind::Success);
    }
    else if (task.status == AssetImportStatus::Cancelled)
        Notify(Localize("Background model import cancelled", "后台模型导入已取消"));
    else if (task.status == AssetImportStatus::Failed)
        Notify(std::string(Localize("Background model import failed: ", "后台模型导入失败：")) + task.result.error, FeedbackKind::Error);
    else
        Notify(Localize("The import task is no longer available.", "导入任务已不可用。"), FeedbackKind::Warning);
    for (const auto &warning : task.result.warnings)
        Notify(std::string(Localize("Import warning: ", "导入警告：")) + warning, FeedbackKind::Warning);
    m_importQueue.ClearFinished();
    m_activeImportTask = 0;
#endif
}

bool EditorApp::MoveEditorAsset(const std::string &source, const std::filesystem::path &destination, std::string &error)
{
    error.clear();
    auto *resources = m_engine.GetContext().resourceManager;
    auto *world = m_engine.GetContext().world;
    if (!resources || m_engine.IsPlaying() || m_packageFuture.valid())
    {
        error = Localize("Stop play or packaging before renaming assets.", "停止运行或打包后才能重命名资产。");
        return false;
    }
    auto &registry = resources->GetAssetRegistry();
    auto asset = registry.Find(source);
    if (!asset) asset = registry.Find(resources->ResolveAssetPath(source));
    if (!asset)
    {
        error = Localize("The source asset is not registered.", "源资产未登记。");
        return false;
    }
    const auto sourcePath = std::filesystem::u8path(asset->path);
    const auto &settings = m_engine.GetProjectSettings();
    const auto assetRoot = NormalizedAssetPath(settings.Resolve(settings.assetDirectory));
    const auto projectRoot = settings.projectFile.empty() ? assetRoot.parent_path() : settings.projectFile.parent_path();
    const auto target = settings.Resolve(destination).lexically_normal();
    if (destination.empty() || target.filename().empty() || !AssetPathWithin(sourcePath, assetRoot) || !AssetPathWithin(target, assetRoot))
    {
        error = Localize("Choose a destination inside the project asset folder.", "请选择项目资产目录内的目标路径。");
        return false;
    }
    const auto sameSource = [&](const std::string &path) {
        return !path.empty() && AssetPathKey(std::filesystem::u8path(path)) == AssetPathKey(sourcePath);
    };
    const auto remap = [&](std::string &path) {
        return AssetMaintenance::RewriteReference(path, sourcePath, target, assetRoot, projectRoot);
    };
    const auto rewriteSnapshot = [&](std::string &snapshot) {
        return AssetMaintenance::RewriteReferences(snapshot, sourcePath, target, assetRoot, projectRoot);
    };
    bool shaderAffected = sameSource(m_openShaderAsset);
    if (!m_openShaderAsset.empty())
    {
        const auto shaderOwner = std::filesystem::u8path(m_openShaderAsset);
        const auto shaderTarget = shaderAffected ? target : shaderOwner;
        auto draft = m_shaderSource.empty() ? std::string{} : std::string(m_shaderSource.data());
        auto baseline = m_savedShaderSource;
        auto disk = PlatformFile::ReadAllText(shaderOwner);
        shaderAffected = AssetMaintenance::RewriteShaderIncludes(draft, shaderOwner, shaderTarget, sourcePath, target) || shaderAffected;
        shaderAffected = AssetMaintenance::RewriteShaderIncludes(baseline, shaderOwner, shaderTarget, sourcePath, target) || shaderAffected;
        shaderAffected = AssetMaintenance::RewriteShaderIncludes(disk, shaderOwner, shaderTarget, sourcePath, target) || shaderAffected;
    }
    if (shaderAffected && IsShaderDirty())
    {
        error = Localize("Save or discard the affected shader draft before renaming this asset.", "请先保存或放弃受影响的着色器草稿，再重命名此资产。");
        return false;
    }
    const auto materialDraftAffected = [&](const std::string &path, std::string text, bool graph) {
        return sameSource(path) || (!path.empty() && RewriteMaterialReferences(text, graph, std::filesystem::u8path(path),
            std::filesystem::u8path(path), assetRoot, sourcePath.generic_u8string(), target.generic_u8string()));
    };
    auto draftGraph = m_materialGraphEditor.GetDraft().graph;
    draftGraph.GetParameters() = m_materialGraphEditor.GetDraft().parameters;
    if ((m_materialAssetEditor.IsDirty() && materialDraftAffected(m_materialAssetEditor.GetPath(), SerializeMaterialAsset(m_materialAssetEditor.GetDraft()), false)) ||
        (m_materialGraphEditor.IsDirty() && materialDraftAffected(m_materialGraphEditor.GetPath(), draftGraph.Serialize(), true)))
    {
        error = Localize("Save or discard the affected material draft before renaming this asset.", "请先保存或放弃受影响的材质草稿，再重命名此资产。");
        return false;
    }

    // Prepare every editor mutation first; disk rollback must leave all of this state untouched.
    auto updatedSettings = settings;
    bool environmentChanged = false;
    for (auto *path : {&updatedSettings.startupScene, &updatedSettings.inputBindings, &updatedSettings.environmentTexture})
    {
        auto reference = path->generic_u8string();
        if (remap(reference))
        {
            if (path == &updatedSettings.environmentTexture) environmentChanged = true;
            *path = std::filesystem::u8path(reference);
        }
    }
    auto documents = m_sceneDocuments;
    auto undo = m_undo, redo = m_redo;
    for (auto &snapshot : undo) rewriteSnapshot(snapshot);
    for (auto &snapshot : redo) rewriteSnapshot(snapshot);
    for (auto &document : documents)
    {
        const bool changed = rewriteSnapshot(document.snapshot);
        document.dirty = document.dirty || changed;
        if (!document.path.empty() && AssetPathKey(document.path) == AssetPathKey(sourcePath)) document.path = target;
        for (auto &snapshot : document.undo) rewriteSnapshot(snapshot);
        for (auto &snapshot : document.redo) rewriteSnapshot(snapshot);
    }
    std::vector<std::pair<std::string *, std::string>> liveReferences;
    if (world)
        for (const auto entity : world->GetEntityManager().GetAliveEntities())
            for (const auto &component : ReflectionRegistry::Get().GetComponents())
                if (component.has(*world, entity))
                    for (const auto &property : component.properties)
                        if (property.type == ReflectedPropertyType::String && !property.assetType.empty() && property.access)
                            if (auto *reference = static_cast<std::string *>(property.access(*world, entity)))
                            {
                                auto updated = *reference;
                                if (remap(updated)) liveReferences.emplace_back(reference, std::move(updated));
                            }
    if (world && m_activeSceneDocument < documents.size())
    {
        auto &active = documents[m_activeSceneDocument];
        active.snapshot = SceneSerializer::Serialize(*world);
        rewriteSnapshot(active.snapshot);
        active.undo = undo; active.redo = redo;
        active.dirty = m_sceneDirty || !liveReferences.empty();
    }
    const auto before = registry.GetAssets();
    if (!AssetMaintenance::MoveAsset(registry, sourcePath, target, &error, projectRoot, settings.projectFile)) return false;

    const_cast<ProjectSettings &>(m_engine.GetProjectSettings()) = std::move(updatedSettings);
    for (auto &reference : liveReferences) *reference.first = std::move(reference.second);
    m_sceneDocuments = std::move(documents);
    m_undo = std::move(undo); m_redo = std::move(redo);
    m_sceneDirty = m_sceneDirty || !liveReferences.empty();
    for (auto *path : {&m_selectedAsset, &m_editingAsset, &m_previewAsset, &m_assetReferenceRoot, &m_openShaderAsset, &m_pendingShaderPath,
                       &m_openAnimationAsset, &m_openEffectAsset, &m_openBehaviorAsset, &m_scenePreviewAsset}) remap(*path);
    m_assetReferencesDirty = true;
    if (shaderAffected)
    {
        const bool hidden = m_shaderEditorHidden;
        // Force a clean reread even for an incoming include rewrite at the same owner path.
        m_shaderEditorHidden = true;
        OpenShaderDocument(m_openShaderAsset);
        m_shaderEditorHidden = hidden;
    }
    for (auto &path : m_selectedAssets) remap(path);
    for (auto &draft : m_inspectorAssetDrafts) remap(draft.second);
    for (auto &sample : m_editBlendTree.samples) remap(sample.clipPath);
    for (auto &state : m_editAnimationGraph.states) remap(state.motionPath);
    remap(m_editEffect.texturePath);
    if (environmentChanged)
        if (auto *renderer = m_engine.GetContext().renderer)
            renderer->SetEnvironmentPath(settings.Resolve(settings.environmentTexture).generic_u8string());

    // Refresh cached dependents whose persisted references changed. New World paths load the moved resource at its new key.
    for (const auto &previous : before)
    {
        if (previous.path == asset->path) continue;
        const auto current = registry.Find(previous.path);
        if (!current || (current->modified == previous.modified && current->byteSize == previous.byteSize && current->dependencies == previous.dependencies)) continue;
        resources->Reload(current->path);
        if (auto *renderer = m_engine.GetContext().renderer; renderer && (current->type == "Material" || current->type == "MaterialGraph"))
            renderer->GetMaterialSystem().Reload(current->path);
    }
    resources->GarbageCollect();
    if (!m_materialGraphEditor.IsDirty() && !m_materialGraphEditor.GetPath().empty())
    {
        auto path = m_materialGraphEditor.GetPath(); const bool moved = remap(path);
        MaterialDefinitionResource definition;
        if (definition.Load(path))
        {
            if (moved) m_materialGraphEditor.Open(path, definition.GetData());
            else m_materialGraphEditor.RefreshFromDisk(definition.GetData());
            m_openMaterialGraphAsset = path;
        }
    }
    if (!m_materialAssetEditor.IsDirty() && !m_materialAssetEditor.GetPath().empty())
    {
        auto path = m_materialAssetEditor.GetPath(); const bool moved = remap(path);
        MaterialResource material;
        if (material.LoadWithResolver(path, [&](const auto &reference, const auto &owner) { return resources->ResolveAssetPath(reference, owner); }))
        {
            if (moved) m_materialAssetEditor.Open(path, material.GetMaterialData(), {});
            else m_materialAssetEditor.RefreshFromDisk(material.GetMaterialData());
            m_openMaterialAsset = path;
        }
    }
    return true;
}

void EditorApp::DrawAssetCreationMenu(const std::filesystem::path &directory)
{
    const auto assetRoot = NormalizedAssetPath(m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory));
    std::error_code error;
    const bool canCreate = !m_engine.IsPlaying() && !m_packageFuture.valid() && AssetPathWithin(directory, assetRoot) &&
                           std::filesystem::is_directory(directory, error);
    ImGui::BeginDisabled(!canCreate);
    if (ImGui::MenuItem(Localize("New Folder", "新建文件夹")))
    {
        m_assetCreateParent = directory;
        m_assetFolderError.clear();
        std::string name = "NewFolder";
        for (int suffix = 1; std::filesystem::exists(directory / name, error); ++suffix)
            name = "NewFolder" + std::to_string(suffix);
        std::snprintf(m_assetFolderName.data(), m_assetFolderName.size(), "%s", name.c_str());
        m_assetFolderCreateRequested = true;
    }
    ImGui::Separator();
    const auto publishCreated = [&](const std::filesystem::path &path) {
        m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(assetRoot);
        SelectAsset(path.generic_u8string(), false);
        m_assetFolder = directory;
        m_assetSearch.fill(0);
        m_assetTypeFilters.clear();
        m_assetRevealPending = true;
    };
    const auto createUniquePath = [&directory](const std::string &stem, const std::string &extension) {
        const auto &root = directory;
        for (int suffix = 0;; ++suffix)
        {
            const auto name = stem + (suffix == 0 ? std::string{} : std::to_string(suffix)) + extension;
            const auto path = root / name;
            if (!std::filesystem::exists(path))
                return path;
        }
    };
    const auto ensureTemplateClip = [this, &createUniquePath]() -> std::optional<std::string> {
        auto assets = m_engine.GetContext().resourceManager->GetAssetRegistry().GetAssets();
        std::sort(assets.begin(), assets.end(), [](const AssetInfo &left, const AssetInfo &right) { return left.path < right.path; });
        std::filesystem::path clipPath;
        for (const auto &asset : assets)
            if (asset.extension == ".skanim" && PlatformFile::Exists(std::filesystem::u8path(asset.path)))
            {
                clipPath = std::filesystem::u8path(asset.path);
                break;
            }
        if (clipPath.empty())
        {
            clipPath = createUniquePath("NewAnimationClip", ".skanim");
            SkeletalClip clip;
            clip.name = clipPath.stem().u8string();
            clip.duration = 1.0f;
            clip.keyframes = {{0.0f, {BonePose{}}}, {1.0f, {BonePose{}}}};
            if (!SaveSkeletalClipAsset(clip, clipPath))
                return std::nullopt;
        }
        std::error_code error;
        const auto relative = std::filesystem::relative(clipPath, m_engine.GetProjectSettings().projectFile.parent_path(), error);
        return error || relative.empty() ? clipPath.lexically_normal().generic_u8string() : relative.lexically_normal().generic_u8string();
    };
    if (ImGui::MenuItem(Localize("Material", "材质")))
    {
        const auto path = createUniquePath("NewMaterial", ".matgraph");
        MaterialGraph graph;
        graph.SetFormatVersion(2);
        graph.AddNode(MaterialNodeType::SurfaceOutput, "Surface", {420, 60});
        MaterialParameterDecl color;
        color.name = "BaseColor";
        color.displayName = "Base Color";
        color.type = MaterialParameterType::Vec3;
        color.defaultValue = Vec3{.8f, .8f, .8f};
        graph.GetParameters().push_back(color);
        const auto id = graph.AddNode(MaterialNodeType::VectorParameter, "Base Color", {40, 60});
        graph.GetNodes().back().parameterName = "BaseColor";
        graph.Connect(id, 1, 0);
        if (graph.Save(path))
            publishCreated(path);
        else
            Notify(Localize("Unable to save material.", "无法保存材质。"), FeedbackKind::Error);
    }
    if (ImGui::MenuItem(Localize("Material Instance", "材质实例")))
    {
        auto &registry = m_engine.GetContext().resourceManager->GetAssetRegistry();
        const auto parent = registry.Find(m_selectedAsset);
        const auto path = createUniquePath("NewMaterialInstance", ".mat");
        std::string error;
        const auto reference =
            parent && (parent->type == "Material" || parent->type == "MaterialGraph") ? parent->path : std::string("builtin:PBR");
        if (MaterialAssetEditor::CreateInstance(path.generic_u8string(), reference, error))
            publishCreated(path);
        else
            Notify(error, FeedbackKind::Error);
    }
    if (ImGui::MenuItem(Localize("+ Blend Tree", "+ 混合树")))
    {
        const auto clip = ensureTemplateClip();
        if (!clip)
            Notify(Localize("Unable to create the starter animation clip.", "无法创建初始动画片段。"), FeedbackKind::Error);
        else
        {
            BlendTree1D tree;
            tree.samples = {{0.0f, *clip}};
            const auto path = createUniquePath("NewBlendTree", ".blendtree");
            if (SaveBlendTreeAsset(tree, path))
            {
                auto &registry = m_engine.GetContext().resourceManager->GetAssetRegistry();
                publishCreated(path);
                registry.SetDependencies(path.generic_u8string(), {*clip});
            }
        }
    }
    if (ImGui::MenuItem(Localize("+ Animation Graph", "+ 动画图")))
    {
        const auto clip = ensureTemplateClip();
        if (!clip)
            Notify(Localize("Unable to create the starter animation clip.", "无法创建初始动画片段。"), FeedbackKind::Error);
        else
        {
            AnimationGraph graph;
            graph.entryState = "Idle";
            graph.parameters = {{"Speed", 0.0f}};
            graph.states = {{"Idle", *clip}};
            const auto path = createUniquePath("NewAnimationGraph", ".animgraph");
            if (SaveAnimationGraphAsset(graph, path))
            {
                auto &registry = m_engine.GetContext().resourceManager->GetAssetRegistry();
                publishCreated(path);
                registry.SetDependencies(path.generic_u8string(), {*clip});
            }
        }
    }
    if (ImGui::MenuItem(Localize("+ VFX", "+ 特效")))
    {
        const auto path = createUniquePath("NewEffect", ".vfx");
        if (SaveParticleEffectAsset({}, path))
        {
            publishCreated(path);
        }
    }
    if (ImGui::MenuItem(Localize("+ Behavior Tree", "+ 行为树")))
    {
        BehaviorTreeGraph graph;
        graph.root = 1;
        graph.nodes = {{1, "Sequence", "Root", {20.0f, 20.0f}, {2}}, {2, "Action", "Action", {220.0f, 20.0f}, {}}};
        const auto path = createUniquePath("NewBehavior", ".btgraph");
        if (SaveBehaviorTreeGraph(graph, path))
        {
            publishCreated(path);
        }
    }
    ImGui::EndDisabled();
}

void EditorApp::DrawAssetFolderDialog()
{
    if (m_assetFolderCreateRequested)
    {
        m_assetFolderCreateRequested = false;
        ImGui::OpenPopup(Localize("New Folder###NewAssetFolder", "新建文件夹###NewAssetFolder"));
    }
    if (!ImGui::BeginPopupModal(Localize("New Folder###NewAssetFolder", "新建文件夹###NewAssetFolder"), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextWrapped("%s", m_assetCreateParent.generic_u8string().c_str());
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(Scaled(300.0f));
    const bool enter = ImGui::InputText(Localize("Name", "名称"), m_assetFolderName.data(), m_assetFolderName.size(),
                                        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
    if (!m_assetFolderError.empty())
        ImGui::TextColored({1, .45f, .3f, 1}, "%s", m_assetFolderError.c_str());
    const bool canWrite = !m_engine.IsPlaying() && !m_packageFuture.valid();
    ImGui::BeginDisabled(!canWrite);
    if ((enter && canWrite) || ImGui::Button(Localize("Create", "创建")))
    {
        const std::string name = m_assetFolderName.data();
        const auto root = m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory);
        const auto target = m_assetCreateParent / std::filesystem::u8path(name);
        std::error_code error;
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:*?\"<>|") != std::string::npos || name.back() == '.' ||
            name.back() == ' ' || !AssetPathWithin(target, root))
            m_assetFolderError = Localize("Enter a valid folder name.", "请输入有效的文件夹名称。");
        else if (!std::filesystem::is_directory(m_assetCreateParent, error))
            m_assetFolderError = Localize("The parent folder is no longer available.", "父文件夹已不存在。");
        else if (std::filesystem::exists(target, error))
            m_assetFolderError = Localize("An item with this name already exists.", "同名文件或文件夹已存在。");
        else if (!std::filesystem::create_directory(target, error))
            m_assetFolderError = std::string(Localize("Could not create folder: ", "无法创建文件夹：")) + error.message();
        else
        {
            m_assetFolder = NormalizedAssetPath(target);
            m_assetSearch.fill(0);
            m_assetTypeFilters.clear();
            m_selectedAsset.clear();
            m_selectedAssets.clear();
            m_assetRevealPending = true;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(Localize("Cancel", "取消")))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::DrawAssets()
{
    m_assetsFocused = false;
    if (!m_showAssets)
        return;
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const auto layout = GetWorkspaceLayout(viewport->WorkSize.x, viewport->WorkSize.y);
    const float top = viewport->WorkPos.y + layout.upper + 3.0f;
    const float halfWidth = layout.assets - 3.0f;
    if (m_focusAssets) ImGui::SetNextWindowFocus();
    if (!BeginEditorPanel("Assets", "资产", m_showAssets, {viewport->WorkPos.x, top},
                          {halfWidth, viewport->WorkPos.y + viewport->WorkSize.y - top}))
    {
        ImGui::End();
        return;
    }
    m_focusAssets = false;
    m_assetsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const auto assetRoot = NormalizedAssetPath(m_engine.GetProjectSettings().Resolve(m_engine.GetProjectSettings().assetDirectory));
    std::error_code folderError;
    if (m_assetFolder.empty() || !AssetPathWithin(m_assetFolder, assetRoot) || !std::filesystem::is_directory(m_assetFolder, folderError)) m_assetFolder = assetRoot;
    m_assetFolder = NormalizedAssetPath(m_assetFolder);
    const bool canWriteAssets = !m_engine.IsPlaying() && !m_packageFuture.valid();
    LoadAssetTrashHistory();
    const float toolbarRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const auto continueToolbar = [&](const char *label) {
        const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= toolbarRight) ImGui::SameLine();
    };
    ImGui::BeginDisabled(!canWriteAssets);
    if (ImGui::Button(Localize("Create...", "新建资产..."))) ImGui::OpenPopup("CreateAssetMenu");
    if (ImGui::BeginPopup("CreateAssetMenu"))
    {
        DrawAssetCreationMenu(m_assetFolder);
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
#ifdef TINYENGINE_HAS_ASSIMP
    if (m_activeImportTask != 0)
    {
        const auto task = m_importQueue.Get(m_activeImportTask);
        ImGui::ProgressBar(task.progress, ImVec2(220.0f, 0.0f),
                           task.status == AssetImportStatus::Queued    ? Localize("Queued", "等待中")
                           : task.status == AssetImportStatus::Running ? Localize("Importing", "正在导入")
                                                                       : Localize("Finishing", "正在完成"));
        if ((task.status == AssetImportStatus::Queued || task.status == AssetImportStatus::Running) &&
            ImGui::Button(Localize("Cancel Import", "取消导入")))
            m_importQueue.Cancel(m_activeImportTask);
        ImGui::Separator();
    }
    ImGui::BeginDisabled(m_activeImportTask != 0 || !canWriteAssets);
    continueToolbar(Localize("Import Model...", "导入模型..."));
    if (ImGui::Button(Localize("Import Model...", "导入模型...")))
    {
#ifdef _WIN32
        SDL_SysWMinfo windowInfo{};
        SDL_VERSION(&windowInfo.version);
        HWND owner = nullptr;
        if (SDL_GetWindowWMInfo(static_cast<SDL_Window *>(m_device->GetNativeWindow()), &windowInfo) == SDL_TRUE)
            owner = windowInfo.info.win.window;
        const auto selected =
            OpenModelFileDialog(owner, m_lastImportDirectory, m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese);
        if (selected.path)
        {
            m_importSource = *selected.path;
            const std::string path = selected.path->generic_u8string();
            std::fill(m_importSourcePath.begin(), m_importSourcePath.end(), '\0');
            std::strncpy(m_importSourcePath.data(), path.c_str(), m_importSourcePath.size() - 1);
            m_lastImportDirectory = selected.path->parent_path();
            m_importValidationError = ValidateModelSource(*selected.path);
            SaveEditorSettings();
            ImGui::OpenPopup(Localize("Import Model###ImportModelPopup", "导入模型###ImportModelPopup"));
        }
        else if (!selected.error.empty())
            Notify(std::string(Localize("File picker failed: ", "文件选择器失败：")) + selected.error, FeedbackKind::Error);
#else
        m_importSource = std::filesystem::u8path(m_importSourcePath.data());
        m_importValidationError = ValidateModelSource(m_importSource);
        ImGui::OpenPopup(Localize("Import Model###ImportModelPopup", "导入模型###ImportModelPopup"));
#endif
    }
    continueToolbar(Localize("Reimport", "重新导入"));
    const auto selectedInfo = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                                      : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
    ImGui::BeginDisabled(!selectedInfo || selectedInfo->type != "Prefab" || selectedInfo->importSettings.count("source") == 0 ||
                         m_activeImportTask != 0);
    if (ImGui::Button(Localize("Reimport", "重新导入")))
    {
        const std::filesystem::path source = selectedInfo->importSettings.at("source");
        ModelImportOptions options;
        if (const auto scale = selectedInfo->importSettings.find("scale"); scale != selectedInfo->importSettings.end())
        {
            try
            {
                options.scale = std::stof(scale->second);
            }
            catch (...)
            {
            }
        }
        if (const auto flip = selectedInfo->importSettings.find("flipUvs"); flip != selectedInfo->importSettings.end())
            options.flipUvs = flip->second == "true";
        if (const auto compress = selectedInfo->importSettings.find("compressAnimations"); compress != selectedInfo->importSettings.end())
            options.compressAnimations = compress->second == "true";
        const auto readFloatSetting = [&selectedInfo](const char *name, float &value) {
            if (const auto setting = selectedInfo->importSettings.find(name); setting != selectedInfo->importSettings.end())
                try
                {
                    value = std::stof(setting->second);
                }
                catch (...)
                {
                }
        };
        readFloatSetting("animationPositionTolerance", options.animationPositionTolerance);
        readFloatSetting("animationRotationToleranceDegrees", options.animationRotationToleranceDegrees);
        readFloatSetting("animationScaleTolerance", options.animationScaleTolerance);
        const std::filesystem::path output = std::filesystem::path(selectedInfo->path).parent_path();
        const auto projectRoot = m_engine.GetProjectSettings().projectFile.empty()
                                     ? std::filesystem::current_path()
                                     : m_engine.GetProjectSettings().projectFile.parent_path();
        options.resourcePrefix = std::filesystem::relative(output, projectRoot);
        m_activeImportTask = m_importQueue.Submit(source, output, options);
        Notify(std::string(Localize("Model reimport queued: ", "模型重新导入已加入队列：")) + source.generic_string());
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal(Localize("Import Model###ImportModelPopup", "导入模型###ImportModelPopup"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SetNextItemWidth(520.0f);
#ifdef _WIN32
        ImGui::InputText(Localize("Source File", "源文件"), m_importSourcePath.data(), m_importSourcePath.size(),
                         ImGuiInputTextFlags_ReadOnly);
#else
        if (ImGui::InputText(Localize("Source File", "源文件"), m_importSourcePath.data(), m_importSourcePath.size()))
        {
            m_importSource = std::filesystem::u8path(m_importSourcePath.data());
            m_importValidationError = ValidateModelSource(m_importSource);
        }
#endif
#ifdef _WIN32
        ImGui::SameLine();
        if (ImGui::Button(Localize("Browse...", "浏览...")))
        {
            SDL_SysWMinfo windowInfo{};
            SDL_VERSION(&windowInfo.version);
            HWND owner = nullptr;
            if (SDL_GetWindowWMInfo(static_cast<SDL_Window *>(m_device->GetNativeWindow()), &windowInfo) == SDL_TRUE)
                owner = windowInfo.info.win.window;
            const auto selected =
                OpenModelFileDialog(owner, m_lastImportDirectory, m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese);
            if (selected.path)
            {
                m_importSource = *selected.path;
                const std::string path = selected.path->generic_u8string();
                std::fill(m_importSourcePath.begin(), m_importSourcePath.end(), '\0');
                std::strncpy(m_importSourcePath.data(), path.c_str(), m_importSourcePath.size() - 1);
                m_lastImportDirectory = selected.path->parent_path();
                m_importValidationError = ValidateModelSource(*selected.path);
                SaveEditorSettings();
            }
            else if (!selected.error.empty())
                m_importValidationError = selected.error;
        }
#else
        ImGui::SameLine();
        ImGui::TextDisabled("%s",
                            Localize("Native file picker is currently available on Windows.", "原生文件选择器目前仅在 Windows 上提供。"));
#endif
        if (!m_importValidationError.empty())
            ImGui::TextColored({1.0f, 0.35f, 0.28f, 1.0f}, "%s",
                               ModelSourceErrorText(m_importValidationError, m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese));
        else
            ImGui::TextColored({0.35f, 0.85f, 0.45f, 1.0f}, "%s", Localize("Ready to import", "文件有效，可以导入"));
        ImGui::DragFloat(Localize("Scale", "缩放"), &m_importScale, 0.01f, 0.0001f, 1000.0f);
        ImGui::Checkbox(Localize("Flip UVs", "翻转 UV"), &m_importFlipUvs);
        ImGui::Checkbox(Localize("Compress Animations", "压缩动画"), &m_importCompressAnimations);
        ImGui::BeginDisabled(!m_importCompressAnimations);
        ImGui::DragFloat(Localize("Position Error", "位置误差"), &m_importPositionTolerance, 0.0001f, 0.0f, 1.0f, "%.5f");
        ImGui::DragFloat(Localize("Rotation Error (Degrees)", "旋转误差（度）"), &m_importRotationTolerance, 0.01f, 0.0f, 45.0f, "%.3f");
        ImGui::DragFloat(Localize("Scale Error", "缩放误差"), &m_importScaleTolerance, 0.0001f, 0.0f, 1.0f, "%.5f");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_importValidationError.empty() || m_importSourcePath[0] == '\0');
        if (ImGui::Button(Localize("Import", "导入")))
        {
            const std::filesystem::path source = m_importSource;
            const std::string folderName = source.stem().empty() ? "Model" : source.stem().string();
            const auto relativeOutput = m_engine.GetProjectSettings().assetDirectory / "Imported" / folderName;
            const auto output = m_engine.GetProjectSettings().Resolve(relativeOutput);
            ModelImportOptions options;
            options.scale = m_importScale;
            options.flipUvs = m_importFlipUvs;
            options.compressAnimations = m_importCompressAnimations;
            options.animationPositionTolerance = m_importPositionTolerance;
            options.animationRotationToleranceDegrees = m_importRotationTolerance;
            options.animationScaleTolerance = m_importScaleTolerance;
            options.resourcePrefix = relativeOutput;
            m_activeImportTask = m_importQueue.Submit(source, output, options);
            Notify(std::string(Localize("Model import queued: ", "模型导入已加入队列：")) + source.generic_string());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(Localize("Cancel", "取消")))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
#endif
    auto assets = m_engine.GetContext().resourceManager->GetAssetRegistry().GetAssets();
    std::sort(assets.begin(), assets.end(), [](const AssetInfo &a, const AssetInfo &b) { return a.path < b.path; });
    static std::string moveSource;
    static std::array<char, 1024> moveDestination{};
    std::error_code pathError;
    if (m_assetFolder.empty() || !AssetPathWithin(m_assetFolder, assetRoot) ||
        !std::filesystem::is_directory(m_assetFolder, pathError)) m_assetFolder = assetRoot;
    m_assetFolder = NormalizedAssetPath(m_assetFolder);
    const bool wideBrowser = ImGui::GetContentRegionAvail().x >= Scaled(560.0f);
    const auto activeAsset = m_selectedAssets.size() == 1 ? m_selectedAssets.front()
                           : m_selectedAssets.empty() ? m_selectedAsset : std::string{};
    const bool canRename = canWriteAssets && !activeAsset.empty() &&
        std::any_of(assets.begin(), assets.end(), [&](const AssetInfo &asset) { return asset.path == activeAsset; });
    bool openMovePopup = false;
    bool openDetailsPopup = false;
    const auto requestMove = [&](const std::string &source) {
        if (!canWriteAssets) return;
        moveSource = source;
        moveDestination.fill('\0');
        std::snprintf(moveDestination.data(), moveDestination.size(), "%s", source.c_str());
        m_assetMoveError.clear();
        openMovePopup = true;
    };
    if (m_assetRenameRequested)
    {
        m_assetRenameRequested = false;
        if (canRename && m_assetsFocused && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) requestMove(activeAsset);
    }

    const auto toolbarButton = [&](const char *label, bool sameLine) {
        const float desired = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (sameLine && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + desired <= toolbarRight) ImGui::SameLine();
        return ImGui::Button(label, {std::min(desired, std::max(1.0f, ImGui::GetContentRegionAvail().x)), 0});
    };
    if (toolbarButton(Localize("Filters", "筛选"), true)) ImGui::OpenPopup("AssetFilters");
    if (toolbarButton(Localize("Display", "显示"), true)) ImGui::OpenPopup("AssetViewOptions");
    if (!wideBrowser && toolbarButton(Localize("Folders", "文件夹"), true)) ImGui::OpenPopup("AssetFolderTree");
    ImGui::BeginDisabled(!canRename);
    if (toolbarButton(Localize("Rename (F2)", "重命名 (F2)"), true)) requestMove(activeAsset);
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("AssetFilters"))
    {
        std::set<std::string> availableTypes = {"Material", "MaterialGraph", "Texture", "Mesh", "Prefab", "Scene", "Shader",
            "Animation", "Skeleton", "BlendTree", "AnimationGraph", "VFX", "BehaviorTree", "Audio", "Environment", "Input", "Font"};
        for (const auto &asset : assets) availableTypes.insert(asset.type);
        ImGui::TextDisabled("%s", Localize("Asset types (match any selected type)", "资产类型（匹配任一勾选类型）"));
        if (ImGui::Selectable(Localize("All Types", "全部类型"), m_assetTypeFilters.empty(), ImGuiSelectableFlags_DontClosePopups)) m_assetTypeFilters.clear();
        ImGui::Separator();
        for (const auto &type : availableTypes)
        {
            bool enabled = m_assetTypeFilters.count(type) != 0;
            if (ImGui::Checkbox(LocalizeReflected(type).c_str(), &enabled))
            {
                if (enabled) m_assetTypeFilters.insert(type); else m_assetTypeFilters.erase(type);
            }
        }
        if (ImGui::Button(Localize("Clear filters", "清除筛选"))) m_assetTypeFilters.clear();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("AssetViewOptions"))
    {
        ImGui::TextDisabled("%s", Localize("View mode", "视图模式"));
        if (ImGui::RadioButton(Localize("Grid", "网格"), m_assetViewMode == AssetViewMode::Grid))
        {
            m_assetViewMode = AssetViewMode::Grid; SaveEditorSettings();
        }
        if (ImGui::RadioButton(Localize("List", "列表"), m_assetViewMode == AssetViewMode::List))
        {
            m_assetViewMode = AssetViewMode::List; SaveEditorSettings();
        }
        if (m_assetViewMode == AssetViewMode::Grid)
        {
            ImGui::SetNextItemWidth(Scaled(190.0f));
            if (ImGui::SliderFloat("##ThumbnailSize", &m_assetThumbnailSize, 64.0f, 160.0f, Localize("%.0f px", "%.0f 像素"))) SaveEditorSettings();
        }
        ImGui::EndPopup();
    }

    // Wide panels reserve two compact toolbar rows, leaving room for actual asset rows.
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const float clearWidth = ImGui::GetFrameHeight();
    const float searchGroupWidth = wideBrowser ? std::clamp(rowWidth * .28f, Scaled(200.0f), Scaled(400.0f)) : rowWidth;
    ImGui::SetNextItemWidth(std::max(1.0f, searchGroupWidth - clearWidth - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##AssetSearch", Localize("Search assets...", "搜索资产..."), m_assetSearch.data(), m_assetSearch.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(m_assetSearch[0] == '\0' && m_assetTypeFilters.empty());
    if (ImGui::Button("X##ClearAssetFilters", {clearWidth, 0}))
    {
        m_assetSearch.fill('\0');
        m_assetTypeFilters.clear();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", Localize("Clear search and type filter", "清除搜索和类型筛选"));

    // Breadcrumbs wrap without moving when only the asset highlight changes.
    if (wideBrowser) ImGui::SameLine();
    if (ImGui::SmallButton(Localize("Assets", "资产"))) m_assetFolder = assetRoot;
    auto breadcrumb = assetRoot;
    for (const auto &part : m_assetFolder.lexically_relative(assetRoot))
    {
        if (part == "." || part.empty()) continue;
        breadcrumb /= part;
        const auto name = part.u8string();
        const float width = std::min({ImGui::CalcTextSize(name.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f,
                                      Scaled(150.0f), std::max(1.0f, ImGui::GetContentRegionAvail().x)});
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= toolbarRight) ImGui::SameLine();
        ImGui::PushID(breadcrumb.generic_u8string().c_str());
        const auto clipped = FitAssetText(name, width - ImGui::GetStyle().FramePadding.x * 2.0f);
        if (ImGui::SmallButton(clipped.c_str())) m_assetFolder = breadcrumb;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", breadcrumb.generic_u8string().c_str());
        ImGui::PopID();
    }
    if (!m_assetTypeFilters.empty())
    {
        std::string remove;
        ImGui::TextDisabled("%s", Localize("Active filters:", "已选筛选："));
        for (const auto &type : m_assetTypeFilters)
        {
            const auto label = LocalizeReflected(type) + "  x###RemoveFilter";
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(label.c_str(), nullptr, true).x +
                ImGui::GetStyle().FramePadding.x * 2 <= toolbarRight) ImGui::SameLine();
            ImGui::PushID(type.c_str());
            if (ImGui::SmallButton(label.c_str())) remove = type;
            ImGui::PopID();
        }
        if (!remove.empty()) m_assetTypeFilters.erase(remove);
    }
    const auto folderActions = [&](const std::filesystem::path &folder) {
        if (ImGui::BeginPopupContextItem("FolderContext"))
        {
            if (ImGui::MenuItem(Localize("Open Folder", "打开文件夹"))) m_assetFolder = folder;
            ImGui::Separator();
            DrawAssetCreationMenu(folder);
            ImGui::EndPopup();
        }
    };
    const auto folderKey = ImGui::GetID(m_assetFolder.generic_u8string().c_str());
    auto *treeState = ImGui::GetStateStorage();
    const auto treeStateKey = ImGui::GetID("AssetTreeCurrentFolder");
    const bool expandFolder = m_assetRevealPending || static_cast<ImGuiID>(treeState->GetInt(treeStateKey)) != folderKey;
    treeState->SetInt(treeStateKey, static_cast<int>(folderKey));
    std::function<void(const std::filesystem::path &)> drawFolderTree;
    drawFolderTree = [&](const std::filesystem::path &folder) {
        std::vector<std::filesystem::path> children;
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(folder, std::filesystem::directory_options::skip_permission_denied, error), end;
             !error && iterator != end; iterator.increment(error))
        {
            std::error_code entryError;
            if (iterator->is_directory(entryError) && !iterator->is_symlink(entryError)) children.push_back(iterator->path());
        }
        std::sort(children.begin(), children.end());
        const bool root = AssetPathKey(folder) == AssetPathKey(assetRoot);
        const bool selected = AssetPathKey(folder) == AssetPathKey(m_assetFolder);
        ImGui::PushID(folder.generic_u8string().c_str());
        auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (root) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;
        if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (expandFolder && AssetPathWithin(m_assetFolder, folder)) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const auto name = root ? std::string(Localize("Assets", "资产")) : folder.filename().u8string();
        const auto clipped = FitAssetText(name, ImGui::GetContentRegionAvail().x - ImGui::GetTreeNodeToLabelSpacing());
        const bool open = ImGui::TreeNodeEx("##Folder", flags, "%s", clipped.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_assetFolder = folder;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", folder.generic_u8string().c_str());
        folderActions(folder);
        if (selected && m_assetRevealPending) ImGui::SetScrollHereY(.5f);
        if (open && !children.empty())
        {
            for (const auto &child : children) drawFolderTree(child);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    if (!wideBrowser)
    {
        ImGui::SetNextWindowSize({std::min(Scaled(280.0f), viewport->WorkSize.x * .8f), std::min(Scaled(340.0f), viewport->WorkSize.y * .7f)}, ImGuiCond_Appearing);
        if (ImGui::BeginPopup("AssetFolderTree")) { drawFolderTree(assetRoot); ImGui::EndPopup(); }
    }

    const auto search = Lowercase(m_assetSearch.data());
    const auto regenerateThumbnails = [&](const AssetInfo &fallback) {
        if (m_selectedAssets.size() <= 1 || std::find(m_selectedAssets.begin(), m_selectedAssets.end(), fallback.path) == m_selectedAssets.end())
        { RequestThumbnail(fallback, true); return; }
        for (const auto &asset : assets)
            if (std::find(m_selectedAssets.begin(), m_selectedAssets.end(), asset.path) != m_selectedAssets.end()) RequestThumbnail(asset, true);
    };
    const auto assetActions = [&](const AssetInfo &asset) {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            if (std::find(m_selectedAssets.begin(), m_selectedAssets.end(), asset.path) == m_selectedAssets.end()) SelectAsset(asset.path, false);
            else m_selectedAsset = asset.path;
        }
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("TINY_ASSET_PATH", asset.path.c_str(), asset.path.size() + 1);
            ImGui::TextUnformatted(std::filesystem::u8path(asset.path).filename().u8string().c_str());
            ImGui::TextDisabled("%s", Localize("Drop into Scene View or a compatible reference", "拖到场景视图或匹配类型的引用字段"));
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("AssetContext"))
        {
            if (ImGui::MenuItem(Localize("Details / Preview", "详情 / 预览"))) openDetailsPopup = true;
            if (ImGui::MenuItem(Localize("Reference Viewer", "依赖图"))) OpenAssetReferences(asset.path);
            ImGui::Separator();
            if (asset.type == "Material" || asset.type == "MaterialGraph")
            {
                if (ImGui::MenuItem(Localize("Apply to Selected Meshes", "应用到选中网格"), nullptr, false, CanApplyMaterialToSelection()))
                    ApplyMaterialToMeshes(asset.path, GetInspectorTargets());
                if (ImGui::MenuItem(Localize("Create Material Instance", "创建材质实例"), nullptr, false, canWriteAssets))
                {
                    const auto directory = std::filesystem::u8path(asset.path).parent_path();
                    auto path = directory / "NewMaterialInstance.mat";
                    for (int suffix = 1; std::filesystem::exists(path); ++suffix) path = directory / ("NewMaterialInstance" + std::to_string(suffix) + ".mat");
                    std::string error;
                    if (MaterialAssetEditor::CreateInstance(path.generic_u8string(), asset.path, error))
                    { m_engine.GetContext().resourceManager->GetAssetRegistry().Scan(assetRoot); SelectAsset(path.generic_u8string(), false); }
                    else Notify(error, FeedbackKind::Error);
                }
            }
            if ((asset.type == "Prefab" || asset.type == "Mesh") &&
                ImGui::MenuItem(Localize("Place in Scene", "放入场景"), nullptr, false, canWriteAssets)) PlaceAssetInScene(asset);
            if (ImGui::MenuItem(Localize("Move / Rename...", "移动 / 重命名..."), "F2", false, canWriteAssets)) requestMove(asset.path);
            if (ImGui::MenuItem(Localize("Delete...", "删除..."), "Delete", false, canWriteAssets)) RequestDeleteAssets();
            const auto label = m_selectedAssets.size() > 1 ? std::string(Localize("Regenerate Selected Thumbnails (", "重新生成所选缩略图（")) +
                std::to_string(m_selectedAssets.size()) + Localize(")", "）") : std::string(Localize("Regenerate Thumbnail", "重新生成缩略图"));
            if (ImGui::MenuItem(label.c_str())) regenerateThumbnails(asset);
            ImGui::EndPopup();
        }
        if (m_assetRevealPending && m_selectedAssets.size() == 1 && m_selectedAssets.front() == asset.path)
        { ImGui::SetScrollHereY(.5f); m_assetRevealPending = false; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", asset.path.c_str(), LocalizeReflected(asset.type).c_str());
    };
    std::vector<const AssetInfo *> visibleAssets;
    std::set<std::filesystem::path> childFolders;
    if (search.empty())
    {
        pathError.clear();
        for (std::filesystem::directory_iterator iterator(m_assetFolder, std::filesystem::directory_options::skip_permission_denied, pathError), end;
             !pathError && iterator != end; iterator.increment(pathError))
        {
            std::error_code error;
            if (iterator->is_directory(error) && !iterator->is_symlink(error)) childFolders.insert(iterator->path());
        }
    }
    for (const auto &asset : assets)
    {
        const auto path = NormalizedAssetPath(std::filesystem::u8path(asset.path));
        if (!AssetPathWithin(path, assetRoot) || (!m_assetTypeFilters.empty() && m_assetTypeFilters.count(asset.type) == 0)) continue;
        if (search.empty()) { if (AssetPathKey(path.parent_path()) != AssetPathKey(m_assetFolder)) continue; }
        else if (Lowercase(path.filename().u8string() + " " + asset.type + " " + asset.path).find(search) == std::string::npos) continue;
        visibleAssets.push_back(&asset);
    }
    ImGui::Separator();
    const float browserHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    if (wideBrowser)
    {
        ImGui::BeginChild("AssetFolderTree", {std::clamp(ImGui::GetContentRegionAvail().x * .24f, Scaled(120.0f), Scaled(220.0f)), browserHeight}, ImGuiChildFlags_Borders);
        drawFolderTree(assetRoot);
        ImGui::EndChild();
        ImGui::SameLine();
    }
    // Keep the items child last and directly under Assets for the existing click/drag harness.
    ImGui::BeginChild("AssetBrowserItems", {0.0f, browserHeight}, ImGuiChildFlags_Borders);
    const float available = ImGui::GetContentRegionAvail().x;
    const float imageSize = std::max(1.0f, std::min(Scaled(m_assetThumbnailSize), available));
    const bool list = m_assetViewMode == AssetViewMode::List || browserHeight < imageSize + ImGui::GetTextLineHeight() + Scaled(14.0f) || available < Scaled(64.0f);
    if (list)
    {
        for (const auto &folder : childFolders)
        {
            ImGui::PushID(folder.generic_u8string().c_str());
            const auto position = ImGui::GetCursorScreenPos();
            const float iconSize = ImGui::GetTextLineHeight();
            if (ImGui::Selectable("##FolderRow", false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) m_assetFolder = folder;
            DrawAssetIcon(ImGui::GetWindowDrawList(), position, iconSize, "Folder");
            const auto label = FitAssetText(folder.filename().u8string(), available - iconSize - Scaled(6.0f));
            ImGui::GetWindowDrawList()->AddText({position.x + iconSize + Scaled(6.0f), position.y}, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", folder.generic_u8string().c_str(), Localize("Double click to open folder", "双击打开文件夹"));
            folderActions(folder);
            ImGui::PopID();
        }
        const int columns = available >= Scaled(410.0f) ? 3 : available >= Scaled(260.0f) ? 2 : 1;
        if (ImGui::BeginTable("AssetList", columns, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn(Localize("Name", "名称"), ImGuiTableColumnFlags_WidthStretch);
            if (columns > 1) ImGui::TableSetupColumn(Localize("Type", "类型"), ImGuiTableColumnFlags_WidthFixed, Scaled(100.0f));
            if (columns > 2) ImGui::TableSetupColumn(Localize("Size", "大小"), ImGuiTableColumnFlags_WidthFixed, Scaled(76.0f));
            ImGui::TableHeadersRow();
            for (const auto *asset : visibleAssets)
            {
                ImGui::PushID(asset->id.c_str());
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                const auto position = ImGui::GetCursorScreenPos();
                const float iconSize = ImGui::GetTextLineHeight();
                const auto name = FitAssetText(std::filesystem::u8path(asset->path).filename().u8string(), ImGui::GetContentRegionAvail().x - iconSize - Scaled(6.0f));
                const bool selected = std::find(m_selectedAssets.begin(), m_selectedAssets.end(), asset->path) != m_selectedAssets.end();
                if (ImGui::Selectable("##AssetRow", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    SelectAsset(asset->path, ImGui::GetIO().KeyCtrl);
                    if (ImGui::IsMouseDoubleClicked(0)) OpenAsset(*asset);
                }
                DrawAssetIcon(ImGui::GetWindowDrawList(), position, iconSize, asset->type);
                ImGui::GetWindowDrawList()->AddText({position.x + iconSize + Scaled(6.0f), position.y}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
                assetActions(*asset);
                if (columns > 1) { ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(FitAssetText(LocalizeReflected(asset->type), ImGui::GetContentRegionAvail().x).c_str()); }
                if (columns > 2) { ImGui::TableSetColumnIndex(2); ImGui::Text("%llu KB", static_cast<unsigned long long>((asset->byteSize + 1023) / 1024)); }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    else
    {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int columns = std::max(1, static_cast<int>((available + spacing) / (imageSize + spacing)));
        int column = 0;
        const auto nextTile = [&] { if (++column < columns) ImGui::SameLine(); else column = 0; };
        const auto tile = [&](const std::filesystem::path &path, const AssetInfo *asset) {
            ImGui::PushID(asset ? asset->id.c_str() : path.generic_u8string().c_str());
            const auto position = ImGui::GetCursorScreenPos();
            const ImVec2 size{imageSize, imageSize + ImGui::GetTextLineHeight() + Scaled(14.0f)};
            const bool clicked = ImGui::InvisibleButton("AssetTile", size);
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = asset && std::find(m_selectedAssets.begin(), m_selectedAssets.end(), asset->path) != m_selectedAssets.end();
            auto *draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(position, {position.x + size.x, position.y + size.y},
                ImGui::GetColorU32(selected ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg), Scaled(4.0f));
            std::uintptr_t texture = 0;
            if (asset)
            {
                RequestThumbnail(*asset);
                const auto thumbnail = GetThumbnailPath(*asset);
                if (m_device && IsThumbnailReady(*asset, thumbnail)) texture = m_device->GetUiTexture(thumbnail.generic_u8string());
            }
            if (texture)
            {
                draw->AddImage(ImTextureRef(static_cast<ImTextureID>(texture)), position, {position.x + imageSize, position.y + imageSize});
                DrawAssetIcon(draw, {position.x + Scaled(4.0f), position.y + Scaled(4.0f)}, std::min(imageSize * .3f, Scaled(24.0f)), asset->type);
            }
            else DrawAssetIcon(draw, {position.x + imageSize * .16f, position.y + imageSize * .16f}, imageSize * .68f, asset ? asset->type : "Folder");
            const auto name = FitAssetText(path.filename().u8string(), imageSize - Scaled(8.0f));
            draw->AddText({position.x + Scaled(4.0f), position.y + imageSize + Scaled(7.0f)}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            if (asset)
            {
                if (clicked) SelectAsset(asset->path, ImGui::GetIO().KeyCtrl);
                if (hovered && ImGui::IsMouseDoubleClicked(0)) OpenAsset(*asset);
                assetActions(*asset);
            }
            else
            {
                if (hovered && ImGui::IsMouseDoubleClicked(0)) m_assetFolder = path;
                if (hovered) ImGui::SetTooltip("%s\n%s", path.generic_u8string().c_str(), Localize("Double click to open folder", "双击打开文件夹"));
                folderActions(path);
            }
            ImGui::PopID(); nextTile();
        };
        for (const auto &folder : childFolders) tile(folder, nullptr);
        for (const auto *asset : visibleAssets) tile(std::filesystem::u8path(asset->path), asset);
    }
    if (visibleAssets.empty() && childFolders.empty()) ImGui::TextDisabled("%s", Localize("No matching assets", "没有匹配的资产"));
    if (ImGui::BeginPopupContextWindow("AssetBackground", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        DrawAssetCreationMenu(m_assetFolder);
        ImGui::Separator();
        if (ImGui::MenuItem(Localize("Restore Deleted Assets", "恢复删除的资产"), "Ctrl+Z", false, canWriteAssets && !m_assetDeleteHistory.empty())) RestoreDeletedAssets();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    DrawAssetFolderDialog();
    DrawAssetDeleteDialog();
    if (openDetailsPopup) ImGui::OpenPopup("AssetDetails");
    if (openMovePopup) ImGui::OpenPopup(Localize("Move or Rename Asset###MoveAssetPopup", "移动或重命名资产###MoveAssetPopup"));
    ImGui::SetNextWindowSizeConstraints({std::min(Scaled(360.0f), viewport->WorkSize.x * .8f), 0}, {viewport->WorkSize.x * .9f, viewport->WorkSize.y * .9f});
    if (ImGui::BeginPopupModal(Localize("Move or Rename Asset###MoveAssetPopup", "移动或重命名资产###MoveAssetPopup"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped(Localize("Move: %s", "移动：%s"), moveSource.c_str());
        ImGui::TextUnformatted(Localize("Destination Path", "目标路径"));
        ImGui::BeginDisabled(!canWriteAssets);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##DestinationPath", moveDestination.data(), moveDestination.size())) m_assetMoveError.clear();
        if (!m_assetMoveError.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, {1,.45f,.4f,1}); ImGui::TextWrapped("%s", m_assetMoveError.c_str()); ImGui::PopStyleColor(); }
        if (ImGui::Button(Localize("Move", "移动")))
        {
            const auto enteredPath = std::filesystem::u8path(moveDestination.data());
            const auto destination = m_engine.GetProjectSettings().Resolve(enteredPath).lexically_normal();
            std::string error;
            if (enteredPath.empty() || destination.filename().empty()) error = Localize("Enter a destination file path.", "请输入目标文件路径。");
            else if (!AssetPathWithin(destination, assetRoot)) error = Localize("Choose a path inside the project asset folder.", "请选择项目资产目录内的路径。");
            else if ((m_materialAssetEditor.GetPath() == moveSource && m_materialAssetEditor.IsDirty()) ||
                     (m_materialGraphEditor.GetPath() == moveSource && m_materialGraphEditor.IsDirty()))
                error = Localize("Save or discard material changes before renaming this asset.", "重命名此资产前，请先保存或放弃材质修改。");
            if (error.empty() && MoveEditorAsset(moveSource, destination, error))
            {
                const auto moved = destination.generic_u8string();
                if (m_selectedAsset == moveSource) m_selectedAsset = moved;
                if (m_openShaderAsset == moveSource) m_openShaderAsset = moved;
                if (m_editingAsset == moveSource) m_editingAsset = moved;
                if (m_openAnimationAsset == moveSource) m_openAnimationAsset = moved;
                if (m_openEffectAsset == moveSource) m_openEffectAsset = moved;
                if (m_openBehaviorAsset == moveSource) m_openBehaviorAsset = moved;
                if (m_previewAsset == moveSource) m_previewAsset = moved;
                // Clean material editors reload at the new path on their next draw.
                if (m_materialAssetEditor.GetPath() == moveSource) m_openMaterialAsset.clear();
                if (m_materialGraphEditor.GetPath() == moveSource) m_openMaterialGraphAsset.clear();
                m_selectedAssets = {moved};
                m_assetFolder = destination.parent_path();
                m_assetRevealPending = true;
                Notify(Localize("Asset moved and references updated", "资产已移动，引用已更新"), FeedbackKind::Success);
                ImGui::CloseCurrentPopup();
            }
            else { m_assetMoveError = std::string(Localize("Move failed: ", "移动失败：")) + error; Notify(m_assetMoveError, FeedbackKind::Error); }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(Localize("Cancel", "取消"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    const auto preview = m_selectedAsset.empty() ? std::optional<AssetInfo>{}
                                                 : m_engine.GetContext().resourceManager->GetAssetRegistry().Find(m_selectedAsset);
    if (ImGui::BeginPopup("AssetDetails"))
    {
      if (preview)
      {
        ImGui::SeparatorText(Localize("Preview and Dependencies", "预览与依赖"));
        ImGui::TextWrapped("%s", preview->path.c_str());
        RequestThumbnail(*preview);
        const auto previewThumbnail = GetThumbnailPath(*preview);
        if (m_device && IsThumbnailReady(*preview, previewThumbnail))
        {
            const std::uintptr_t texture = m_device->GetUiTexture(previewThumbnail.generic_u8string());
            if (texture)
            {
                ImGui::Image(ImTextureRef(static_cast<ImTextureID>(texture)), {96.0f, 96.0f});
                ImGui::SameLine();
            }
        }
        ImGui::BeginGroup();
        const std::string previewTypeLabel = LocalizeReflected(preview->type);
        ImGui::Text(Localize("Type: %s", "类型：%s"), previewTypeLabel.c_str());
        ImGui::Text(Localize("Size: %llu bytes", "大小：%llu 字节"), static_cast<unsigned long long>(preview->byteSize));
        ImGui::Text(Localize("Dependencies: %zu", "依赖：%zu"), preview->dependencies.size());
        if (ImGui::Button(Localize("Reference Viewer", "依赖图"))) OpenAssetReferences(preview->path);
        if ((preview->type == "Prefab" || preview->type == "Mesh") && !m_engine.IsPlaying() &&
            ImGui::Button(Localize("Place in Scene", "放入场景")))
            PlaceAssetInScene(*preview);
        ImGui::SameLine();
        if (ImGui::SmallButton(Localize("Regenerate Thumbnail", "重新生成缩略图")))
            RequestThumbnail(*preview, true);
        ImGui::EndGroup();
        auto &registry = m_engine.GetContext().resourceManager->GetAssetRegistry();
        for (const auto &issue : AssetMaintenance::FindMissingDependencies(registry))
            if (issue.owner == preview->path)
            {
                ImGui::TextColored({1.0f, 0.45f, 0.3f, 1.0f}, Localize("Missing: %s", "缺失：%s"), issue.missingPath.c_str());
                for (const auto &candidate : issue.candidates)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton((std::string(Localize("Repair as -> ", "修复为 → ")) + candidate).c_str()))
                    {
                        std::string error;
                        if (!AssetMaintenance::RepairDependency(registry, issue.owner, issue.missingPath, candidate, &error))
                            Notify(std::string(Localize("Dependency repair failed: ", "依赖修复失败：")) + error, FeedbackKind::Error);
                    }
                }
            }
      }
      ImGui::EndPopup();
    }
    ImGui::End();
}
}
