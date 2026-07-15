#pragma once

#include "MiniEngine/Animation/SkeletalAnimation.hpp"
#include "MiniEngine/ECS/ECS.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
class MovementSystem final : public System
{
  public:
    void Update(World &world, float deltaTime) override;
};

class AnimationSystem final : public System
{
  public:
    using EventCallback = std::function<void(Entity, const AnimationEvent &)>;
    void Update(World &world, float deltaTime) override;
    void SetEventCallback(EventCallback callback);

  private:
    struct Keyframe
    {
        float time = 0.0f;
        Vec3 position;
        Vec3 rotation;
        Vec3 scale{1.0f, 1.0f, 1.0f};
    };
    struct Clip
    {
        float duration = 0.0f;
        std::vector<Keyframe> keys;
    };
    const Clip *LoadClip(const std::string &path);
    const SkeletalClip *LoadSkeletalClip(const std::string &path);
    const BlendTree1D *LoadBlendTree(const std::string &path);
    std::shared_ptr<const AnimationGraph> LoadGraph(const std::string &path);
    std::unordered_map<std::string, Clip> m_clips;
    std::unordered_map<std::string, Skeleton> m_skeletons;
    std::unordered_map<std::string, SkeletalClip> m_skeletalClips;
    std::unordered_map<std::string, BlendTree1D> m_blendTrees;
    std::unordered_map<std::string, std::shared_ptr<AnimationGraph>> m_graphs;
    std::unordered_map<Entity, AnimationGraphPlayer> m_graphPlayers;
    std::unordered_map<Entity, std::string> m_playerGraphs;
    EventCallback m_eventCallback;
};
} // namespace MiniEngine
