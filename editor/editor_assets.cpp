#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shobjidl.h>
#include <windows.h>
#endif

#include "editor_app.h"
#include "tinyengine/ecs/scene_serializer.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/render/sdl_gl_render_device.h"
#include "tinyengine/resource/asset_maintenance.h"
#include "tinyengine/resource/material_asset_references.h"
#ifdef TINYENGINE_HAS_ASSIMP
#include "tinyengine/import/assimp_importer.h"
#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif
#endif
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <optional>
#include <set>
#include <string_view>

namespace tinyengine
{
namespace
{
std::string lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
std::filesystem::path normalizedAssetPath(const std::filesystem::path& path)
{
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error)
        normalized = std::filesystem::absolute(path, error).lexically_normal();
    return error ? path.lexically_normal() : normalized;
}

std::string assetPathKey(const std::filesystem::path& path)
{
    auto key = toUtf8GenericPath(normalizedAssetPath(path));
#ifdef _WIN32
    key = lowercase(std::move(key));
#endif
    return key;
}

bool assetPathWithin(const std::filesystem::path& path, const std::filesystem::path& folder)
{
    const auto key = assetPathKey(path);
    auto prefix = assetPathKey(folder);
    if (key == prefix)
        return true;
    if (prefix.empty())
        return false;
    if (prefix.back() != '/')
        prefix += '/';
    return key.compare(0, prefix.size(), prefix) == 0;
}

// Truncate at a UTF-8 boundary and measure the current scaled font, rather than byte count.
std::string fitAssetText(const std::string& text, float width)
{
    if (width <= 0.0f)
        return {};
    if (ImGui::CalcTextSize(text.c_str()).x <= width)
        return text;
    const float suffixWidth = ImGui::CalcTextSize("...").x;
    if (suffixWidth > width)
        return {};
    std::size_t length = text.size();
    while (length > 0)
    {
        --length;
        while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80)
            --length;
        if (ImGui::CalcTextSize(text.data(), text.data() + length).x + suffixWidth <= width)
            return text.substr(0, length) + "...";
    }
    return "...";
}

void drawAssetIcon(ImDrawList* draw, ImVec2 origin, float size, const std::string& type)
{
    const bool folder = type == "Folder";
    const ImU32 color = folder                                          ? IM_COL32(203, 170, 100, 255)
                        : type == "Texture"                             ? IM_COL32(103, 169, 128, 255)
                        : type == "Mesh" || type == "Prefab"            ? IM_COL32(110, 159, 211, 255)
                        : type == "Material" || type == "MaterialGraph" ? IM_COL32(192, 136, 172, 255)
                        : type == "Shader"                              ? IM_COL32(213, 164, 109, 255)
                        : type == "Audio"                               ? IM_COL32(164, 146, 209, 255)
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
        const char* code = type == "Shader"                                                         ? "<>"
                           : type == "Audio"                                                        ? "AU"
                           : type == "Scene"                                                        ? "SC"
                           : type == "Animation" || type == "AnimationGraph" || type == "BlendTree" ? "AN"
                           : type == "Skeleton"                                                     ? "SK"
                           : type == "VFX"                                                          ? "FX"
                           : type == "BehaviorTree"                                                 ? "AI"
                           : type == "Font"                                                         ? "Aa"
                           : type == "Environment"                                                  ? "EN"
                           : type == "Input"                                                        ? "IN"
                                                                                                    : "DA";
        const float fontSize = std::min(ImGui::GetFontSize(), size * .48f);
        const auto textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, 1000.0f, 0.0f, code);
        draw->AddText(ImGui::GetFont(), fontSize,
                      {origin.x + (size - textSize.x) * .5f, origin.y + (size - textSize.y) * .5f}, color, code);
    }
}
#ifdef TINYENGINE_HAS_ASSIMP
std::string validateModelSource(const std::filesystem::path& source)
{
    if (source.empty())
        return "Select a source model file.";
    if (!std::filesystem::is_regular_file(source))
        return "The selected model file does not exist.";
    std::ifstream input(source, std::ios::binary);
    if (!input)
        return "The selected model file cannot be read.";
    if (!AssimpImporter::canImport(source))
        return "The selected file extension is not supported by Assimp.";
    return {};
}

const char* modelSourceErrorText(const std::string& error, const bool simplifiedChinese)
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

NativeFileDialogResult openModelFileDialog(const HWND owner, const std::filesystem::path& initialDirectory,
                                           const bool simplifiedChinese)
{
    NativeFileDialogResult result;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    {
        result.error = "Windows COM initialization failed.";
        return result;
    }

    IFileOpenDialog* dialog = nullptr;
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
    for (auto& extension : supported)
    {
        extension = lowercase(extension);
        if (!extension.empty() && extension.front() != '.')
            extension.insert(extension.begin(), '.');
    }
    std::sort(supported.begin(), supported.end());
    supported.erase(std::unique(supported.begin(), supported.end()), supported.end());

    const auto buildPattern = [&supported](const std::vector<std::string>& extensions)
    {
        std::wstring pattern;
        for (const auto& requested : extensions)
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
    const auto addFilter = [&names, &patterns](std::wstring name, std::wstring pattern)
    {
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
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialDirectory.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder)
        {
            dialog->SetFolder(folder);
            folder->release();
        }
    }

    status = dialog->Show(owner);
    if (SUCCEEDED(status))
    {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item)
        {
            PWSTR selected = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &selected)) && selected)
            {
                result.path = std::filesystem::path(selected).lexically_normal();
                CoTaskMemFree(selected);
            }
            item->release();
        }
        if (!result.path)
            result.error = "Windows returned no model file from the picker.";
    }
    else if (status != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        result.error = "The Windows model file picker failed.";

    dialog->release();
    if (uninitialize)
        CoUninitialize();
    return result;
}
#endif
#endif

} // namespace

void EditorApp::locateAsset(const std::string& path)
{
    const auto* resources = m_engine.GetContext().resourceManager;
    if (!resources || path.empty() || path.rfind("builtin:", 0) == 0)
        return;
    const auto& registry = resources->getAssetRegistry();
    auto asset = registry.Find(path);
    if (!asset)
        asset = registry.Find(resources->resolveAssetPath(path));
    if (!asset)
        asset = registry.FindById(path);
    const auto root =
        normalizedAssetPath(m_engine.getProjectSettings().resolve(m_engine.getProjectSettings().assetDirectory));
    if (!asset || !assetPathWithin(fromUtf8Path(asset->path), root))
    {
        notify(std::string(localize("Unable to locate asset: ", "无法定位资产：")) + path, FeedbackKind::Warning);
        return;
    }
    // Highlight in the browser without transferring ownership away from the current inspector.
    m_assetFolder = normalizedAssetPath(fromUtf8Path(asset->path).parent_path());
    m_selectedAssets = {asset->path};
    m_assetSearch.fill('\0');
    m_assetTypeFilters.clear();
    m_showAssets = true;
    m_sceneMaximized = false;
    m_assetRevealPending = true;
    m_focusAssets = true;
}

void EditorApp::pollAssetImport()
{
#ifdef TINYENGINE_HAS_ASSIMP
    if (m_activeImportTask == 0)
        return;
    const auto task = m_importQueue.Get(m_activeImportTask);
    if (task.status == AssetImportStatus::Queued || task.status == AssetImportStatus::Running)
        return;
    if (task.status == AssetImportStatus::Completed)
    {
        if (auto* resources = m_engine.GetContext().resourceManager)
            resources->getAssetRegistry().scan(
                m_engine.getProjectSettings().resolve(m_engine.getProjectSettings().assetDirectory));
        if (!task.result.prefabPath.empty())
        {
            m_selectedAsset = toUtf8GenericPath(task.result.prefabPath.lexically_normal());
            m_selectedAssets = {m_selectedAsset};
            m_assetFolder = task.result.prefabPath.parent_path().lexically_normal();
            m_assetRevealPending = true;
        }
        notify(localize("Model import completed. The Prefab is selected; double-click it, drag it to Scene View, or "
                        "use Place in Scene.",
                        "模型导入完成，已选中 Prefab；可双击、拖到场景视图，或使用“放入场景”。"),
               FeedbackKind::Success);
    }
    else if (task.status == AssetImportStatus::Cancelled)
        notify(localize("Background model import cancelled", "后台模型导入已取消"));
    else if (task.status == AssetImportStatus::Failed)
        notify(std::string(localize("Background model import failed: ", "后台模型导入失败：")) + task.result.error,
               FeedbackKind::Error);
    else
        notify(localize("The import task is no longer available.", "导入任务已不可用。"), FeedbackKind::Warning);
    for (const auto& warning : task.result.warnings)
        notify(std::string(localize("Import warning: ", "导入警告：")) + warning, FeedbackKind::Warning);
    m_importQueue.clearFinished();
    m_activeImportTask = 0;
#endif
}

bool EditorApp::moveEditorAsset(const std::string& source, const std::filesystem::path& destination, std::string& error)
{
    error.clear();
    auto* resources = m_engine.GetContext().resourceManager;
    auto* world = m_engine.GetContext().world;
    if (!resources || m_engine.isPlaying() || m_packageFuture.valid())
    {
        error = localize("Stop play or packaging before renaming assets.", "停止运行或打包后才能重命名资产。");
        return false;
    }
    auto& registry = resources->getAssetRegistry();
    auto asset = registry.Find(source);
    if (!asset)
        asset = registry.Find(resources->resolveAssetPath(source));
    if (!asset)
    {
        error = localize("The source asset is not registered.", "源资产未登记。");
        return false;
    }
    const auto sourcePath = fromUtf8Path(asset->path);
    const auto& settings = m_engine.getProjectSettings();
    const auto assetRoot = normalizedAssetPath(settings.resolve(settings.assetDirectory));
    const auto projectRoot =
        settings.projectFile.empty() ? assetRoot.parent_path() : settings.projectFile.parent_path();
    const auto target = settings.resolve(destination).lexically_normal();
    if (destination.empty() || target.filename().empty() || !assetPathWithin(sourcePath, assetRoot) ||
        !assetPathWithin(target, assetRoot))
    {
        error = localize("Choose a destination inside the project asset folder.", "请选择项目资产目录内的目标路径。");
        return false;
    }
    const auto sameSource = [&](const std::string& path)
    { return !path.empty() && assetPathKey(fromUtf8Path(path)) == assetPathKey(sourcePath); };
    const auto remap = [&](std::string& path)
    { return AssetMaintenance::rewriteReference(path, sourcePath, target, assetRoot, projectRoot); };
    const auto rewriteSnapshot = [&](std::string& snapshot)
    { return AssetMaintenance::rewriteReferences(snapshot, sourcePath, target, assetRoot, projectRoot); };
    bool shaderAffected = sameSource(m_openShaderAsset);
    if (!m_openShaderAsset.empty())
    {
        const auto shaderOwner = fromUtf8Path(m_openShaderAsset);
        const auto shaderTarget = shaderAffected ? target : shaderOwner;
        auto draft = m_shaderSource.empty() ? std::string{} : std::string(m_shaderSource.data());
        auto baseline = m_savedShaderSource;
        auto disk = PlatformFile::readAllText(shaderOwner);
        shaderAffected =
            AssetMaintenance::rewriteShaderIncludes(draft, shaderOwner, shaderTarget, sourcePath, target) ||
            shaderAffected;
        shaderAffected =
            AssetMaintenance::rewriteShaderIncludes(baseline, shaderOwner, shaderTarget, sourcePath, target) ||
            shaderAffected;
        shaderAffected = AssetMaintenance::rewriteShaderIncludes(disk, shaderOwner, shaderTarget, sourcePath, target) ||
                         shaderAffected;
    }
    if (shaderAffected && isShaderDirty())
    {
        error = localize("Save or discard the affected shader draft before renaming this asset.",
                         "请先保存或放弃受影响的着色器草稿，再重命名此资产。");
        return false;
    }
    const auto materialDraftAffected = [&](const std::string& path, std::string text, bool graph)
    {
        return sameSource(path) || (!path.empty() && rewriteMaterialReferences(
                                                         text, graph, fromUtf8Path(path), fromUtf8Path(path), assetRoot,
                                                         toUtf8GenericPath(sourcePath), toUtf8GenericPath(target)));
    };
    auto draftGraph = m_materialGraphEditor.GetDraft().graph;
    draftGraph.GetParameters() = m_materialGraphEditor.GetDraft().parameters;
    if ((m_materialAssetEditor.isDirty() &&
         materialDraftAffected(m_materialAssetEditor.GetPath(),
                               serializeMaterialAsset(m_materialAssetEditor.GetDraft()), false)) ||
        (m_materialGraphEditor.isDirty() &&
         materialDraftAffected(m_materialGraphEditor.GetPath(), draftGraph.serialize(), true)))
    {
        error = localize("Save or discard the affected material draft before renaming this asset.",
                         "请先保存或放弃受影响的材质草稿，再重命名此资产。");
        return false;
    }

    // Prepare every editor mutation first; disk rollback must leave all of this state untouched.
    auto updatedSettings = settings;
    bool environmentChanged = false;
    for (auto* path :
         {&updatedSettings.startupScene, &updatedSettings.inputBindings, &updatedSettings.environmentTexture})
    {
        auto reference = toUtf8GenericPath(*path);
        if (remap(reference))
        {
            if (path == &updatedSettings.environmentTexture)
                environmentChanged = true;
            *path = fromUtf8Path(reference);
        }
    }
    auto documents = m_sceneDocuments;
    auto undo = m_undo, redo = m_redo;
    for (auto& snapshot : undo)
        rewriteSnapshot(snapshot);
    for (auto& snapshot : redo)
        rewriteSnapshot(snapshot);
    for (auto& document : documents)
    {
        const bool changed = rewriteSnapshot(document.snapshot);
        document.dirty = document.dirty || changed;
        if (!document.path.empty() && assetPathKey(document.path) == assetPathKey(sourcePath))
            document.path = target;
        for (auto& snapshot : document.undo)
            rewriteSnapshot(snapshot);
        for (auto& snapshot : document.redo)
            rewriteSnapshot(snapshot);
    }
    std::vector<std::pair<std::string*, std::string>> liveReferences;
    if (world)
        for (const auto entity : world->GetEntityManager().GetAliveEntities())
            for (const auto& component : ReflectionRegistry::Get().GetComponents())
                if (component.has(*world, entity))
                    for (const auto& property : component.properties)
                        if (property.type == ReflectedPropertyType::String && !property.assetType.empty() &&
                            property.access)
                            if (auto* reference = static_cast<std::string*>(property.access(*world, entity)))
                            {
                                auto updated = *reference;
                                if (remap(updated))
                                    liveReferences.emplace_back(reference, std::move(updated));
                            }
    if (world && m_activeSceneDocument < documents.size())
    {
        auto& active = documents[m_activeSceneDocument];
        active.snapshot = SceneSerializer::serialize(*world);
        rewriteSnapshot(active.snapshot);
        active.undo = undo;
        active.redo = redo;
        active.dirty = m_sceneDirty || !liveReferences.empty();
    }
    const auto before = registry.GetAssets();
    if (!AssetMaintenance::moveAsset(registry, sourcePath, target, &error, projectRoot, settings.projectFile))
        return false;

    const_cast<ProjectSettings&>(m_engine.getProjectSettings()) = std::move(updatedSettings);
    for (auto& reference : liveReferences)
        *reference.first = std::move(reference.second);
    m_sceneDocuments = std::move(documents);
    m_undo = std::move(undo);
    m_redo = std::move(redo);
    m_sceneDirty = m_sceneDirty || !liveReferences.empty();
    for (auto* path :
         {&m_selectedAsset, &m_editingAsset, &m_previewAsset, &m_assetReferenceRoot, &m_openShaderAsset,
          &m_pendingShaderPath, &m_openAnimationAsset, &m_openEffectAsset, &m_openBehaviorAsset, &m_scenePreviewAsset})
        remap(*path);
    m_assetReferencesDirty = true;
    if (shaderAffected)
    {
        const bool hidden = m_shaderEditorHidden;
        // Force a clean reread even for an incoming include rewrite at the same owner path.
        m_shaderEditorHidden = true;
        openShaderDocument(m_openShaderAsset);
        m_shaderEditorHidden = hidden;
    }
    for (auto& path : m_selectedAssets)
        remap(path);
    for (auto& draft : m_inspectorAssetDrafts)
        remap(draft.second);
    for (auto& sample : m_editBlendTree.samples)
        remap(sample.clipPath);
    for (auto& state : m_editAnimationGraph.states)
        remap(state.motionPath);
    remap(m_editEffect.texturePath);
    if (environmentChanged)
        if (auto* renderer = m_engine.GetContext().renderer)
            renderer->setEnvironmentPath(toUtf8GenericPath(settings.resolve(settings.environmentTexture)));

    // Refresh cached dependents whose persisted references changed. New World paths load the moved resource at its new
    // key.
    for (const auto& previous : before)
    {
        if (previous.path == asset->path)
            continue;
        const auto current = registry.Find(previous.path);
        if (!current || (current->modified == previous.modified && current->byteSize == previous.byteSize &&
                         current->dependencies == previous.dependencies))
            continue;
        resources->reload(current->path);
        if (auto* renderer = m_engine.GetContext().renderer;
            renderer && (current->type == "Material" || current->type == "MaterialGraph"))
            renderer->GetMaterialSystem().reload(current->path);
    }
    resources->garbageCollect();
    if (!m_materialGraphEditor.isDirty() && !m_materialGraphEditor.GetPath().empty())
    {
        auto path = m_materialGraphEditor.GetPath();
        const bool moved = remap(path);
        MaterialDefinitionResource definition;
        if (definition.load(path))
        {
            if (moved)
                m_materialGraphEditor.Open(path, definition.GetData());
            else
                m_materialGraphEditor.refreshFromDisk(definition.GetData());
            m_openMaterialGraphAsset = path;
        }
    }
    if (!m_materialAssetEditor.isDirty() && !m_materialAssetEditor.GetPath().empty())
    {
        auto path = m_materialAssetEditor.GetPath();
        const bool moved = remap(path);
        MaterialResource material;
        if (material.loadWithResolver(path, [&](const auto& reference, const auto& owner)
                                      { return resources->resolveAssetPath(reference, owner); }))
        {
            if (moved)
                m_materialAssetEditor.Open(path, material.GetMaterialData(), {});
            else
                m_materialAssetEditor.refreshFromDisk(material.GetMaterialData());
            m_openMaterialAsset = path;
        }
    }
    return true;
}

void EditorApp::drawAssetCreationMenu(const std::filesystem::path& directory)
{
    const auto assetRoot =
        normalizedAssetPath(m_engine.getProjectSettings().resolve(m_engine.getProjectSettings().assetDirectory));
    std::error_code error;
    const bool canCreate = !m_engine.isPlaying() && !m_packageFuture.valid() && assetPathWithin(directory, assetRoot) &&
                           std::filesystem::is_directory(directory, error);
    ImGui::BeginDisabled(!canCreate);
    if (ImGui::MenuItem(localize("New Folder", "新建文件夹")))
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
    const auto publishCreated = [&](const std::filesystem::path& path)
    {
        m_engine.GetContext().resourceManager->getAssetRegistry().scan(assetRoot);
        selectAsset(toUtf8GenericPath(path), false);
        m_assetFolder = directory;
        m_assetSearch.fill(0);
        m_assetTypeFilters.clear();
        m_assetRevealPending = true;
    };
    const auto createUniquePath = [&directory](const std::string& stem, const std::string& extension)
    {
        const auto& root = directory;
        for (int suffix = 0;; ++suffix)
        {
            const auto name = stem + (suffix == 0 ? std::string{} : std::to_string(suffix)) + extension;
            const auto path = root / name;
            if (!std::filesystem::exists(path))
                return path;
        }
    };
    const auto ensureTemplateClip = [this, &createUniquePath]() -> std::optional<std::string>
    {
        auto assets = m_engine.GetContext().resourceManager->getAssetRegistry().GetAssets();
        std::sort(assets.begin(), assets.end(),
                  [](const AssetInfo& left, const AssetInfo& right) { return left.path < right.path; });
        std::filesystem::path clipPath;
        for (const auto& asset : assets)
            if (asset.extension == ".skanim" && PlatformFile::exists(fromUtf8Path(asset.path)))
            {
                clipPath = fromUtf8Path(asset.path);
                break;
            }
        if (clipPath.empty())
        {
            clipPath = createUniquePath("NewAnimationClip", ".skanim");
            SkeletalClip clip;
            clip.name = toUtf8Path(clipPath.stem());
            clip.duration = 1.0f;
            clip.keyframes = {{0.0f, {BonePose{}}}, {1.0f, {BonePose{}}}};
            if (!saveSkeletalClipAsset(clip, clipPath))
                return std::nullopt;
        }
        std::error_code error;
        const auto relative =
            std::filesystem::relative(clipPath, m_engine.getProjectSettings().projectFile.parent_path(), error);
        return error || relative.empty() ? toUtf8GenericPath(clipPath.lexically_normal())
                                         : toUtf8GenericPath(relative.lexically_normal());
    };
    if (ImGui::MenuItem(localize("Material", "材质")))
    {
        const auto path = createUniquePath("NewMaterial", ".matgraph");
        MaterialGraph graph;
        graph.setFormatVersion(2);
        graph.addNode(MaterialNodeType::SurfaceOutput, "Surface", {420, 60});
        MaterialParameterDecl color;
        color.name = "BaseColor";
        color.displayName = "Base Color";
        color.type = MaterialParameterType::Vec3;
        color.defaultValue = Vec3{.8f, .8f, .8f};
        graph.GetParameters().push_back(color);
        const auto id = graph.addNode(MaterialNodeType::VectorParameter, "Base Color", {40, 60});
        graph.GetNodes().back().parameterName = "BaseColor";
        graph.connect(id, 1, 0);
        if (graph.Save(path))
            publishCreated(path);
        else
            notify(localize("Unable to save material.", "无法保存材质。"), FeedbackKind::Error);
    }
    if (ImGui::MenuItem(localize("Material Instance", "材质实例")))
    {
        auto& registry = m_engine.GetContext().resourceManager->getAssetRegistry();
        const auto parent = registry.Find(m_selectedAsset);
        const auto path = createUniquePath("NewMaterialInstance", ".mat");
        std::string error;
        const auto reference = parent && (parent->type == "Material" || parent->type == "MaterialGraph")
                                   ? parent->path
                                   : std::string("builtin:PBR");
        if (MaterialAssetEditor::createInstance(toUtf8GenericPath(path), reference, error))
            publishCreated(path);
        else
            notify(error, FeedbackKind::Error);
    }
    if (ImGui::MenuItem(localize("+ Blend Tree", "+ 混合树")))
    {
        const auto clip = ensureTemplateClip();
        if (!clip)
            notify(localize("Unable to create the starter animation clip.", "无法创建初始动画片段。"),
                   FeedbackKind::Error);
        else
        {
            BlendTree1D tree;
            tree.samples = {{0.0f, *clip}};
            const auto path = createUniquePath("NewBlendTree", ".blendtree");
            if (saveBlendTreeAsset(tree, path))
            {
                auto& registry = m_engine.GetContext().resourceManager->getAssetRegistry();
                publishCreated(path);
                registry.setDependencies(toUtf8GenericPath(path), {*clip});
            }
        }
    }
    if (ImGui::MenuItem(localize("+ Animation Graph", "+ 动画图")))
    {
        const auto clip = ensureTemplateClip();
        if (!clip)
            notify(localize("Unable to create the starter animation clip.", "无法创建初始动画片段。"),
                   FeedbackKind::Error);
        else
        {
            AnimationGraph graph;
            graph.entryState = "Idle";
            graph.parameters = {{"Speed", 0.0f}};
            graph.states = {{"Idle", *clip}};
            const auto path = createUniquePath("NewAnimationGraph", ".animgraph");
            if (saveAnimationGraphAsset(graph, path))
            {
                auto& registry = m_engine.GetContext().resourceManager->getAssetRegistry();
                publishCreated(path);
                registry.setDependencies(toUtf8GenericPath(path), {*clip});
            }
        }
    }
    if (ImGui::MenuItem(localize("+ VFX", "+ 特效")))
    {
        const auto path = createUniquePath("NewEffect", ".vfx");
        if (saveParticleEffectAsset({}, path))
        {
            publishCreated(path);
        }
    }
    if (ImGui::MenuItem(localize("+ Behavior Tree", "+ 行为树")))
    {
        BehaviorTreeGraph graph;
        graph.root = 1;
        graph.nodes = {{1, "Sequence", "Root", {20.0f, 20.0f}, {2}}, {2, "Action", "Action", {220.0f, 20.0f}, {}}};
        const auto path = createUniquePath("NewBehavior", ".btgraph");
        if (saveBehaviorTreeGraph(graph, path))
        {
            publishCreated(path);
        }
    }
    ImGui::EndDisabled();
}

void EditorApp::drawAssetFolderDialog()
{
    if (m_assetFolderCreateRequested)
    {
        m_assetFolderCreateRequested = false;
        ImGui::OpenPopup(localize("New Folder###NewAssetFolder", "新建文件夹###NewAssetFolder"));
    }
    if (!ImGui::BeginPopupModal(localize("New Folder###NewAssetFolder", "新建文件夹###NewAssetFolder"), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextWrapped("%s", toUtf8GenericPath(m_assetCreateParent).c_str());
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(scaled(300.0f));
    const bool enter = ImGui::InputText(localize("Name", "名称"), m_assetFolderName.data(), m_assetFolderName.size(),
                                        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
    if (!m_assetFolderError.empty())
        ImGui::TextColored({1, .45f, .3f, 1}, "%s", m_assetFolderError.c_str());
    const bool canWrite = !m_engine.isPlaying() && !m_packageFuture.valid();
    ImGui::BeginDisabled(!canWrite);
    if ((enter && canWrite) || ImGui::Button(localize("Create", "创建")))
    {
        const std::string name = m_assetFolderName.data();
        const auto root = m_engine.getProjectSettings().resolve(m_engine.getProjectSettings().assetDirectory);
        const auto target = m_assetCreateParent / fromUtf8Path(name);
        std::error_code error;
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:*?\"<>|") != std::string::npos ||
            name.back() == '.' || name.back() == ' ' || !assetPathWithin(target, root))
            m_assetFolderError = localize("Enter a valid folder name.", "请输入有效的文件夹名称。");
        else if (!std::filesystem::is_directory(m_assetCreateParent, error))
            m_assetFolderError = localize("The parent folder is no longer available.", "父文件夹已不存在。");
        else if (std::filesystem::exists(target, error))
            m_assetFolderError = localize("An item with this name already exists.", "同名文件或文件夹已存在。");
        else if (!std::filesystem::create_directory(target, error))
            m_assetFolderError =
                std::string(localize("Could not create folder: ", "无法创建文件夹：")) + error.message();
        else
        {
            m_assetFolder = normalizedAssetPath(target);
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
    if (ImGui::Button(localize("Cancel", "取消")))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void EditorApp::drawAssets()
{
    m_assetsFocused = false;
    if (!m_showAssets)
        return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto layout = getWorkspaceLayout(viewport->WorkSize.x, viewport->WorkSize.y);
    const float top = viewport->WorkPos.y + layout.upper + 3.0f;
    const float halfWidth = layout.assets - 3.0f;
    if (m_focusAssets)
        ImGui::SetNextWindowFocus();
    if (!beginEditorPanel("Assets", "资产", m_showAssets, {viewport->WorkPos.x, top},
                          {halfWidth, viewport->WorkPos.y + viewport->WorkSize.y - top}))
    {
        ImGui::End();
        return;
    }
    m_focusAssets = false;
    m_assetsFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const auto assetRoot =
        normalizedAssetPath(m_engine.getProjectSettings().resolve(m_engine.getProjectSettings().assetDirectory));
    std::error_code folderError;
    if (m_assetFolder.empty() || !assetPathWithin(m_assetFolder, assetRoot) ||
        !std::filesystem::is_directory(m_assetFolder, folderError))
        m_assetFolder = assetRoot;
    m_assetFolder = normalizedAssetPath(m_assetFolder);
    const bool canWriteAssets = !m_engine.isPlaying() && !m_packageFuture.valid();
    loadAssetTrashHistory();
    const float toolbarRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const auto continueToolbar = [&](const char* label)
    {
        const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= toolbarRight)
            ImGui::SameLine();
    };
    ImGui::BeginDisabled(!canWriteAssets);
    if (ImGui::Button(localize("Create...", "新建资产...")))
        ImGui::OpenPopup("CreateAssetMenu");
    if (ImGui::BeginPopup("CreateAssetMenu"))
    {
        drawAssetCreationMenu(m_assetFolder);
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
#ifdef TINYENGINE_HAS_ASSIMP
    if (m_activeImportTask != 0)
    {
        const auto task = m_importQueue.Get(m_activeImportTask);
        ImGui::ProgressBar(task.progress, ImVec2(220.0f, 0.0f),
                           task.status == AssetImportStatus::Queued    ? localize("Queued", "等待中")
                           : task.status == AssetImportStatus::Running ? localize("Importing", "正在导入")
                                                                       : localize("Finishing", "正在完成"));
        if ((task.status == AssetImportStatus::Queued || task.status == AssetImportStatus::Running) &&
            ImGui::Button(localize("Cancel Import", "取消导入")))
            m_importQueue.Cancel(m_activeImportTask);
        ImGui::Separator();
    }
    ImGui::BeginDisabled(m_activeImportTask != 0 || !canWriteAssets);
    continueToolbar(localize("Import Model...", "导入模型..."));
    if (ImGui::Button(localize("Import Model...", "导入模型...")))
    {
#ifdef _WIN32
        SDL_SysWMinfo windowInfo{};
        SDL_VERSION(&windowInfo.version);
        HWND owner = nullptr;
        if (SDL_GetWindowWMInfo(static_cast<SDL_Window*>(m_device->getNativeWindow()), &windowInfo) == SDL_TRUE)
            owner = windowInfo.info.win.window;
        const auto selected = openModelFileDialog(owner, m_lastImportDirectory,
                                                  m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese);
        if (selected.path)
        {
            m_importSource = *selected.path;
            const std::string path = toUtf8GenericPath(*selected.path);
            std::fill(m_importSourcePath.begin(), m_importSourcePath.end(), '\0');
            std::strncpy(m_importSourcePath.data(), path.c_str(), m_importSourcePath.size() - 1);
            m_lastImportDirectory = selected.path->parent_path();
            m_importValidationError = validateModelSource(*selected.path);
            saveEditorSettings();
            ImGui::OpenPopup(localize("Import Model###ImportModelPopup", "导入模型###ImportModelPopup"));
        }
        else if (!selected.error.empty())
            notify(std::string(localize("File picker failed: ", "文件选择器失败：")) + selected.error,
                   FeedbackKind::Error);
#else
        m_importSource = fromUtf8Path(m_importSourcePath.data());
        m_importValidationError = validateModelSource(m_importSource);
        ImGui::OpenPopup(localize("Import Model###ImportModelPopup", "导入模型###ImportModelPopup"));
#endif
    }
    continueToolbar(localize("Reimport", "重新导入"));
    const auto selectedInfo = m_selectedAsset.empty()
                                  ? std::optional<AssetInfo>{}
                                  : m_engine.GetContext().resourceManager->getAssetRegistry().Find(m_selectedAsset);
    ImGui::BeginDisabled(!selectedInfo || selectedInfo->type != "Prefab" ||
                         selectedInfo->importSettings.count("source") == 0 || m_activeImportTask != 0);
    if (ImGui::Button(localize("Reimport", "重新导入")))
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
        if (const auto compress = selectedInfo->importSettings.find("compressAnimations");
            compress != selectedInfo->importSettings.end())
            options.compressAnimations = compress->second == "true";
        const auto readFloatSetting = [&selectedInfo](const char* name, float& value)
        {
            if (const auto setting = selectedInfo->importSettings.find(name);
                setting != selectedInfo->importSettings.end())
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
        const auto projectRoot = m_engine.getProjectSettings().projectFile.empty()
                                     ? std::filesystem::current_path()
                                     : m_engine.getProjectSettings().projectFile.parent_path();
        options.resourcePrefix = std::filesystem::relative(output, projectRoot);
        m_activeImportTask = m_importQueue.submit(source, output, options);
        notify(std::string(localize("Model reimport queued: ", "模型重新导入已加入队列：")) + source.generic_string());
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal(localize("Import Model###ImportModelPopup", "导入模型###ImportModelPopup"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SetNextItemWidth(520.0f);
#ifdef _WIN32
        ImGui::InputText(localize("Source File", "源文件"), m_importSourcePath.data(), m_importSourcePath.size(),
                         ImGuiInputTextFlags_ReadOnly);
#else
        if (ImGui::InputText(localize("Source File", "源文件"), m_importSourcePath.data(), m_importSourcePath.size()))
        {
            m_importSource = fromUtf8Path(m_importSourcePath.data());
            m_importValidationError = validateModelSource(m_importSource);
        }
#endif
#ifdef _WIN32
        ImGui::SameLine();
        if (ImGui::Button(localize("Browse...", "浏览...")))
        {
            SDL_SysWMinfo windowInfo{};
            SDL_VERSION(&windowInfo.version);
            HWND owner = nullptr;
            if (SDL_GetWindowWMInfo(static_cast<SDL_Window*>(m_device->getNativeWindow()), &windowInfo) == SDL_TRUE)
                owner = windowInfo.info.win.window;
            const auto selected = openModelFileDialog(owner, m_lastImportDirectory,
                                                      m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese);
            if (selected.path)
            {
                m_importSource = *selected.path;
                const std::string path = toUtf8GenericPath(*selected.path);
                std::fill(m_importSourcePath.begin(), m_importSourcePath.end(), '\0');
                std::strncpy(m_importSourcePath.data(), path.c_str(), m_importSourcePath.size() - 1);
                m_lastImportDirectory = selected.path->parent_path();
                m_importValidationError = validateModelSource(*selected.path);
                saveEditorSettings();
            }
            else if (!selected.error.empty())
                m_importValidationError = selected.error;
        }
#else
        ImGui::SameLine();
        ImGui::TextDisabled("%s", localize("Native file picker is currently available on Windows.",
                                           "原生文件选择器目前仅在 Windows 上提供。"));
#endif
        if (!m_importValidationError.empty())
            ImGui::TextColored({1.0f, 0.35f, 0.28f, 1.0f}, "%s",
                               modelSourceErrorText(m_importValidationError,
                                                    m_interfaceLanguage == InterfaceLanguage::SimplifiedChinese));
        else
            ImGui::TextColored({0.35f, 0.85f, 0.45f, 1.0f}, "%s", localize("Ready to import", "文件有效，可以导入"));
        ImGui::DragFloat(localize("Scale", "缩放"), &m_importScale, 0.01f, 0.0001f, 1000.0f);
        ImGui::Checkbox(localize("Flip UVs", "翻转 UV"), &m_importFlipUvs);
        ImGui::Checkbox(localize("Compress Animations", "压缩动画"), &m_importCompressAnimations);
        ImGui::BeginDisabled(!m_importCompressAnimations);
        ImGui::DragFloat(localize("Position Error", "位置误差"), &m_importPositionTolerance, 0.0001f, 0.0f, 1.0f,
                         "%.5f");
        ImGui::DragFloat(localize("Rotation Error (Degrees)", "旋转误差（度）"), &m_importRotationTolerance, 0.01f,
                         0.0f, 45.0f, "%.3f");
        ImGui::DragFloat(localize("Scale Error", "缩放误差"), &m_importScaleTolerance, 0.0001f, 0.0f, 1.0f, "%.5f");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_importValidationError.empty() || m_importSourcePath[0] == '\0');
        if (ImGui::Button(localize("Import", "导入")))
        {
            const std::filesystem::path source = m_importSource;
            const std::string folderName = source.stem().empty() ? "Model" : source.stem().string();
            const auto relativeOutput = m_engine.getProjectSettings().assetDirectory / "Imported" / folderName;
            const auto output = m_engine.getProjectSettings().resolve(relativeOutput);
            ModelImportOptions options;
            options.scale = m_importScale;
            options.flipUvs = m_importFlipUvs;
            options.compressAnimations = m_importCompressAnimations;
            options.animationPositionTolerance = m_importPositionTolerance;
            options.animationRotationToleranceDegrees = m_importRotationTolerance;
            options.animationScaleTolerance = m_importScaleTolerance;
            options.resourcePrefix = relativeOutput;
            m_activeImportTask = m_importQueue.submit(source, output, options);
            notify(std::string(localize("Model import queued: ", "模型导入已加入队列：")) + source.generic_string());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(localize("Cancel", "取消")))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
#endif
    auto assets = m_engine.GetContext().resourceManager->getAssetRegistry().GetAssets();
    std::sort(assets.begin(), assets.end(), [](const AssetInfo& a, const AssetInfo& b) { return a.path < b.path; });
    static std::string moveSource;
    static std::array<char, 1024> moveDestination{};
    std::error_code pathError;
    if (m_assetFolder.empty() || !assetPathWithin(m_assetFolder, assetRoot) ||
        !std::filesystem::is_directory(m_assetFolder, pathError))
        m_assetFolder = assetRoot;
    m_assetFolder = normalizedAssetPath(m_assetFolder);
    const bool wideBrowser = ImGui::GetContentRegionAvail().x >= scaled(560.0f);
    const auto activeAsset = m_selectedAssets.size() == 1 ? m_selectedAssets.front()
                             : m_selectedAssets.empty()   ? m_selectedAsset
                                                          : std::string{};
    const bool canRename =
        canWriteAssets && !activeAsset.empty() &&
        std::any_of(assets.begin(), assets.end(), [&](const AssetInfo& asset) { return asset.path == activeAsset; });
    bool openMovePopup = false;
    bool openDetailsPopup = false;
    const auto requestMove = [&](const std::string& source)
    {
        if (!canWriteAssets)
            return;
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
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
            requestMove(activeAsset);
    }

    const auto toolbarButton = [&](const char* label, bool sameLine)
    {
        const float desired = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (sameLine && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + desired <= toolbarRight)
            ImGui::SameLine();
        return ImGui::Button(label, {std::min(desired, std::max(1.0f, ImGui::GetContentRegionAvail().x)), 0});
    };
    if (toolbarButton(localize("Filters", "筛选"), true))
        ImGui::OpenPopup("AssetFilters");
    if (toolbarButton(localize("Display", "显示"), true))
        ImGui::OpenPopup("AssetViewOptions");
    if (!wideBrowser && toolbarButton(localize("Folders", "文件夹"), true))
        ImGui::OpenPopup("AssetFolderTree");
    ImGui::BeginDisabled(!canRename);
    if (toolbarButton(localize("Rename (F2)", "重命名 (F2)"), true))
        requestMove(activeAsset);
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("AssetFilters"))
    {
        std::set<std::string> availableTypes = {
            "Material",    "MaterialGraph", "Texture",   "Mesh",           "Prefab", "Scene",        "Shader",
            "Animation",   "Skeleton",      "BlendTree", "AnimationGraph", "VFX",    "BehaviorTree", "Audio",
            "Environment", "Input",         "Font"};
        for (const auto& asset : assets)
            availableTypes.insert(asset.type);
        ImGui::TextDisabled("%s", localize("Asset types (match any selected type)", "资产类型（匹配任一勾选类型）"));
        if (ImGui::Selectable(localize("All Types", "全部类型"), m_assetTypeFilters.empty(),
                              ImGuiSelectableFlags_DontClosePopups))
            m_assetTypeFilters.clear();
        ImGui::Separator();
        for (const auto& type : availableTypes)
        {
            bool enabled = m_assetTypeFilters.count(type) != 0;
            if (ImGui::Checkbox(localizeReflected(type).c_str(), &enabled))
            {
                if (enabled)
                    m_assetTypeFilters.insert(type);
                else
                    m_assetTypeFilters.erase(type);
            }
        }
        if (ImGui::Button(localize("Clear filters", "清除筛选")))
            m_assetTypeFilters.clear();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("AssetViewOptions"))
    {
        ImGui::TextDisabled("%s", localize("View mode", "视图模式"));
        if (ImGui::RadioButton(localize("Grid", "网格"), m_assetViewMode == AssetViewMode::Grid))
        {
            m_assetViewMode = AssetViewMode::Grid;
            saveEditorSettings();
        }
        if (ImGui::RadioButton(localize("List", "列表"), m_assetViewMode == AssetViewMode::List))
        {
            m_assetViewMode = AssetViewMode::List;
            saveEditorSettings();
        }
        if (m_assetViewMode == AssetViewMode::Grid)
        {
            ImGui::SetNextItemWidth(scaled(190.0f));
            if (ImGui::SliderFloat("##ThumbnailSize", &m_assetThumbnailSize, 64.0f, 160.0f,
                                   localize("%.0f px", "%.0f 像素")))
                saveEditorSettings();
        }
        ImGui::EndPopup();
    }

    // Wide panels reserve two compact toolbar rows, leaving room for actual asset rows.
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const float clearWidth = ImGui::GetFrameHeight();
    const float searchGroupWidth = wideBrowser ? std::clamp(rowWidth * .28f, scaled(200.0f), scaled(400.0f)) : rowWidth;
    ImGui::SetNextItemWidth(std::max(1.0f, searchGroupWidth - clearWidth - ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##AssetSearch", localize("Search assets...", "搜索资产..."), m_assetSearch.data(),
                             m_assetSearch.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(m_assetSearch[0] == '\0' && m_assetTypeFilters.empty());
    if (ImGui::Button("X##ClearAssetFilters", {clearWidth, 0}))
    {
        m_assetSearch.fill('\0');
        m_assetTypeFilters.clear();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", localize("Clear search and type filter", "清除搜索和类型筛选"));

    // Breadcrumbs wrap without moving when only the asset highlight changes.
    if (wideBrowser)
        ImGui::SameLine();
    if (ImGui::SmallButton(localize("Assets", "资产")))
        m_assetFolder = assetRoot;
    auto breadcrumb = assetRoot;
    for (const auto& part : m_assetFolder.lexically_relative(assetRoot))
    {
        if (part == "." || part.empty())
            continue;
        breadcrumb /= part;
        const auto name = toUtf8Path(part);
        const float width = std::min({ImGui::CalcTextSize(name.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f,
                                      scaled(150.0f), std::max(1.0f, ImGui::GetContentRegionAvail().x)});
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= toolbarRight)
            ImGui::SameLine();
        ImGui::PushID(toUtf8GenericPath(breadcrumb).c_str());
        const auto clipped = fitAssetText(name, width - ImGui::GetStyle().FramePadding.x * 2.0f);
        if (ImGui::SmallButton(clipped.c_str()))
            m_assetFolder = breadcrumb;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", toUtf8GenericPath(breadcrumb).c_str());
        ImGui::PopID();
    }
    if (!m_assetTypeFilters.empty())
    {
        std::string remove;
        ImGui::TextDisabled("%s", localize("Active filters:", "已选筛选："));
        for (const auto& type : m_assetTypeFilters)
        {
            const auto label = localizeReflected(type) + "  x###RemoveFilter";
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x +
                    ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2 <=
                toolbarRight)
                ImGui::SameLine();
            ImGui::PushID(type.c_str());
            if (ImGui::SmallButton(label.c_str()))
                remove = type;
            ImGui::PopID();
        }
        if (!remove.empty())
            m_assetTypeFilters.erase(remove);
    }
    const auto folderActions = [&](const std::filesystem::path& folder)
    {
        if (ImGui::BeginPopupContextItem("FolderContext"))
        {
            if (ImGui::MenuItem(localize("Open Folder", "打开文件夹")))
                m_assetFolder = folder;
            ImGui::Separator();
            drawAssetCreationMenu(folder);
            ImGui::EndPopup();
        }
    };
    const auto folderKey = ImGui::GetID(toUtf8GenericPath(m_assetFolder).c_str());
    auto* treeState = ImGui::GetStateStorage();
    const auto treeStateKey = ImGui::GetID("AssetTreeCurrentFolder");
    const bool expandFolder =
        m_assetRevealPending || static_cast<ImGuiID>(treeState->GetInt(treeStateKey)) != folderKey;
    treeState->SetInt(treeStateKey, static_cast<int>(folderKey));
    std::function<void(const std::filesystem::path&)> drawFolderTree;
    drawFolderTree = [&](const std::filesystem::path& folder)
    {
        std::vector<std::filesystem::path> children;
        std::error_code error;
        for (std::filesystem::directory_iterator
                 iterator(folder, std::filesystem::directory_options::skip_permission_denied, error),
             end;
             !error && iterator != end; iterator.increment(error))
        {
            std::error_code entryError;
            if (iterator->is_directory(entryError) && !iterator->is_symlink(entryError))
                children.push_back(iterator->path());
        }
        std::sort(children.begin(), children.end());
        const bool root = assetPathKey(folder) == assetPathKey(assetRoot);
        const bool selected = assetPathKey(folder) == assetPathKey(m_assetFolder);
        ImGui::PushID(toUtf8GenericPath(folder).c_str());
        auto flags =
            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (root)
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (selected)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (expandFolder && assetPathWithin(m_assetFolder, folder))
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        const auto name = root ? std::string(localize("Assets", "资产")) : toUtf8Path(folder.filename());
        const auto clipped = fitAssetText(name, ImGui::GetContentRegionAvail().x - ImGui::GetTreeNodeToLabelSpacing());
        const bool open = ImGui::TreeNodeEx("##Folder", flags, "%s", clipped.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            m_assetFolder = folder;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", toUtf8GenericPath(folder).c_str());
        folderActions(folder);
        if (selected && m_assetRevealPending)
            ImGui::SetScrollHereY(.5f);
        if (open && !children.empty())
        {
            for (const auto& child : children)
                drawFolderTree(child);
            ImGui::TreePop();
        }
        ImGui::PopID();
    };
    if (!wideBrowser)
    {
        ImGui::SetNextWindowSize({std::min(scaled(280.0f), viewport->WorkSize.x * .8f),
                                  std::min(scaled(340.0f), viewport->WorkSize.y * .7f)},
                                 ImGuiCond_Appearing);
        if (ImGui::BeginPopup("AssetFolderTree"))
        {
            drawFolderTree(assetRoot);
            ImGui::EndPopup();
        }
    }

    const auto search = lowercase(m_assetSearch.data());
    const auto regenerateThumbnails = [&](const AssetInfo& fallback)
    {
        if (m_selectedAssets.size() <= 1 ||
            std::find(m_selectedAssets.begin(), m_selectedAssets.end(), fallback.path) == m_selectedAssets.end())
        {
            requestThumbnail(fallback, true);
            return;
        }
        for (const auto& asset : assets)
            if (std::find(m_selectedAssets.begin(), m_selectedAssets.end(), asset.path) != m_selectedAssets.end())
                requestThumbnail(asset, true);
    };
    const auto assetActions = [&](const AssetInfo& asset)
    {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            if (std::find(m_selectedAssets.begin(), m_selectedAssets.end(), asset.path) == m_selectedAssets.end())
                selectAsset(asset.path, false);
            else
                m_selectedAsset = asset.path;
        }
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload("TINY_ASSET_PATH", asset.path.c_str(), asset.path.size() + 1);
            ImGui::TextUnformatted(toUtf8Path(fromUtf8Path(asset.path).filename()).c_str());
            ImGui::TextDisabled(
                "%s", localize("Drop into Scene View or a compatible reference", "拖到场景视图或匹配类型的引用字段"));
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("AssetContext"))
        {
            if (ImGui::MenuItem(localize("Details / Preview", "详情 / 预览")))
                openDetailsPopup = true;
            if (ImGui::MenuItem(localize("Reference Viewer", "依赖图")))
                openAssetReferences(asset.path);
            ImGui::Separator();
            if (asset.type == "Material" || asset.type == "MaterialGraph")
            {
                if (ImGui::MenuItem(localize("Apply to Selected Meshes", "应用到选中网格"), nullptr, false,
                                    canApplyMaterialToSelection()))
                    applyMaterialToMeshes(asset.path, GetInspectorTargets());
                if (ImGui::MenuItem(localize("Create Material Instance", "创建材质实例"), nullptr, false,
                                    canWriteAssets))
                {
                    const auto directory = fromUtf8Path(asset.path).parent_path();
                    auto path = directory / "NewMaterialInstance.mat";
                    for (int suffix = 1; std::filesystem::exists(path); ++suffix)
                        path = directory / ("NewMaterialInstance" + std::to_string(suffix) + ".mat");
                    std::string error;
                    if (MaterialAssetEditor::createInstance(toUtf8GenericPath(path), asset.path, error))
                    {
                        m_engine.GetContext().resourceManager->getAssetRegistry().scan(assetRoot);
                        selectAsset(toUtf8GenericPath(path), false);
                    }
                    else
                        notify(error, FeedbackKind::Error);
                }
            }
            if ((asset.type == "Prefab" || asset.type == "Mesh") &&
                ImGui::MenuItem(localize("Place in Scene", "放入场景"), nullptr, false, canWriteAssets))
                placeAssetInScene(asset);
            if (ImGui::MenuItem(localize("Move / Rename...", "移动 / 重命名..."), "F2", false, canWriteAssets))
                requestMove(asset.path);
            if (ImGui::MenuItem(localize("Delete...", "删除..."), "Delete", false, canWriteAssets))
                requestDeleteAssets();
            const auto label = m_selectedAssets.size() > 1
                                   ? std::string(localize("Regenerate Selected Thumbnails (", "重新生成所选缩略图（")) +
                                         std::to_string(m_selectedAssets.size()) + localize(")", "）")
                                   : std::string(localize("Regenerate Thumbnail", "重新生成缩略图"));
            if (ImGui::MenuItem(label.c_str()))
                regenerateThumbnails(asset);
            ImGui::EndPopup();
        }
        if (m_assetRevealPending && m_selectedAssets.size() == 1 && m_selectedAssets.front() == asset.path)
        {
            ImGui::SetScrollHereY(.5f);
            m_assetRevealPending = false;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s", asset.path.c_str(), localizeReflected(asset.type).c_str());
    };
    std::vector<const AssetInfo*> visibleAssets;
    std::set<std::filesystem::path> childFolders;
    if (search.empty())
    {
        pathError.clear();
        for (std::filesystem::directory_iterator
                 iterator(m_assetFolder, std::filesystem::directory_options::skip_permission_denied, pathError),
             end;
             !pathError && iterator != end; iterator.increment(pathError))
        {
            std::error_code error;
            if (iterator->is_directory(error) && !iterator->is_symlink(error))
                childFolders.insert(iterator->path());
        }
    }
    for (const auto& asset : assets)
    {
        const auto path = normalizedAssetPath(fromUtf8Path(asset.path));
        if (!assetPathWithin(path, assetRoot) ||
            (!m_assetTypeFilters.empty() && m_assetTypeFilters.count(asset.type) == 0))
            continue;
        if (search.empty())
        {
            if (assetPathKey(path.parent_path()) != assetPathKey(m_assetFolder))
                continue;
        }
        else if (lowercase(toUtf8Path(path.filename()) + " " + asset.type + " " + asset.path).find(search) ==
                 std::string::npos)
            continue;
        visibleAssets.push_back(&asset);
    }
    ImGui::Separator();
    const float browserHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    if (wideBrowser)
    {
        ImGui::BeginChild(
            "AssetFolderTree",
            {std::clamp(ImGui::GetContentRegionAvail().x * .24f, scaled(120.0f), scaled(220.0f)), browserHeight},
            ImGuiChildFlags_Borders);
        drawFolderTree(assetRoot);
        ImGui::EndChild();
        ImGui::SameLine();
    }
    // Keep the items child last and directly under Assets for the existing click/drag harness.
    ImGui::BeginChild("AssetBrowserItems", {0.0f, browserHeight}, ImGuiChildFlags_Borders);
    const float available = ImGui::GetContentRegionAvail().x;
    const float imageSize = std::max(1.0f, std::min(scaled(m_assetThumbnailSize), available));
    const bool list = m_assetViewMode == AssetViewMode::List ||
                      browserHeight < imageSize + ImGui::GetTextLineHeight() + scaled(14.0f) ||
                      available < scaled(64.0f);
    if (list)
    {
        for (const auto& folder : childFolders)
        {
            ImGui::PushID(toUtf8GenericPath(folder).c_str());
            const auto position = ImGui::GetCursorScreenPos();
            const float iconSize = ImGui::GetTextLineHeight();
            if (ImGui::Selectable("##FolderRow", false, ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(0))
                m_assetFolder = folder;
            drawAssetIcon(ImGui::GetWindowDrawList(), position, iconSize, "Folder");
            const auto label = fitAssetText(toUtf8Path(folder.filename()), available - iconSize - scaled(6.0f));
            ImGui::GetWindowDrawList()->AddText({position.x + iconSize + scaled(6.0f), position.y},
                                                ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%s", toUtf8GenericPath(folder).c_str(),
                                  localize("Double click to open folder", "双击打开文件夹"));
            folderActions(folder);
            ImGui::PopID();
        }
        const int columns = available >= scaled(410.0f) ? 3 : available >= scaled(260.0f) ? 2 : 1;
        if (ImGui::BeginTable("AssetList", columns,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn(localize("Name", "名称"), ImGuiTableColumnFlags_WidthStretch);
            if (columns > 1)
                ImGui::TableSetupColumn(localize("Type", "类型"), ImGuiTableColumnFlags_WidthFixed, scaled(100.0f));
            if (columns > 2)
                ImGui::TableSetupColumn(localize("Size", "大小"), ImGuiTableColumnFlags_WidthFixed, scaled(76.0f));
            ImGui::TableHeadersRow();
            for (const auto* asset : visibleAssets)
            {
                ImGui::PushID(asset->id.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const auto position = ImGui::GetCursorScreenPos();
                const float iconSize = ImGui::GetTextLineHeight();
                const auto name = fitAssetText(toUtf8Path(fromUtf8Path(asset->path).filename()),
                                               ImGui::GetContentRegionAvail().x - iconSize - scaled(6.0f));
                const bool selected =
                    std::find(m_selectedAssets.begin(), m_selectedAssets.end(), asset->path) != m_selectedAssets.end();
                if (ImGui::Selectable("##AssetRow", selected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    selectAsset(asset->path, ImGui::GetIO().KeyCtrl);
                    if (ImGui::IsMouseDoubleClicked(0))
                        openAsset(*asset);
                }
                drawAssetIcon(ImGui::GetWindowDrawList(), position, iconSize, asset->type);
                ImGui::GetWindowDrawList()->AddText({position.x + iconSize + scaled(6.0f), position.y},
                                                    ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
                assetActions(*asset);
                if (columns > 1)
                {
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(
                        fitAssetText(localizeReflected(asset->type), ImGui::GetContentRegionAvail().x).c_str());
                }
                if (columns > 2)
                {
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%llu KB", static_cast<unsigned long long>((asset->byteSize + 1023) / 1024));
                }
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
        const auto nextTile = [&]
        {
            if (++column < columns)
                ImGui::SameLine();
            else
                column = 0;
        };
        const auto tile = [&](const std::filesystem::path& path, const AssetInfo* asset)
        {
            ImGui::PushID(asset ? asset->id.c_str() : toUtf8GenericPath(path).c_str());
            const auto position = ImGui::GetCursorScreenPos();
            const ImVec2 size{imageSize, imageSize + ImGui::GetTextLineHeight() + scaled(14.0f)};
            const bool clicked = ImGui::InvisibleButton("AssetTile", size);
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = asset && std::find(m_selectedAssets.begin(), m_selectedAssets.end(), asset->path) !=
                                               m_selectedAssets.end();
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled(position, {position.x + size.x, position.y + size.y},
                                ImGui::GetColorU32(selected  ? ImGuiCol_HeaderActive
                                                   : hovered ? ImGuiCol_HeaderHovered
                                                             : ImGuiCol_FrameBg),
                                scaled(4.0f));
            std::uintptr_t texture = 0;
            if (asset)
            {
                requestThumbnail(*asset);
                const auto thumbnail = getThumbnailPath(*asset);
                if (m_device && isThumbnailReady(*asset, thumbnail))
                    texture = m_device->getUiTexture(toUtf8GenericPath(thumbnail));
            }
            if (texture)
            {
                draw->AddImage(ImTextureRef(static_cast<ImTextureID>(texture)), position,
                               {position.x + imageSize, position.y + imageSize});
                drawAssetIcon(draw, {position.x + scaled(4.0f), position.y + scaled(4.0f)},
                              std::min(imageSize * .3f, scaled(24.0f)), asset->type);
            }
            else
                drawAssetIcon(draw, {position.x + imageSize * .16f, position.y + imageSize * .16f}, imageSize * .68f,
                              asset ? asset->type : "Folder");
            const auto name = fitAssetText(toUtf8Path(path.filename()), imageSize - scaled(8.0f));
            draw->AddText({position.x + scaled(4.0f), position.y + imageSize + scaled(7.0f)},
                          ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            if (asset)
            {
                if (clicked)
                    selectAsset(asset->path, ImGui::GetIO().KeyCtrl);
                if (hovered && ImGui::IsMouseDoubleClicked(0))
                    openAsset(*asset);
                assetActions(*asset);
            }
            else
            {
                if (hovered && ImGui::IsMouseDoubleClicked(0))
                    m_assetFolder = path;
                if (hovered)
                    ImGui::SetTooltip("%s\n%s", toUtf8GenericPath(path).c_str(),
                                      localize("Double click to open folder", "双击打开文件夹"));
                folderActions(path);
            }
            ImGui::PopID();
            nextTile();
        };
        for (const auto& folder : childFolders)
            tile(folder, nullptr);
        for (const auto* asset : visibleAssets)
            tile(fromUtf8Path(asset->path), asset);
    }
    if (visibleAssets.empty() && childFolders.empty())
        ImGui::TextDisabled("%s", localize("No matching assets", "没有匹配的资产"));
    if (ImGui::BeginPopupContextWindow("AssetBackground",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        drawAssetCreationMenu(m_assetFolder);
        ImGui::Separator();
        if (ImGui::MenuItem(localize("Restore Deleted Assets", "恢复删除的资产"), "Ctrl+Z", false,
                            canWriteAssets && !m_assetDeleteHistory.empty()))
            restoreDeletedAssets();
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    drawAssetFolderDialog();
    drawAssetDeleteDialog();
    if (openDetailsPopup)
        ImGui::OpenPopup("AssetDetails");
    if (openMovePopup)
        ImGui::OpenPopup(localize("Move or Rename Asset###MoveAssetPopup", "移动或重命名资产###MoveAssetPopup"));
    ImGui::SetNextWindowSizeConstraints({std::min(scaled(360.0f), viewport->WorkSize.x * .8f), 0},
                                        {viewport->WorkSize.x * .9f, viewport->WorkSize.y * .9f});
    if (ImGui::BeginPopupModal(localize("Move or Rename Asset###MoveAssetPopup", "移动或重命名资产###MoveAssetPopup"),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped(localize("Move: %s", "移动：%s"), moveSource.c_str());
        ImGui::TextUnformatted(localize("Destination Path", "目标路径"));
        ImGui::BeginDisabled(!canWriteAssets);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##DestinationPath", moveDestination.data(), moveDestination.size()))
            m_assetMoveError.clear();
        if (!m_assetMoveError.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, {1, .45f, .4f, 1});
            ImGui::TextWrapped("%s", m_assetMoveError.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::Button(localize("Move", "移动")))
        {
            const auto enteredPath = fromUtf8Path(moveDestination.data());
            const auto destination = m_engine.getProjectSettings().resolve(enteredPath).lexically_normal();
            std::string error;
            if (enteredPath.empty() || destination.filename().empty())
                error = localize("Enter a destination file path.", "请输入目标文件路径。");
            else if (!assetPathWithin(destination, assetRoot))
                error = localize("Choose a path inside the project asset folder.", "请选择项目资产目录内的路径。");
            else if ((m_materialAssetEditor.GetPath() == moveSource && m_materialAssetEditor.isDirty()) ||
                     (m_materialGraphEditor.GetPath() == moveSource && m_materialGraphEditor.isDirty()))
                error = localize("Save or discard material changes before renaming this asset.",
                                 "重命名此资产前，请先保存或放弃材质修改。");
            if (error.empty() && moveEditorAsset(moveSource, destination, error))
            {
                const auto moved = toUtf8GenericPath(destination);
                if (m_selectedAsset == moveSource)
                    m_selectedAsset = moved;
                if (m_openShaderAsset == moveSource)
                    m_openShaderAsset = moved;
                if (m_editingAsset == moveSource)
                    m_editingAsset = moved;
                if (m_openAnimationAsset == moveSource)
                    m_openAnimationAsset = moved;
                if (m_openEffectAsset == moveSource)
                    m_openEffectAsset = moved;
                if (m_openBehaviorAsset == moveSource)
                    m_openBehaviorAsset = moved;
                if (m_previewAsset == moveSource)
                    m_previewAsset = moved;
                // Clean material editors reload at the new path on their next draw.
                if (m_materialAssetEditor.GetPath() == moveSource)
                    m_openMaterialAsset.clear();
                if (m_materialGraphEditor.GetPath() == moveSource)
                    m_openMaterialGraphAsset.clear();
                m_selectedAssets = {moved};
                m_assetFolder = destination.parent_path();
                m_assetRevealPending = true;
                notify(localize("Asset moved and references updated", "资产已移动，引用已更新"), FeedbackKind::Success);
                ImGui::CloseCurrentPopup();
            }
            else
            {
                m_assetMoveError = std::string(localize("Move failed: ", "移动失败：")) + error;
                notify(m_assetMoveError, FeedbackKind::Error);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(localize("Cancel", "取消")))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    const auto preview = m_selectedAsset.empty()
                             ? std::optional<AssetInfo>{}
                             : m_engine.GetContext().resourceManager->getAssetRegistry().Find(m_selectedAsset);
    if (ImGui::BeginPopup("AssetDetails"))
    {
        if (preview)
        {
            ImGui::SeparatorText(localize("Preview and Dependencies", "预览与依赖"));
            ImGui::TextWrapped("%s", preview->path.c_str());
            requestThumbnail(*preview);
            const auto previewThumbnail = getThumbnailPath(*preview);
            if (m_device && isThumbnailReady(*preview, previewThumbnail))
            {
                const std::uintptr_t texture = m_device->getUiTexture(toUtf8GenericPath(previewThumbnail));
                if (texture)
                {
                    ImGui::Image(ImTextureRef(static_cast<ImTextureID>(texture)), {96.0f, 96.0f});
                    ImGui::SameLine();
                }
            }
            ImGui::BeginGroup();
            const std::string previewTypeLabel = localizeReflected(preview->type);
            ImGui::Text(localize("Type: %s", "类型：%s"), previewTypeLabel.c_str());
            ImGui::Text(localize("Size: %llu bytes", "大小：%llu 字节"),
                        static_cast<unsigned long long>(preview->byteSize));
            ImGui::Text(localize("Dependencies: %zu", "依赖：%zu"), preview->dependencies.size());
            if (ImGui::Button(localize("Reference Viewer", "依赖图")))
                openAssetReferences(preview->path);
            if ((preview->type == "Prefab" || preview->type == "Mesh") && !m_engine.isPlaying() &&
                ImGui::Button(localize("Place in Scene", "放入场景")))
                placeAssetInScene(*preview);
            ImGui::SameLine();
            if (ImGui::SmallButton(localize("Regenerate Thumbnail", "重新生成缩略图")))
                requestThumbnail(*preview, true);
            ImGui::EndGroup();
            auto& registry = m_engine.GetContext().resourceManager->getAssetRegistry();
            for (const auto& issue : AssetMaintenance::FindMissingDependencies(registry))
                if (issue.owner == preview->path)
                {
                    ImGui::TextColored({1.0f, 0.45f, 0.3f, 1.0f}, localize("Missing: %s", "缺失：%s"),
                                       issue.missingPath.c_str());
                    for (const auto& candidate : issue.candidates)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(
                                (std::string(localize("Repair as -> ", "修复为 → ")) + candidate).c_str()))
                        {
                            std::string error;
                            if (!AssetMaintenance::repairDependency(registry, issue.owner, issue.missingPath, candidate,
                                                                    &error))
                                notify(std::string(localize("Dependency repair failed: ", "依赖修复失败：")) + error,
                                       FeedbackKind::Error);
                        }
                    }
                }
        }
        ImGui::EndPopup();
    }
    ImGui::End();
}
} // namespace tinyengine
