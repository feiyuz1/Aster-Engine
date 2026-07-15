#include "MiniEngine/Animation/SkeletalAnimation.hpp"

#include <algorithm>
#include <cmath>

namespace MiniEngine
{
namespace
{
Vec3 Lerp(const Vec3 &left, const Vec3 &right, const float alpha)
{
    return left * (1.0f - alpha) + right * alpha;
}

BonePose Lerp(const BonePose &left, const BonePose &right, const float alpha)
{
    return {Lerp(left.position, right.position, alpha), Lerp(left.rotation, right.rotation, alpha), Lerp(left.scale, right.scale, alpha)};
}
} // namespace

bool Skeleton::IsValid() const
{
    for (std::size_t index = 0; index < bones.size(); ++index)
        if (bones[index].name.empty() || bones[index].parent >= static_cast<int>(index) || bones[index].parent < -1)
            return false;
    return !bones.empty();
}

int Skeleton::FindBone(const std::string &name) const
{
    for (std::size_t index = 0; index < bones.size(); ++index)
        if (bones[index].name == name)
            return static_cast<int>(index);
    return -1;
}

bool SkeletalClip::IsValid(const std::size_t boneCount) const
{
    if (duration <= 0.0f || keyframes.empty())
        return false;
    float previous = -1.0f;
    for (const auto &keyframe : keyframes)
    {
        if (keyframe.time < previous || keyframe.time < 0.0f || keyframe.time > duration || keyframe.localPose.size() != boneCount)
            return false;
        previous = keyframe.time;
    }
    return true;
}

std::vector<BonePose> SkeletalClip::Sample(float time, const std::size_t boneCount) const
{
    if (!IsValid(boneCount))
        return std::vector<BonePose>(boneCount);
    if (looping)
        time = std::fmod(std::max(0.0f, time), duration);
    else
        time = std::clamp(time, 0.0f, duration);
    const auto upper = std::upper_bound(keyframes.begin(), keyframes.end(), time,
                                        [](const float value, const SkeletalKeyframe &key) { return value < key.time; });
    if (upper == keyframes.begin())
        return upper->localPose;
    if (upper == keyframes.end())
        return keyframes.back().localPose;
    const auto &right = *upper;
    const auto &left = *(upper - 1);
    const float alpha = right.time > left.time ? (time - left.time) / (right.time - left.time) : 0.0f;
    std::vector<BonePose> result(boneCount);
    for (std::size_t index = 0; index < boneCount; ++index)
        result[index] = Lerp(left.localPose[index], right.localPose[index], alpha);
    return result;
}

std::vector<Mat4> BuildSkinMatrices(const Skeleton &skeleton, const std::vector<BonePose> &localPose)
{
    if (!skeleton.IsValid() || localPose.size() != skeleton.bones.size())
        return {};
    std::vector<Mat4> world(skeleton.bones.size()), skin(skeleton.bones.size());
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
    {
        const auto &pose = localPose[index];
        const Mat4 local = TransformMatrix(pose.position, pose.rotation, pose.scale);
        const int parent = skeleton.bones[index].parent;
        world[index] = parent >= 0 ? world[static_cast<std::size_t>(parent)] * local : local;
        skin[index] = world[index] * skeleton.bones[index].inverseBind;
    }
    return skin;
}

bool AnimationStateMachine::AddState(std::string name, std::shared_ptr<SkeletalClip> clip)
{
    if (name.empty() || !clip)
        return false;
    const bool inserted = m_states.emplace(name, std::move(clip)).second;
    if (inserted && m_state.empty())
        m_state = std::move(name);
    return inserted;
}

bool AnimationStateMachine::AddTransition(std::string from, std::string to, const float blendSeconds, Condition condition)
{
    if (!m_states.count(from) || !m_states.count(to) || !condition)
        return false;
    m_transitions.push_back({std::move(from), std::move(to), std::max(0.0f, blendSeconds), std::move(condition)});
    return true;
}

bool AnimationStateMachine::SetState(const std::string &name)
{
    if (!m_states.count(name))
        return false;
    m_previousState.clear();
    m_state = name;
    m_time = 0.0f;
    return true;
}

std::vector<BonePose> AnimationStateMachine::Update(const float deltaTime, const std::size_t boneCount)
{
    if (m_state.empty())
        return std::vector<BonePose>(boneCount);
    for (const auto &transition : m_transitions)
        if (transition.from == m_state && transition.condition())
        {
            m_previousState = m_state;
            m_previousTime = m_time;
            m_state = transition.to;
            m_time = 0.0f;
            m_blendDuration = transition.blendSeconds;
            m_blendTime = 0.0f;
            break;
        }
    m_time += std::max(0.0f, deltaTime);
    auto pose = m_states.at(m_state)->Sample(m_time, boneCount);
    if (!m_previousState.empty() && m_blendDuration > 0.0f)
    {
        m_previousTime += std::max(0.0f, deltaTime);
        m_blendTime += std::max(0.0f, deltaTime);
        const auto previous = m_states.at(m_previousState)->Sample(m_previousTime, boneCount);
        const float alpha = std::clamp(m_blendTime / m_blendDuration, 0.0f, 1.0f);
        for (std::size_t index = 0; index < pose.size(); ++index)
            pose[index] = Lerp(previous[index], pose[index], alpha);
        if (alpha >= 1.0f)
            m_previousState.clear();
    }
    return pose;
}

const std::string &AnimationStateMachine::GetState() const
{
    return m_state;
}
} // namespace MiniEngine
