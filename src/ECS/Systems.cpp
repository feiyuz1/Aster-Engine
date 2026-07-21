// 文件：src/ECS/Systems.cpp
// 作用：声明变换、动画、物理和脚本等 ECS 系统。
// 约定：实体句柄包含代际信息，禁止长期保存未校验的裸实体编号。
// 类型：实现文件。

#include "MiniEngine/ECS/Systems.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace MiniEngine
{
void MovementSystem::Update(World &world, const float deltaTime)
{
    auto &components = world.GetComponentManager();
    components.ForEach<VelocityComponent>([&components, deltaTime](const Entity entity, VelocityComponent &velocity) {
        TransformComponent *transform = components.Get<TransformComponent>(entity);
        if (transform == nullptr)
        {
            return;
        }

        transform->position.x += velocity.linear.x * deltaTime;
        transform->position.y += velocity.linear.y * deltaTime;
        transform->position.z += velocity.linear.z * deltaTime;
    });
}

const AnimationSystem::Clip *AnimationSystem::LoadClip(const std::string &path)
{
    const auto found = m_clips.find(path);
    if (found != m_clips.end())
        return &found->second;
    Clip clip;
    std::istringstream stream(PlatformFile::ReadAllText(path));
    std::string token;
    while (stream >> token)
    {
        if (token == "duration")
            stream >> clip.duration;
        else if (token == "key")
        {
            Keyframe key;
            stream >> key.time >> key.position.x >> key.position.y >> key.position.z >> key.rotation.x >> key.rotation.y >>
                key.rotation.z >> key.scale.x >> key.scale.y >> key.scale.z;
            if (stream)
                clip.keys.push_back(key);
        }
        else
        {
            std::string ignored;
            std::getline(stream, ignored);
        }
    }
    std::sort(clip.keys.begin(), clip.keys.end(), [](const Keyframe &lhs, const Keyframe &rhs) { return lhs.time < rhs.time; });
    if (clip.duration <= 0.0f && !clip.keys.empty())
        clip.duration = clip.keys.back().time;
    return &m_clips.emplace(path, std::move(clip)).first->second;
}

const SkeletalClip *AnimationSystem::LoadSkeletalClip(const std::string &path)
{
    const auto found = m_skeletalClips.find(path);
    if (found != m_skeletalClips.end())
        return &found->second;
    SkeletalClip clip;
    if (!LoadSkeletalClipAsset(path, clip))
        return nullptr;
    return &m_skeletalClips.emplace(path, std::move(clip)).first->second;
}

const BlendTree1D *AnimationSystem::LoadBlendTree(const std::string &path)
{
    const auto found = m_blendTrees.find(path);
    if (found != m_blendTrees.end())
        return &found->second;
    BlendTree1D tree;
    if (!LoadBlendTreeAsset(path, tree))
        return nullptr;
    return &m_blendTrees.emplace(path, std::move(tree)).first->second;
}

std::shared_ptr<const AnimationGraph> AnimationSystem::LoadGraph(const std::string &path)
{
    const auto found = m_graphs.find(path);
    if (found != m_graphs.end())
        return found->second;
    auto graph = std::make_shared<AnimationGraph>();
    if (!LoadAnimationGraphAsset(path, *graph))
        return {};
    m_graphs[path] = graph;
    return graph;
}

void AnimationSystem::SetEventCallback(EventCallback callback)
{
    m_eventCallback = std::move(callback);
}

void AnimationSystem::Update(World &world, const float deltaTime)
{
    world.GetComponentManager().ForEach<TransformComponent, AnimationComponent>([this, deltaTime](const Entity entity,
                                                                                                  TransformComponent &transform,
                                                                                                  AnimationComponent &animation) {
        animation.firedEvents.clear();
        if (!animation.playing || (animation.clipPath.empty() && animation.graphPath.empty()))
            return;
        if (!animation.skeletonPath.empty())
        {
            auto skeleton = m_skeletons.find(animation.skeletonPath);
            if (skeleton == m_skeletons.end())
            {
                Skeleton loaded;
                if (!LoadSkeletonAsset(animation.skeletonPath, loaded))
                    return;
                skeleton = m_skeletons.emplace(animation.skeletonPath, std::move(loaded)).first;
            }
            const auto clipResolver = [this](const std::string &path) { return LoadSkeletalClip(path); };
            const auto motionSampler = [this, &animation, &clipResolver](const std::string &path, const float time,
                                                                         const std::size_t boneCount) {
                if (std::filesystem::path(path).extension() == ".blendtree")
                {
                    const BlendTree1D *tree = LoadBlendTree(path);
                    return tree ? tree->Sample(animation.graphParameterValue, time, boneCount, clipResolver)
                                : std::vector<BonePose>(boneCount);
                }
                const SkeletalClip *clip = LoadSkeletalClip(path);
                return clip ? clip->Sample(time, boneCount) : std::vector<BonePose>(boneCount);
            };
            std::vector<BonePose> pose;
            const float previousTime = animation.time;
            float eventPreviousTime = previousTime;
            std::string eventClipPath;
            if (!animation.graphPath.empty())
            {
                const auto graph = LoadGraph(animation.graphPath);
                if (!graph)
                    return;
                if (m_playerGraphs[entity] != animation.graphPath)
                {
                    if (!m_graphPlayers[entity].SetGraph(graph))
                        return;
                    m_playerGraphs[entity] = animation.graphPath;
                }
                auto &player = m_graphPlayers[entity];
                player.SetParameter(animation.graphParameter, animation.graphParameterValue);
                const std::string previousState = player.GetState();
                eventPreviousTime = player.GetTime();
                pose = player.Update(std::max(0.0f, deltaTime * animation.speed), skeleton->second.bones.size(), motionSampler);
                animation.graphState = player.GetState();
                animation.time = player.GetTime();
                if (animation.graphState != previousState)
                    eventPreviousTime = 0.0f;
                const AnimationGraphState *state = graph->FindState(animation.graphState);
                if (state)
                {
                    eventClipPath = state->motionPath;
                    if (std::filesystem::path(eventClipPath).extension() == ".blendtree")
                        if (const BlendTree1D *tree = LoadBlendTree(eventClipPath))
                            eventClipPath = tree->DominantClip(animation.graphParameterValue);
                }
            }
            else if (std::filesystem::path(animation.clipPath).extension() == ".blendtree")
            {
                const BlendTree1D *tree = LoadBlendTree(animation.clipPath);
                if (!tree)
                    return;
                animation.time += deltaTime * animation.speed;
                pose = tree->Sample(animation.graphParameterValue, animation.time, skeleton->second.bones.size(), clipResolver);
                eventClipPath = tree->DominantClip(animation.graphParameterValue);
            }
            else
            {
                const SkeletalClip *skeletalClip = LoadSkeletalClip(animation.clipPath);
                if (!skeletalClip)
                    return;
                const float unwrappedTime = animation.time + deltaTime * animation.speed;
                for (const auto &event : skeletalClip->GatherEvents(animation.time, unwrappedTime, animation.loop))
                {
                    animation.firedEvents.push_back(event.name);
                    if (m_eventCallback)
                        m_eventCallback(entity, event);
                }
                animation.time = unwrappedTime;
                if (animation.loop && skeletalClip->duration > 0.0f)
                {
                    animation.time = std::fmod(animation.time, skeletalClip->duration);
                    if (animation.time < 0.0f)
                        animation.time += skeletalClip->duration;
                }
                else if (animation.time >= skeletalClip->duration)
                {
                    animation.time = skeletalClip->duration;
                    animation.playing = false;
                }
                else if (animation.time <= 0.0f && animation.speed < 0.0f)
                {
                    animation.time = 0.0f;
                    animation.playing = false;
                }
                pose = skeletalClip->Sample(animation.time, skeleton->second.bones.size(), animation.loop);
            }
            if (!eventClipPath.empty())
            {
                if (const SkeletalClip *eventClip = LoadSkeletalClip(eventClipPath))
                    for (const auto &event : eventClip->GatherEvents(eventPreviousTime, animation.time, true))
                    {
                        animation.firedEvents.push_back(event.name);
                        if (m_eventCallback)
                            m_eventCallback(entity, event);
                    }
            }
            animation.skinMatrices = BuildSkinMatrices(skeleton->second, pose);
            return;
        }
        const Clip *clip = LoadClip(animation.clipPath);
        if (!clip || clip->keys.empty() || clip->duration <= 0.0f)
            return;
        const auto sample = [](const Clip &value, const float time) {
            const auto upper = std::upper_bound(value.keys.begin(), value.keys.end(), time,
                                                [](const float keyTime, const Keyframe &key) { return keyTime < key.time; });
            const Keyframe &next = upper == value.keys.end() ? value.keys.back() : *upper;
            const Keyframe &previous = upper == value.keys.begin() ? value.keys.front() : *(upper - 1);
            const float span = next.time - previous.time;
            const float alpha = span > 0.0001f ? std::clamp((time - previous.time) / span, 0.0f, 1.0f) : 0.0f;
            Keyframe result;
            result.time = time;
            result.position = previous.position + (next.position - previous.position) * alpha;
            result.rotation = previous.rotation + (next.rotation - previous.rotation) * alpha;
            result.scale = previous.scale + (next.scale - previous.scale) * alpha;
            return result;
        };
        const float previousTime = animation.time;
        animation.time += deltaTime * animation.speed;
        bool wrapped = false;
        if (animation.loop)
        {
            while (animation.time >= clip->duration)
            {
                animation.time -= clip->duration;
                wrapped = true;
            }
            while (animation.time < 0.0f)
            {
                animation.time += clip->duration;
                wrapped = true;
            }
        }
        else
        {
            if (animation.time >= clip->duration)
            {
                animation.time = clip->duration;
                animation.playing = false;
            }
            if (animation.time < 0.0f)
            {
                animation.time = 0.0f;
                animation.playing = false;
            }
        }
        Keyframe pose = sample(*clip, animation.time);
        if (!animation.nextClipPath.empty() && animation.blendDuration > 0.0f)
        {
            if (const Clip *nextClip = LoadClip(animation.nextClipPath); nextClip && !nextClip->keys.empty() && nextClip->duration > 0.0f)
            {
                animation.blendTime += std::abs(deltaTime);
                const float nextTime = std::fmod(std::max(0.0f, animation.time), nextClip->duration);
                const Keyframe nextPose = sample(*nextClip, nextTime);
                const float alpha = std::clamp(animation.blendTime / animation.blendDuration, 0.0f, 1.0f);
                pose.position += (nextPose.position - pose.position) * alpha;
                pose.rotation += (nextPose.rotation - pose.rotation) * alpha;
                pose.scale += (nextPose.scale - pose.scale) * alpha;
                if (alpha >= 1.0f)
                {
                    animation.clipPath = animation.nextClipPath;
                    animation.nextClipPath.clear();
                    animation.time = nextTime;
                    animation.blendTime = 0.0f;
                    animation.blendDuration = 0.0f;
                }
            }
        }
        if (animation.rootMotion)
        {
            const Keyframe previousPose = sample(*clip, std::clamp(previousTime, 0.0f, clip->duration));
            Vec3 motion = pose.position - previousPose.position;
            if (wrapped && animation.speed >= 0.0f)
                motion = (sample(*clip, clip->duration).position - previousPose.position) + (pose.position - sample(*clip, 0.0f).position);
            transform.position += motion;
            transform.rotation = pose.rotation;
            transform.scale = pose.scale;
        }
        else
        {
            transform.position = pose.position;
            transform.rotation = pose.rotation;
            transform.scale = pose.scale;
        }
    });
}
} // namespace MiniEngine
