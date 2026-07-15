#pragma once

#include "MiniEngine/Animation/SkeletalAnimation.hpp"
#include "MiniEngine/ECS/ECS.hpp"

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
    void Update(World &world, float deltaTime) override;

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
    std::unordered_map<std::string, Clip> m_clips;
    std::unordered_map<std::string, Skeleton> m_skeletons;
    std::unordered_map<std::string, SkeletalClip> m_skeletalClips;
};
} // namespace MiniEngine
