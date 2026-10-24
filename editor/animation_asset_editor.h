#pragma once

#include "tinyengine/animation/skeletal_animation.h"
#include "tinyengine/resource/asset_registry.h"
#include "tinyengine/resource/resource.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tinyengine
{
// CPU preview and document state are independent of the ImGui window and graphics device.
class AnimationAssetEditor
{
  public:
    bool open(const std::string& path, AssetRegistry* registry = nullptr);
    void draw(bool chinese = false);
    bool save();
    bool revert();
    void discard();
    std::vector<std::string> retainedAssetPaths() const;
    bool undo();
    bool redo();
    bool setName(const std::string& name);
    bool setLooping(bool looping);
    bool addEvent(const AnimationEvent& event);
    bool updateEvent(std::size_t index, const AnimationEvent& event);
    bool removeEvent(std::size_t index);
    bool compress();
    bool selectPreviewSkeleton(const std::string& reference);
    bool selectPreviewMesh(const std::string& reference);

    void play();
    void pause();
    void reset();
    void scrub(float time);
    void update(float deltaTime);
    void setPlaybackSpeed(float speed);
    void setPreviewLoop(bool loop);
    void hide();
    bool isDirty() const
    {
        return m_state.revision != m_saved.revision;
    }
    bool isVisible() const
    {
        return m_visible;
    }
    bool isFocused() const
    {
        return m_visible && m_focused;
    }
    bool isPlaying() const
    {
        return m_playing;
    }
    bool canUndo() const
    {
        return !m_undo.empty();
    }
    bool canRedo() const
    {
        return !m_redo.empty();
    }
    float playhead() const
    {
        return m_playhead;
    }
    float playbackSpeed() const
    {
        return m_speed;
    }
    bool previewLoop() const
    {
        return m_previewLoop;
    }
    const std::string& path() const
    {
        return m_path;
    }
    const std::string& error() const
    {
        return m_error;
    }
    const std::string& previewError() const
    {
        return m_previewError;
    }
    const SkeletalClip& clip() const
    {
        return m_state.clip;
    }
    const Skeleton& skeleton() const
    {
        return m_skeleton;
    }
    const std::string& skeletonPath() const
    {
        return m_skeletonPath;
    }
    const std::string& meshPath() const
    {
        return m_meshPath;
    }
    const std::vector<BonePose>& localPose() const
    {
        return m_pose;
    }
    const AnimationDebugSnapshot& debugSnapshot() const
    {
        return m_snapshot;
    }
    const std::vector<Vec3>& previewVertices() const
    {
        return m_previewVertices;
    }
    float previewRadius() const
    {
        return m_previewRadius;
    }

  private:
    struct DocumentState
    {
        SkeletalClip clip;
        std::string skeletonReference;
        std::string meshReference;
        std::uint64_t revision = 0;
    };
    void checkpoint();
    bool validateEvent(const AnimationEvent& event);
    bool validateClip(const SkeletalClip& clip);
    void resolvePreview();
    void refreshPose();
    void fitPreview();
    void prepareMesh();
    std::string findPreviewReference(const char* setting, const char* type) const;
    std::optional<AssetInfo> findAsset(const std::string& reference) const;
    void drawPreview(bool chinese);
    void drawProperties(bool chinese);
    void drawEvents(bool chinese);
    void drawCloseDialog(bool chinese);
    std::string translatedError(const std::string& message, bool chinese) const;

    AssetRegistry* m_registry = nullptr;
    DocumentState m_state, m_saved;
    std::vector<DocumentState> m_undo, m_redo;
    std::uint64_t m_nextRevision = 0;
    std::string m_path, m_error, m_previewError, m_skeletonPath, m_meshPath;
    std::unordered_map<std::string, Skeleton> m_skeletonCache;
    std::unordered_map<std::string, std::shared_ptr<MeshResource>> m_meshCache;
    Skeleton m_skeleton;
    std::shared_ptr<MeshResource> m_mesh;
    std::vector<MeshVertex> m_previewSourceVertices;
    std::vector<std::uint32_t> m_previewIndices;
    std::vector<Vec3> m_previewVertices;
    std::vector<BonePose> m_pose;
    AnimationDebugSnapshot m_snapshot;
    Vec3 m_previewCenter{};
    float m_previewRadius = 1;
    float m_yaw = 0.6f, m_pitch = 0.25f, m_zoom = 1;
    float m_playhead = 0, m_speed = 1;
    int m_selectedBone = -1;
    bool m_playing = false, m_previewLoop = true, m_visible = false, m_focused = false;
    bool m_closeRequested = false, m_showMesh = true, m_showBones = true;
};
} // namespace tinyengine
