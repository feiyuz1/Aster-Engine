#include "animation_asset_editor.h"
#include "tinyengine/platform/platform.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <limits>

namespace tinyengine
{
namespace
{
const char* text(bool chinese, const char* english, const char* translated)
{
    return chinese ? translated : english;
}
std::size_t boneCount(const SkeletalClip& clip)
{
    return clip.isCompressed()      ? clip.tracks.size()
           : clip.keyframes.empty() ? 0
                                    : clip.keyframes.front().localPose.size();
}
bool finite(const Vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool hasText(const std::string& value)
{
    return value.find_first_not_of(" \t\r\n") != std::string::npos;
}
bool inputText(const char* label, std::string& value)
{
    std::vector<char> buffer(std::max<std::size_t>(value.size() + 256, 1024), '\0');
    std::memcpy(buffer.data(), value.data(), value.size());
    if (!ImGui::InputText(label, buffer.data(), buffer.size()))
        return false;
    value = buffer.data();
    return true;
}
Vec3 transformPosition(const Mat4& matrix, const Vec3& position)
{
    return {
        matrix.at(0, 0) * position.x + matrix.at(0, 1) * position.y + matrix.at(0, 2) * position.z + matrix.at(0, 3),
        matrix.at(1, 0) * position.x + matrix.at(1, 1) * position.y + matrix.at(1, 2) * position.z + matrix.at(1, 3),
        matrix.at(2, 0) * position.x + matrix.at(2, 1) * position.y + matrix.at(2, 2) * position.z + matrix.at(2, 3)};
}
Vec3 skinPosition(const MeshVertex& vertex, const std::vector<Mat4>& palette)
{
    const float indices[] = {vertex.boneIndices.x, vertex.boneIndices.y, vertex.boneIndices.z, vertex.boneIndices.w};
    const float weights[] = {vertex.boneWeights.x, vertex.boneWeights.y, vertex.boneWeights.z, vertex.boneWeights.w};
    Vec3 position{};
    float total = 0;
    for (int influence = 0; influence < 4; ++influence)
    {
        if (!std::isfinite(indices[influence]) || !std::isfinite(weights[influence]) || weights[influence] <= 0 ||
            indices[influence] < 0 || indices[influence] >= static_cast<float>(palette.size()))
            continue;
        position += transformPosition(palette[static_cast<std::size_t>(indices[influence])], vertex.position) *
                    weights[influence];
        total += weights[influence];
    }
    return total > 0 ? position / total : vertex.position;
}
} // namespace

bool AnimationAssetEditor::validateEvent(const AnimationEvent& event)
{
    if (!std::isfinite(event.time) || event.time < 0 || event.time > m_state.clip.duration || !hasText(event.name))
    {
        m_error = "Events require a name and a finite time between zero and clip duration.";
        return false;
    }
    return true;
}

bool AnimationAssetEditor::validateClip(const SkeletalClip& clip)
{
    if (!std::isfinite(clip.duration) || !clip.isValid(boneCount(clip)))
    {
        m_error = "Invalid animation clip.";
        return false;
    }
    const auto validCurve = [](const std::vector<Vec3CurveKey>& curve)
    {
        return std::all_of(curve.begin(), curve.end(),
                           [](const Vec3CurveKey& key) { return std::isfinite(key.time) && finite(key.value); });
    };
    for (const auto& key : clip.keyframes)
    {
        if (!std::isfinite(key.time))
        {
            m_error = "Animation contains nonfinite pose data.";
            return false;
        }
        for (const auto& pose : key.localPose)
        {
            if (!finite(pose.position) || !finite(pose.rotation) || !finite(pose.scale))
            {
                m_error = "Animation contains nonfinite pose data.";
                return false;
            }
        }
    }
    for (const auto& track : clip.tracks)
    {
        if (!validCurve(track.positions) || !validCurve(track.rotations) || !validCurve(track.scales))
        {
            m_error = "Animation contains nonfinite pose data.";
            return false;
        }
    }
    if (!validCurve(clip.rootMotion))
    {
        m_error = "Animation contains nonfinite pose data.";
        return false;
    }
    for (const auto& event : clip.events)
    {
        if (!std::isfinite(event.time) || !hasText(event.name))
        {
            m_error = "Events require a name and a finite time between zero and clip duration.";
            return false;
        }
    }
    return true;
}

bool AnimationAssetEditor::open(const std::string& path, AssetRegistry* registry)
{
    const auto normalizedPath = toUtf8GenericPath(fromUtf8Path(path).lexically_normal());
    if (m_path == normalizedPath && !m_path.empty())
    {
        m_visible = true;
        return true;
    }
    if (isDirty())
    {
        m_error = "Save or revert the current clip before switching assets.";
        m_visible = true;
        return false;
    }
    SkeletalClip candidate;
    if (!loadSkeletalClipAsset(fromUtf8Path(normalizedPath), candidate))
    {
        m_error = "Unable to open animation asset: " + normalizedPath;
        return false;
    }
    if (!validateClip(candidate))
        return false;
    m_path = normalizedPath;
    m_registry = registry;
    m_state = {};
    m_state.clip = std::move(candidate);
    m_state.revision = ++m_nextRevision;
    m_state.skeletonReference = findPreviewReference("preview.skeleton", "Skeleton");
    m_state.meshReference = findPreviewReference("preview.mesh", "Mesh");
    m_saved = m_state;
    m_undo.clear();
    m_redo.clear();
    m_skeletonCache.clear();
    m_meshCache.clear();
    m_playhead = 0;
    m_speed = 1;
    m_playing = false;
    m_previewLoop = m_state.clip.looping;
    m_selectedBone = -1;
    m_yaw = 0.6f;
    m_pitch = 0.25f;
    m_zoom = 1;
    m_visible = true;
    m_focused = false;
    m_closeRequested = false;
    m_error.clear();
    resolvePreview();
    return true;
}

std::optional<AssetInfo> AnimationAssetEditor::findAsset(const std::string& reference) const
{
    if (!m_registry || reference.empty())
        return {};
    if (auto asset = m_registry->FindById(reference))
        return asset;
    if (auto asset = m_registry->Find(reference))
        return asset;
    return m_registry->Find(
        toUtf8GenericPath((m_registry->GetRootPath() / fromUtf8Path(reference)).lexically_normal()));
}

std::string AnimationAssetEditor::findPreviewReference(const char* setting, const char* type) const
{
    if (m_registry)
    {
        if (const auto clipAsset = m_registry->Find(m_path))
        {
            const auto found = clipAsset->importSettings.find(setting);
            if (found != clipAsset->importSettings.end() && !found->second.empty())
                return found->second;
            for (const auto& dependency : clipAsset->dependencies)
            {
                const auto asset = findAsset(dependency);
                if (asset && asset->type == type && (asset->type != "Mesh" || asset->extension == ".mesh"))
                    return asset->id;
            }
        }
    }
    if (std::string(type) == "Skeleton")
    {
        auto fallback = fromUtf8Path(m_path);
        fallback.replace_extension(".skeleton");
        if (PlatformFile::exists(fallback))
            return toUtf8GenericPath(fallback);
    }
    return {};
}

void AnimationAssetEditor::resolvePreview()
{
    m_skeleton = {};
    m_mesh.reset();
    m_skeletonPath.clear();
    m_meshPath.clear();
    m_previewError.clear();
    const auto skeletonAsset = findAsset(m_state.skeletonReference);
    if (skeletonAsset && skeletonAsset->type != "Skeleton")
        m_previewError = "Preview skeleton reference has the wrong asset type.";
    else if (!m_state.skeletonReference.empty())
    {
        const std::string candidatePath = skeletonAsset ? skeletonAsset->path : m_state.skeletonReference;
        auto cached = m_skeletonCache.find(candidatePath);
        if (cached == m_skeletonCache.end())
        {
            Skeleton candidate;
            if (loadSkeletonAsset(fromUtf8Path(candidatePath), candidate))
                cached = m_skeletonCache.emplace(candidatePath, std::move(candidate)).first;
        }
        if (cached == m_skeletonCache.end())
            m_previewError = "Unable to load preview skeleton: " + candidatePath;
        else if (cached->second.bones.size() != boneCount(m_state.clip))
            m_previewError = "Preview skeleton bone count does not match the clip.";
        else
        {
            m_skeleton = cached->second;
            m_skeletonPath = candidatePath;
        }
    }
    else
        m_previewError = "Choose a skeleton to preview this clip.";
    const auto meshAsset = findAsset(m_state.meshReference);
    if (meshAsset && meshAsset->type != "Mesh")
        m_previewError += " Preview mesh reference has the wrong asset type.";
    else if (!m_state.meshReference.empty())
    {
        const std::string candidatePath = meshAsset ? meshAsset->path : m_state.meshReference;
        auto cached = m_meshCache.find(candidatePath);
        if (cached == m_meshCache.end())
        {
            auto candidate = std::make_shared<MeshResource>();
            if (candidate->load(candidatePath))
                cached = m_meshCache.emplace(candidatePath, std::move(candidate)).first;
        }
        if (cached == m_meshCache.end())
            m_previewError += " Unable to load preview mesh: " + candidatePath;
        else
        {
            m_mesh = cached->second;
            m_meshPath = candidatePath;
        }
    }
    prepareMesh();
    refreshPose();
    fitPreview();
}

void AnimationAssetEditor::prepareMesh()
{
    m_previewSourceVertices.clear();
    m_previewIndices.clear();
    if (!m_mesh)
        return;
    // Keep the interactive CPU preview bounded for large imported models.
    const auto& vertices = m_mesh->GetVertices();
    const auto& indices = m_mesh->GetIndices();
    const std::size_t triangleCount = indices.size() / 3;
    const std::size_t stride = std::max<std::size_t>(1, (triangleCount + 5999) / 6000);
    std::unordered_map<std::uint32_t, std::uint32_t> remap;
    for (std::size_t triangle = 0; triangle < triangleCount; triangle += stride)
    {
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const auto index = indices[triangle * 3 + corner];
            if (index >= vertices.size())
                continue;
            auto found = remap.find(index);
            if (found == remap.end())
            {
                const auto compactIndex = static_cast<std::uint32_t>(m_previewSourceVertices.size());
                m_previewSourceVertices.push_back(vertices[index]);
                found = remap.emplace(index, compactIndex).first;
            }
            m_previewIndices.push_back(found->second);
        }
    }
}

void AnimationAssetEditor::refreshPose()
{
    m_pose = m_skeleton.bones.empty() ? std::vector<BonePose>{}
                                      : m_state.clip.sample(m_playhead, m_skeleton.bones.size(), false);
    m_snapshot = buildAnimationDebugSnapshot(m_skeleton, m_pose);
    const auto palette = BuildSkinMatrices(m_skeleton, m_pose);
    m_previewVertices.clear();
    m_previewVertices.reserve(m_previewSourceVertices.size());
    for (const auto& vertex : m_previewSourceVertices)
        m_previewVertices.push_back(skinPosition(vertex, palette));
}

void AnimationAssetEditor::fitPreview()
{
    Vec3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max()};
    Vec3 maximum = minimum * -1;
    bool anyPoint = false;
    const auto include = [&](const Vec3& point)
    {
        if (!finite(point))
            return;
        anyPoint = true;
        minimum = {std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z)};
        maximum = {std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z)};
    };
    // Fit once to three reference poses, then retain the frame during playback.
    for (const float time : {0.0f, m_state.clip.duration * 0.5f, m_state.clip.duration})
    {
        const auto pose = m_state.clip.sample(time, m_skeleton.bones.size(), false);
        const auto snapshot = buildAnimationDebugSnapshot(m_skeleton, pose);
        for (const auto& point : snapshot.jointPositions)
            include(point);
        const auto palette = BuildSkinMatrices(m_skeleton, pose);
        for (const auto& vertex : m_previewSourceVertices)
            include(skinPosition(vertex, palette));
    }
    m_previewCenter = anyPoint ? (minimum + maximum) * 0.5f : Vec3{};
    m_previewRadius = anyPoint ? std::max(0.05f, length(maximum - minimum) * 0.5f) : 1.0f;
}

void AnimationAssetEditor::checkpoint()
{
    m_undo.push_back(m_state);
    if (m_undo.size() > 64)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    m_state.revision = ++m_nextRevision;
    m_error.clear();
}

bool AnimationAssetEditor::setName(const std::string& name)
{
    if (m_path.empty() || !hasText(name))
    {
        m_error = "Clip name must not be empty.";
        return false;
    }
    if (m_state.clip.name != name)
    {
        checkpoint();
        m_state.clip.name = name;
    }
    return true;
}

bool AnimationAssetEditor::setLooping(bool looping)
{
    if (m_path.empty())
        return false;
    if (m_state.clip.looping != looping)
    {
        checkpoint();
        m_state.clip.looping = looping;
    }
    return true;
}

bool AnimationAssetEditor::addEvent(const AnimationEvent& event)
{
    if (m_path.empty() || !validateEvent(event))
        return false;
    checkpoint();
    m_state.clip.events.push_back(event);
    return true;
}

bool AnimationAssetEditor::updateEvent(std::size_t index, const AnimationEvent& event)
{
    if (index >= m_state.clip.events.size() || !validateEvent(event))
        return false;
    const auto& previous = m_state.clip.events[index];
    if (previous.time == event.time && previous.name == event.name && previous.payload == event.payload)
        return true;
    checkpoint();
    m_state.clip.events[index] = event;
    return true;
}

bool AnimationAssetEditor::removeEvent(std::size_t index)
{
    if (index >= m_state.clip.events.size())
        return false;
    checkpoint();
    m_state.clip.events.erase(m_state.clip.events.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool AnimationAssetEditor::compress()
{
    if (m_path.empty() || m_state.clip.isCompressed())
        return false;
    checkpoint();
    m_state.clip.compress();
    refreshPose();
    return true;
}

bool AnimationAssetEditor::selectPreviewSkeleton(const std::string& reference)
{
    const auto asset = findAsset(reference);
    if (!asset || asset->type != "Skeleton")
    {
        m_error = "Select a registered Skeleton asset.";
        return false;
    }
    Skeleton candidate;
    if (!loadSkeletonAsset(fromUtf8Path(asset->path), candidate) || candidate.bones.size() != boneCount(m_state.clip))
    {
        m_error = "Preview skeleton bone count does not match the clip.";
        return false;
    }
    if (m_state.skeletonReference != asset->id)
    {
        checkpoint();
        m_state.skeletonReference = asset->id;
        m_skeletonCache[asset->path] = std::move(candidate);
        resolvePreview();
        m_selectedBone = -1;
    }
    return true;
}

bool AnimationAssetEditor::selectPreviewMesh(const std::string& reference)
{
    const auto asset = findAsset(reference);
    if (!asset || asset->type != "Mesh" || asset->extension != ".mesh")
    {
        m_error = "Select a registered compiled Mesh asset.";
        return false;
    }
    auto candidate = std::make_shared<MeshResource>();
    if (!candidate->load(asset->path))
    {
        m_error = "Unable to load preview mesh: " + asset->path;
        return false;
    }
    if (m_state.meshReference != asset->id)
    {
        checkpoint();
        m_state.meshReference = asset->id;
        m_meshCache[asset->path] = std::move(candidate);
        resolvePreview();
    }
    return true;
}

bool AnimationAssetEditor::undo()
{
    if (m_undo.empty())
        return false;
    m_redo.push_back(m_state);
    m_state = std::move(m_undo.back());
    m_undo.pop_back();
    m_error.clear();
    resolvePreview();
    return true;
}

bool AnimationAssetEditor::redo()
{
    if (m_redo.empty())
        return false;
    m_undo.push_back(m_state);
    m_state = std::move(m_redo.back());
    m_redo.pop_back();
    m_error.clear();
    resolvePreview();
    return true;
}

bool AnimationAssetEditor::save()
{
    m_error.clear();
    if (m_path.empty() || !validateClip(m_state.clip))
        return false;
    SkeletalClip candidate = m_state.clip;
    std::stable_sort(candidate.events.begin(), candidate.events.end(),
                     [](const AnimationEvent& left, const AnimationEvent& right) { return left.time < right.time; });
    const auto temporary = fromUtf8Path(m_path + ".animation-editor.tmp");
    if (!saveSkeletalClipAsset(candidate, temporary))
    {
        m_error = "Unable to save animation asset: " + m_path;
        return false;
    }
    const auto serialized = PlatformFile::readAllText(temporary);
    const bool saved = !serialized.empty() && PlatformFile::writeAllTextAtomic(fromUtf8Path(m_path), serialized);
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    if (!saved)
    {
        m_error = "Unable to save animation asset: " + m_path;
        return false;
    }
    if (m_registry &&
        (m_state.skeletonReference != m_saved.skeletonReference || m_state.meshReference != m_saved.meshReference))
    {
        std::vector<std::string> dependencies;
        if (const auto asset = m_registry->Find(m_path))
        {
            dependencies = asset->dependencies;
        }
        for (const auto& previous : {m_saved.skeletonReference, m_saved.meshReference})
        {
            if (!previous.empty() && previous != m_state.skeletonReference && previous != m_state.meshReference)
            {
                dependencies.erase(std::remove(dependencies.begin(), dependencies.end(), previous), dependencies.end());
            }
        }
        for (const auto& reference : {m_state.skeletonReference, m_state.meshReference})
        {
            if (!reference.empty() &&
                std::find(dependencies.begin(), dependencies.end(), reference) == dependencies.end())
            {
                dependencies.push_back(reference);
            }
        }
        if (!m_registry->setDependencies(m_path, std::move(dependencies)) ||
            !m_registry->setImportSetting(m_path, "preview.skeleton", m_state.skeletonReference) ||
            !m_registry->setImportSetting(m_path, "preview.mesh", m_state.meshReference))
        {
            m_error = "Clip saved, but preview metadata could not be saved. Retry Save.";
            return false;
        }
    }
    m_state.clip = std::move(candidate);
    m_saved = m_state;
    return true;
}

void AnimationAssetEditor::discard()
{
    m_state = m_saved;
    m_undo.clear();
    m_redo.clear();
    m_error.clear();
    reset();
    resolvePreview();
}

std::vector<std::string> AnimationAssetEditor::retainedAssetPaths() const
{
    std::vector<std::string> paths;
    if (!m_path.empty())
    {
        paths.push_back(m_path);
    }
    const auto retain = [&](const DocumentState& state)
    {
        for (const auto& reference : {state.skeletonReference, state.meshReference})
        {
            if (const auto asset = findAsset(reference))
            {
                paths.push_back(asset->path);
            }
            else if (!reference.empty())
            {
                paths.push_back(reference);
            }
        }
    };
    retain(m_state);
    retain(m_saved);
    for (const auto& state : m_undo)
    {
        retain(state);
    }
    for (const auto& state : m_redo)
    {
        retain(state);
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

bool AnimationAssetEditor::revert()
{
    SkeletalClip candidate;
    if (m_path.empty() || !loadSkeletalClipAsset(fromUtf8Path(m_path), candidate))
    {
        m_error = "Unable to reload animation asset: " + m_path;
        return false;
    }
    if (!validateClip(candidate))
        return false;
    m_state.clip = std::move(candidate);
    m_state.skeletonReference = findPreviewReference("preview.skeleton", "Skeleton");
    m_state.meshReference = findPreviewReference("preview.mesh", "Mesh");
    m_state.revision = ++m_nextRevision;
    m_saved = m_state;
    m_undo.clear();
    m_redo.clear();
    m_skeletonCache.clear();
    m_meshCache.clear();
    m_error.clear();
    m_playhead = 0;
    m_playing = false;
    m_previewLoop = m_state.clip.looping;
    resolvePreview();
    return true;
}

void AnimationAssetEditor::play()
{
    if (m_path.empty())
        return;
    if (m_playhead >= m_state.clip.duration)
        scrub(0);
    m_playing = true;
}
void AnimationAssetEditor::pause()
{
    m_playing = false;
}
void AnimationAssetEditor::reset()
{
    m_playing = false;
    scrub(0);
}
void AnimationAssetEditor::scrub(float time)
{
    if (!std::isfinite(time))
        return;
    m_playhead = std::clamp(time, 0.0f, m_state.clip.duration);
    refreshPose();
}
void AnimationAssetEditor::update(float deltaTime)
{
    if (!m_playing || !std::isfinite(deltaTime) || deltaTime <= 0 || m_state.clip.duration <= 0)
        return;
    const double next = static_cast<double>(m_playhead) + static_cast<double>(deltaTime) * m_speed;
    if (m_previewLoop)
        m_playhead = static_cast<float>(std::fmod(next, m_state.clip.duration));
    else
    {
        m_playhead = static_cast<float>(std::min(next, static_cast<double>(m_state.clip.duration)));
        if (m_playhead >= m_state.clip.duration)
            m_playing = false;
    }
    refreshPose();
}
void AnimationAssetEditor::setPlaybackSpeed(float speed)
{
    if (std::isfinite(speed))
        m_speed = std::clamp(speed, 0.05f, 4.0f);
}
void AnimationAssetEditor::setPreviewLoop(bool loop)
{
    m_previewLoop = loop;
}
void AnimationAssetEditor::hide()
{
    m_visible = false;
    m_focused = false;
    m_playing = false;
}

void AnimationAssetEditor::draw(bool chinese)
{
    if (!m_visible)
    {
        m_focused = false;
        return;
    }
    update(ImGui::GetIO().DeltaTime);
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 initialSize{std::min(1040.0f, std::max(320.0f, viewport->WorkSize.x - 32.0f)),
                             std::min(740.0f, std::max(300.0f, viewport->WorkSize.y - 32.0f))};
    ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        {viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f},
        ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    bool visible = true;
    const auto flags = isDirty() ? ImGuiWindowFlags_UnsavedDocument : ImGuiWindowFlags_None;
    const bool expanded = ImGui::Begin(
        text(chinese, "Animation Clip###AnimationAssetEditor", "动画片段###AnimationAssetEditor"), &visible, flags);
    m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!visible)
    {
        if (isDirty())
        {
            m_closeRequested = true;
        }
        else
        {
            hide();
        }
    }
    if (expanded)
    {
        ImGui::TextWrapped("%s%s", m_path.c_str(), isDirty() ? " *" : "");
        if (ImGui::Button(text(chinese, "Save", "保存")))
        {
            save();
        }
        ImGui::SameLine();
        if (ImGui::Button(text(chinese, "Revert", "恢复磁盘版本")))
        {
            revert();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!canUndo());
        if (ImGui::Button(text(chinese, "Undo", "撤销")))
        {
            undo();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!canRedo());
        if (ImGui::Button(text(chinese, "Redo", "重做")))
        {
            redo();
        }
        ImGui::EndDisabled();
        if (m_focused && ImGui::GetIO().KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_S, false))
            {
                save();
            }
            if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Z, false))
            {
                if (ImGui::GetIO().KeyShift)
                {
                    redo();
                }
                else
                {
                    undo();
                }
            }
        }
        if (!m_error.empty())
        {
            ImGui::TextColored({1, 0.55f, 0.3f, 1}, "%s", translatedError(m_error, chinese).c_str());
        }
        ImGui::Separator();
        const float propertyWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.32f, 250.0f, 360.0f);
        ImGui::BeginChild("AnimationProperties", {propertyWidth, 0}, ImGuiChildFlags_Borders);
        drawProperties(chinese);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("AnimationPreviewAndEvents", {0, 0});
        drawPreview(chinese);
        drawEvents(chinese);
        ImGui::EndChild();
    }
    drawCloseDialog(chinese);
    ImGui::End();
}

void AnimationAssetEditor::drawProperties(bool chinese)
{
    std::string name = m_state.clip.name;
    if (inputText(text(chinese, "Name", "名称"), name))
    {
        setName(name);
    }
    bool looping = m_state.clip.looping;
    if (ImGui::Checkbox(text(chinese, "Clip loops", "片段循环"), &looping))
    {
        setLooping(looping);
    }
    ImGui::Text(text(chinese, "Duration: %.3f s", "时长：%.3f 秒"), m_state.clip.duration);
    ImGui::Text(text(chinese, "Bones: %zu", "骨骼数：%zu"), boneCount(m_state.clip));
    ImGui::TextUnformatted(m_state.clip.isCompressed() ? text(chinese, "Compressed curves", "压缩曲线")
                                                       : text(chinese, "Sampled poses", "采样姿势"));
    ImGui::BeginDisabled(m_state.clip.isCompressed());
    if (ImGui::Button(text(chinese, "Compress curves", "压缩曲线")))
    {
        compress();
    }
    ImGui::EndDisabled();
    ImGui::SeparatorText(text(chinese, "Preview assets", "预览资源"));
    if (m_registry)
    {
        auto assets = m_registry->GetAssets();
        std::sort(assets.begin(), assets.end(),
                  [](const AssetInfo& left, const AssetInfo& right) { return left.path < right.path; });
        const auto selector = [&](const char* label, const char* type, const std::string& selected, bool skeleton)
        {
            const auto filename = toUtf8GenericPath(fromUtf8Path(selected).filename());
            if (ImGui::BeginCombo(label, filename.empty() ? text(chinese, "Choose...", "选择资源…") : filename.c_str()))
            {
                for (const auto& asset : assets)
                {
                    if (asset.type != type || (!skeleton && asset.extension != ".mesh"))
                    {
                        continue;
                    }
                    if (ImGui::Selectable(asset.path.c_str(), asset.path == selected))
                    {
                        if (skeleton)
                        {
                            selectPreviewSkeleton(asset.id);
                        }
                        else
                        {
                            selectPreviewMesh(asset.id);
                        }
                    }
                }
                ImGui::EndCombo();
            }
        };
        selector(text(chinese, "Skeleton", "骨架"), "Skeleton", m_skeletonPath, true);
        selector(text(chinese, "Mesh", "网格"), "Mesh", m_meshPath, false);
    }
    if (!m_previewError.empty())
    {
        ImGui::TextWrapped("%s", translatedError(m_previewError, chinese).c_str());
    }
    ImGui::SeparatorText(text(chinese, "Bone hierarchy", "骨骼层级"));
    for (std::size_t index = 0; index < m_skeleton.bones.size(); ++index)
    {
        int depth = 0;
        for (int parent = m_skeleton.bones[index].parent; parent >= 0; parent = m_skeleton.bones[parent].parent)
        {
            ++depth;
        }
        ImGui::Indent(static_cast<float>(depth) * 10.0f);
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(m_skeleton.bones[index].name.c_str(), m_selectedBone == static_cast<int>(index)))
        {
            m_selectedBone = static_cast<int>(index);
        }
        ImGui::PopID();
        ImGui::Unindent(static_cast<float>(depth) * 10.0f);
    }
    if (m_selectedBone >= 0 && static_cast<std::size_t>(m_selectedBone) < m_pose.size())
    {
        ImGui::SeparatorText(text(chinese, "Local pose", "局部姿势"));
        const auto& pose = m_pose[m_selectedBone];
        ImGui::Text("T  %.3f, %.3f, %.3f", pose.position.x, pose.position.y, pose.position.z);
        ImGui::Text("R  %.1f, %.1f, %.1f", pose.rotation.x, pose.rotation.y, pose.rotation.z);
        ImGui::Text("S  %.3f, %.3f, %.3f", pose.scale.x, pose.scale.y, pose.scale.z);
    }
}

void AnimationAssetEditor::drawPreview(bool chinese)
{
    if (ImGui::Button(m_playing ? text(chinese, "Pause", "暂停") : text(chinese, "Play", "播放")))
    {
        if (m_playing)
        {
            pause();
        }
        else
        {
            play();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(text(chinese, "Reset", "复位")))
    {
        reset();
    }
    ImGui::SameLine();
    ImGui::Checkbox(text(chinese, "Preview loop", "循环预览"), &m_previewLoop);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(95);
    ImGui::SliderFloat(text(chinese, "Speed", "速率"), &m_speed, 0.05f, 4.0f, "%.2fx");
    float time = m_playhead;
    if (ImGui::SliderFloat(text(chinese, "Time", "时间"), &time, 0, m_state.clip.duration, "%.3f s"))
    {
        pause();
        scrub(time);
    }
    ImGui::Checkbox(text(chinese, "Mesh", "模型"), &m_showMesh);
    ImGui::SameLine();
    ImGui::Checkbox(text(chinese, "Bones", "骨架"), &m_showBones);
    ImGui::SameLine();
    if (ImGui::SmallButton(text(chinese, "Fit", "适配视图")))
    {
        m_zoom = 1;
        fitPreview();
    }
    const ImVec2 size{std::max(120.0f, ImGui::GetContentRegionAvail().x),
                      std::clamp(ImGui::GetContentRegionAvail().y * 0.57f, 180.0f, 420.0f)};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("AnimationCanvas", size, ImGuiButtonFlags_MouseButtonLeft);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        m_yaw += ImGui::GetIO().MouseDelta.x * 0.01f;
        m_pitch = std::clamp(m_pitch + ImGui::GetIO().MouseDelta.y * 0.01f, -1.5f, 1.5f);
    }
    if (ImGui::IsItemHovered())
    {
        m_zoom = std::clamp(m_zoom * std::pow(1.15f, ImGui::GetIO().MouseWheel), 0.1f, 10.0f);
    }
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 end{origin.x + size.x, origin.y + size.y};
    draw->AddRectFilled(origin, end, IM_COL32(22, 26, 34, 255));
    draw->PushClipRect(origin, end, true);
    const float scale = std::min(size.x, size.y) * 0.4f * m_zoom / m_previewRadius;
    const Vec3 right{std::cos(m_yaw), 0, -std::sin(m_yaw)};
    const Vec3 up{std::sin(m_yaw) * std::sin(m_pitch), std::cos(m_pitch), std::cos(m_yaw) * std::sin(m_pitch)};
    const Vec3 view{std::sin(m_yaw) * std::cos(m_pitch), -std::sin(m_pitch), std::cos(m_yaw) * std::cos(m_pitch)};
    const auto project = [&](const Vec3& point)
    {
        const Vec3 relative = point - m_previewCenter;
        return ImVec2{origin.x + size.x * 0.5f + dot(relative, right) * scale,
                      origin.y + size.y * 0.5f - dot(relative, up) * scale};
    };
    if (m_showMesh)
    {
        std::vector<std::pair<float, std::size_t>> triangles;
        for (std::size_t index = 0; index + 2 < m_previewIndices.size(); index += 3)
        {
            const auto a = m_previewIndices[index], b = m_previewIndices[index + 1], c = m_previewIndices[index + 2];
            if (a >= m_previewVertices.size() || b >= m_previewVertices.size() || c >= m_previewVertices.size())
            {
                continue;
            }
            triangles.emplace_back(
                dot((m_previewVertices[a] + m_previewVertices[b] + m_previewVertices[c]) / 3.0f, view), index);
        }
        std::sort(triangles.begin(), triangles.end());
        for (const auto& triangle : triangles)
        {
            const auto index = triangle.second;
            const auto& a = m_previewVertices[m_previewIndices[index]];
            const auto& b = m_previewVertices[m_previewIndices[index + 1]];
            const auto& c = m_previewVertices[m_previewIndices[index + 2]];
            const auto normal = cross(b - a, c - a);
            const float light = 0.45f + 0.5f * std::abs(dot(normalize(normal), normalize(Vec3{0.3f, 0.8f, 0.6f})));
            draw->AddTriangleFilled(project(a), project(b), project(c),
                                    IM_COL32(static_cast<int>(90 * light), static_cast<int>(175 * light),
                                             static_cast<int>(220 * light), 255));
        }
    }
    if (m_showBones)
    {
        for (const auto& bone : m_snapshot.bones)
        {
            draw->AddLine(project(bone.from), project(bone.to), IM_COL32(255, 190, 75, 255), 2);
        }
        for (std::size_t index = 0; index < m_snapshot.jointPositions.size(); ++index)
        {
            const auto point = project(m_snapshot.jointPositions[index]);
            draw->AddCircleFilled(point, m_selectedBone == static_cast<int>(index) ? 5.0f : 3.0f,
                                  IM_COL32(255, 220, 120, 255));
        }
    }
    draw->AddText({origin.x + 8, origin.y + 8}, IM_COL32(190, 195, 205, 255),
                  text(chinese, "Drag to orbit | Wheel to zoom", "拖动旋转 | 滚轮缩放"));
    draw->PopClipRect();
}

void AnimationAssetEditor::drawEvents(bool chinese)
{
    ImGui::SeparatorText(text(chinese, "Animation events", "动画事件"));
    if (ImGui::Button(text(chinese, "Add at playhead", "在当前时间添加")))
    {
        addEvent({m_playhead, "Event", {}});
    }
    int remove = -1;
    for (std::size_t index = 0; index < m_state.clip.events.size(); ++index)
    {
        auto edited = m_state.clip.events[index];
        bool changed = false;
        ImGui::PushID(static_cast<int>(index));
        ImGui::SetNextItemWidth(95);
        changed |=
            ImGui::DragFloat(text(chinese, "Time", "时间"), &edited.time, 0.01f, 0, m_state.clip.duration, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton(text(chinese, "Go", "定位")))
        {
            pause();
            scrub(edited.time);
        }
        ImGui::SetNextItemWidth(150);
        changed |= inputText(text(chinese, "Event", "事件"), edited.name);
        ImGui::SetNextItemWidth(-50);
        changed |= inputText(text(chinese, "Payload", "参数"), edited.payload);
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
        {
            remove = static_cast<int>(index);
        }
        if (changed)
        {
            updateEvent(index, edited);
        }
        ImGui::PopID();
        ImGui::Separator();
    }
    if (remove >= 0)
    {
        removeEvent(static_cast<std::size_t>(remove));
    }
}

void AnimationAssetEditor::drawCloseDialog(bool chinese)
{
    const char* title = text(chinese, "Unsaved clip###AnimationClose", "片段尚未保存###AnimationClose");
    if (m_closeRequested)
    {
        ImGui::OpenPopup(title);
        m_closeRequested = false;
    }
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(text(chinese, "Save changes before closing the clip?", "关闭片段前保存修改？"));
        if (ImGui::Button(text(chinese, "Save and close", "保存并关闭")) && save())
        {
            hide();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(text(chinese, "Discard and close", "放弃并关闭")))
        {
            discard();
            hide();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(text(chinese, "Cancel", "取消")))
        {
            ImGui::CloseCurrentPopup();
        }
        if (!m_error.empty())
        {
            ImGui::TextWrapped("%s", translatedError(m_error, chinese).c_str());
        }
        ImGui::EndPopup();
    }
}

std::string AnimationAssetEditor::translatedError(const std::string& message, bool chinese) const
{
    if (!chinese)
    {
        return message;
    }
    const std::pair<const char*, const char*> translations[] = {
        {"Save or revert the current clip before switching assets.", "请先保存或恢复当前片段，再打开其他动画。"},
        {"Events require a name and a finite time between zero and clip duration.",
         "事件名称不能为空，时间必须在片段时长内且为有限值。"},
        {"Clip name must not be empty.", "片段名称不能为空。"},
        {"Choose a skeleton to preview this clip.", "请选择骨架以预览此片段。"},
        {"Preview skeleton bone count does not match the clip.", "预览骨架的骨骼数量与片段不匹配。"},
        {"Invalid animation clip.", "动画片段数据无效。"}};
    for (const auto& entry : translations)
    {
        if (message == entry.first)
        {
            return entry.second;
        }
    }
    return message;
}

} // namespace tinyengine
