#pragma once

#include "MiniEngine/Math/Math.hpp"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
struct Bone
{
    std::string name;
    int parent = -1;
    Mat4 inverseBind = Mat4::Identity();
};

struct Skeleton
{
    std::vector<Bone> bones;
    bool IsValid() const;
    int FindBone(const std::string &name) const;
};

struct BonePose
{
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct SkeletalKeyframe
{
    float time = 0.0f;
    std::vector<BonePose> localPose;
};

class SkeletalClip
{
  public:
    std::string name;
    float duration = 0.0f;
    bool looping = true;
    std::vector<SkeletalKeyframe> keyframes;

    bool IsValid(std::size_t boneCount) const;
    std::vector<BonePose> Sample(float time, std::size_t boneCount) const;
};

std::vector<Mat4> BuildSkinMatrices(const Skeleton &skeleton, const std::vector<BonePose> &localPose);

class AnimationStateMachine
{
  public:
    using Condition = std::function<bool()>;
    bool AddState(std::string name, std::shared_ptr<SkeletalClip> clip);
    bool AddTransition(std::string from, std::string to, float blendSeconds, Condition condition);
    bool SetState(const std::string &name);
    std::vector<BonePose> Update(float deltaTime, std::size_t boneCount);
    const std::string &GetState() const;

  private:
    struct Transition
    {
        std::string from;
        std::string to;
        float blendSeconds = 0.0f;
        Condition condition;
    };
    std::unordered_map<std::string, std::shared_ptr<SkeletalClip>> m_states;
    std::vector<Transition> m_transitions;
    std::string m_state;
    std::string m_previousState;
    float m_time = 0.0f;
    float m_previousTime = 0.0f;
    float m_blendDuration = 0.0f;
    float m_blendTime = 0.0f;
};
} // namespace MiniEngine
