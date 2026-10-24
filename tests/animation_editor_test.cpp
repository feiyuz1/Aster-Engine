#if !__has_include("animation_asset_editor.h")
#include <iostream>
int main()
{
    std::cerr << "FAIL: AnimationAssetEditor is not implemented\n";
    return 1;
}
#else
#include "animation_asset_editor.h"
#include "tinyengine/platform/platform.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <imgui.h>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace tinyengine;
namespace
{
int checks = 0;
void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
    ++checks;
    std::cout << "PASS: " << message << '\n';
}
bool near(float left, float right)
{
    return std::abs(left - right) < 0.0001f;
}
struct Fixture
{
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("tinyengine-animation-editor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string clipPath;
    std::string otherPath;
    std::string skeletonPath;
    std::string meshPath;
    AssetRegistry registry;
    Fixture()
    {
        std::filesystem::create_directories(root);
        clipPath = toUtf8GenericPath(root / "walk.skanim");
        otherPath = toUtf8GenericPath(root / "other.skanim");
        skeletonPath = toUtf8GenericPath(root / "actor.skeleton");
        meshPath = toUtf8GenericPath(root / "actor.mesh");
        Skeleton skeleton;
        skeleton.bones = {{"Root", -1, Mat4::identity()}, {"Hand", 0, translationMatrix({0, -1, 0})}};
        check(saveSkeletonAsset(skeleton, fromUtf8Path(skeletonPath)), "fixture skeleton saves");
        SkeletalClip clip;
        clip.name = "Walk";
        clip.duration = 2;
        clip.looping = true;
        clip.keyframes = {{0, {{{0, 0, 0}}, {{0, 1, 0}}}}, {2, {{{2, 0, 0}}, {{0, 2, 0}}}}};
        check(saveSkeletalClipAsset(clip, fromUtf8Path(clipPath)), "fixture clip saves");
        clip.name = "Other";
        clip.duration = 4;
        clip.keyframes.back().time = 4;
        check(saveSkeletalClipAsset(clip, fromUtf8Path(otherPath)), "second fixture clip saves");
        check(PlatformFile::writeAllText(fromUtf8Path(meshPath), "v 0 1 0\nv 1 1 0\nv 0 2 0\nvw 1 1 1 0 0 0 0 0 0\nvw "
                                                                 "2 1 1 0 0 0 0 0 0\nvw 3 1 1 0 0 0 0 0 0\nf 1 2 3\n"),
              "fixture weighted mesh saves");
        registry.scan(root);
        check(registry.setImportSetting(clipPath, "preview.skeleton", registry.Find(skeletonPath)->id),
              "skeleton GUID recorded");
        check(registry.setImportSetting(clipPath, "preview.mesh", registry.Find(meshPath)->id), "mesh GUID recorded");
    }
    ~Fixture()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
void loadingAndPlayback(Fixture& fixture)
{
    AnimationAssetEditor editor;
    check(editor.open(fixture.clipPath, &fixture.registry), "real clip opens through registry");
    check(editor.skeleton().bones.size() == 2 && editor.skeletonPath() == fixture.skeletonPath,
          "preview resolves differently named skeleton by metadata GUID");
    check(editor.meshPath() == fixture.meshPath && editor.previewVertices().size() == 3,
          "preview resolves and loads weighted mesh by metadata GUID");
    editor.scrub(1);
    check(near(editor.previewVertices()[0].x, 1) && near(editor.previewVertices()[0].y, 1.5f),
          "CPU preview uses skin palette and inverse bind matrix");
    const float radius = editor.previewRadius();
    editor.play();
    editor.update(0.5f);
    check(near(editor.playhead(), 1.5f), "playback advances by elapsed seconds");
    editor.pause();
    editor.update(0.5f);
    check(near(editor.playhead(), 1.5f), "pause retains playhead");
    editor.setPlaybackSpeed(2);
    editor.play();
    editor.update(0.5f);
    check(near(editor.playhead(), 0.5f) && editor.isPlaying(), "loop wraps with playback speed");
    check(near(editor.previewRadius(), radius), "preview framing remains stable while animation moves");
    editor.setPreviewLoop(false);
    editor.update(4);
    check(near(editor.playhead(), 2) && !editor.isPlaying(), "non-looping playback stops exactly at duration");
    editor.play();
    check(near(editor.playhead(), 0), "play at clip end restarts");
    editor.scrub(std::numeric_limits<float>::infinity());
    editor.update(std::numeric_limits<float>::quiet_NaN());
    check(std::isfinite(editor.playhead()), "nonfinite time inputs cannot corrupt playback");
    editor.reset();
    check(near(editor.playhead(), 0) && !editor.isPlaying(), "reset stops and rewinds");
    check(!editor.open(toUtf8GenericPath(fixture.root / "missing.skanim"), &fixture.registry) &&
              editor.path() == fixture.clipPath && !editor.error().empty(),
          "failed open preserves current document and reports error");
    check(editor.open(fixture.otherPath, &fixture.registry) && near(editor.playhead(), 0) && !editor.isPlaying(),
          "asset switch resets preview state");
    AnimationAssetEditor second;
    check(second.open(fixture.clipPath, &fixture.registry), "independent preview opens");
    second.scrub(1);
    check(near(editor.playhead(), 0), "preview time is isolated per editor instance");
}
void editsAndFailures(Fixture& fixture)
{
    AnimationAssetEditor editor;
    check(editor.open(fixture.clipPath, &fixture.registry), "editable clip opens");
    check(editor.setName("Walk edited") && editor.setLooping(false), "name and loop setting are editable");
    check(editor.addEvent({1.5f, "Step", "right"}) && editor.addEvent({0.25f, "Step", "left"}), "events are editable");
    check(editor.isDirty(), "edits mark document dirty");
    check(!editor.open(fixture.otherPath, &fixture.registry) && editor.path() == fixture.clipPath && editor.isDirty(),
          "dirty switch retains unsaved draft");
    check(!editor.addEvent({std::numeric_limits<float>::quiet_NaN(), "Bad", {}}) && !editor.addEvent({3, "Bad", {}}) &&
              !editor.addEvent({0, "", {}}),
          "event times and names are validated");
    check(editor.clip().events.size() == 2, "invalid event changes leave draft intact");
    check(editor.updateEvent(0, {1, "Footstep", "updated"}), "existing event can be edited");
    check(editor.undo() && near(editor.clip().events[0].time, 1.5f), "undo restores event data");
    check(editor.redo() && near(editor.clip().events[0].time, 1), "redo reapplies event data");
    check(editor.save() && !editor.isDirty(), "clip saves and clears dirty state");
    SkeletalClip saved;
    check(loadSkeletalClipAsset(fromUtf8Path(fixture.clipPath), saved) && saved.name == "Walk edited" &&
              !saved.looping && saved.events.size() == 2 && near(saved.events[0].time, 0.25f),
          "save persists metadata and sorted events");
    check(editor.removeEvent(0) && editor.undo() && !editor.isDirty(), "undo back to saved state clears dirty flag");
    check(editor.setName("Unsaved") && editor.revert() && editor.clip().name == "Walk edited" && !editor.isDirty(),
          "revert reloads saved clip");
    check(editor.setName("Keep on failure"), "failure fixture has unsaved edit");
    std::filesystem::remove(fromUtf8Path(fixture.clipPath));
    std::filesystem::create_directory(fromUtf8Path(fixture.clipPath));
    check(!editor.save() && editor.isDirty() && !editor.error().empty(), "failed save retains draft and visible error");
    check(!editor.revert() && editor.isDirty() && editor.clip().name == "Keep on failure",
          "failed revert preserves draft");
    std::filesystem::remove(fromUtf8Path(fixture.clipPath));
    check(saveSkeletalClipAsset(saved, fromUtf8Path(fixture.clipPath)), "restore fixture clip");
}
void dependencyAndLegacyResolution(Fixture& fixture)
{
    const auto dependencyPath = toUtf8GenericPath(fixture.root / "dependency.skanim");
    SkeletalClip source;
    check(loadSkeletalClipAsset(fromUtf8Path(fixture.clipPath), source) &&
              saveSkeletalClipAsset(source, fromUtf8Path(dependencyPath)),
          "dependency fixture saves");
    fixture.registry.scan(fixture.root);
    check(fixture.registry.setDependencies(dependencyPath, {fixture.registry.Find(fixture.skeletonPath)->id,
                                                            fixture.registry.Find(fixture.meshPath)->id}),
          "typed GUID dependencies save");
    AnimationAssetEditor editor;
    check(editor.open(dependencyPath, &fixture.registry) && editor.skeleton().bones.size() == 2 &&
              editor.previewVertices().size() == 3,
          "typed dependencies resolve skeleton and mesh");
    check(!editor.selectPreviewSkeleton(fixture.registry.Find(fixture.meshPath)->id),
          "typed skeleton selector rejects mesh GUID");
    const auto legacyPath = fixture.root / "legacy.skanim";
    check(saveSkeletalClipAsset(source, legacyPath) &&
              saveSkeletonAsset(editor.skeleton(), fixture.root / "legacy.skeleton"),
          "legacy fixture saves");
    check(editor.open(toUtf8GenericPath(legacyPath)) && editor.skeleton().bones.size() == 2,
          "legacy same-name skeleton preview remains supported without registry");
}
void previewMetadataPersistence(Fixture& fixture)
{
    const auto alternatePath = toUtf8GenericPath(fixture.root / "alternate.skeleton");
    Skeleton skeleton;
    check(loadSkeletonAsset(fromUtf8Path(fixture.skeletonPath), skeleton) &&
              saveSkeletonAsset(skeleton, fromUtf8Path(alternatePath)),
          "alternate preview rig saves");
    fixture.registry.scan(fixture.root);
    AnimationAssetEditor editor;
    check(editor.open(fixture.clipPath, &fixture.registry), "preview edit document opens");
    const auto alternative = fixture.registry.Find(alternatePath)->id;
    check(editor.selectPreviewSkeleton(alternative) && editor.save(), "new preview rig choice saves");
    const auto saved = fixture.registry.Find(fixture.clipPath);
    check(saved && std::find(saved->dependencies.begin(), saved->dependencies.end(), alternative) !=
                       saved->dependencies.end(),
          "preview selection persists a tracked dependency as well as metadata");
    check(editor.selectPreviewSkeleton(fixture.registry.Find(fixture.skeletonPath)->id) && editor.save(),
          "original preview rig can be restored");
}

void drawSmoke(Fixture& fixture)
{
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1600, 1000};
    io.DeltaTime = 1.f / 60;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    AnimationAssetEditor editor;
    check(editor.open(fixture.clipPath, &fixture.registry), "draw fixture opens");
    for (bool chinese : {false, true})
    {
        ImGui::NewFrame();
        editor.draw(chinese);
        ImGui::Render();
        check(ImGui::GetDrawData()->TotalVtxCount > 0, "editor preview produces ImGui geometry");
    }
    ImGui::DestroyContext();
}
} // namespace
int main()
{
    try
    {
        Fixture fixture;
        loadingAndPlayback(fixture);
        editsAndFailures(fixture);
        dependencyAndLegacyResolution(fixture);
        previewMetadataPersistence(fixture);
        drawSmoke(fixture);
        std::cout << checks << " animation editor checks passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "FAIL: " << exception.what() << '\n';
        return 1;
    }
}
#endif
