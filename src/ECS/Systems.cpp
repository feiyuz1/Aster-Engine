#include "MiniEngine/ECS/Systems.hpp"

#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace MiniEngine
{
    void MovementSystem::Update(World& world, const float deltaTime)
    {
        auto& components = world.GetComponentManager();
        components.ForEach<VelocityComponent>([&components, deltaTime](const Entity entity, VelocityComponent& velocity)
        {
            TransformComponent* transform = components.Get<TransformComponent>(entity);
            if (transform == nullptr)
            {
                return;
            }

            transform->position.x += velocity.linear.x * deltaTime;
            transform->position.y += velocity.linear.y * deltaTime;
            transform->position.z += velocity.linear.z * deltaTime;
        });
    }

    const AnimationSystem::Clip* AnimationSystem::LoadClip(const std::string& path)
    {
        const auto found = m_clips.find(path);
        if (found != m_clips.end()) return &found->second;
        Clip clip;
        std::istringstream stream(PlatformFile::ReadAllText(path));
        std::string token;
        while (stream >> token)
        {
            if (token == "duration") stream >> clip.duration;
            else if (token == "key")
            {
                Keyframe key;
                stream >> key.time >> key.position.x >> key.position.y >> key.position.z >> key.rotation.x >> key.rotation.y >> key.rotation.z >> key.scale.x >> key.scale.y >> key.scale.z;
                if (stream) clip.keys.push_back(key);
            }
            else { std::string ignored; std::getline(stream, ignored); }
        }
        std::sort(clip.keys.begin(), clip.keys.end(), [](const Keyframe& lhs, const Keyframe& rhs) { return lhs.time < rhs.time; });
        if (clip.duration <= 0.0f && !clip.keys.empty()) clip.duration = clip.keys.back().time;
        return &m_clips.emplace(path, std::move(clip)).first->second;
    }

    void AnimationSystem::Update(World& world, const float deltaTime)
    {
        world.GetComponentManager().ForEach<TransformComponent, AnimationComponent>([this, deltaTime](Entity, TransformComponent& transform, AnimationComponent& animation)
        {
            if (!animation.playing || animation.clipPath.empty()) return;
            const Clip* clip = LoadClip(animation.clipPath);
            if (!clip || clip->keys.empty() || clip->duration <= 0.0f) return;
            const auto sample = [](const Clip& value, const float time)
            {
                const auto upper = std::upper_bound(value.keys.begin(), value.keys.end(), time, [](const float keyTime, const Keyframe& key) { return keyTime < key.time; });
                const Keyframe& next = upper == value.keys.end() ? value.keys.back() : *upper;
                const Keyframe& previous = upper == value.keys.begin() ? value.keys.front() : *(upper - 1);
                const float span = next.time - previous.time;
                const float alpha = span > 0.0001f ? std::clamp((time - previous.time) / span, 0.0f, 1.0f) : 0.0f;
                Keyframe result; result.time = time;
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
                while (animation.time >= clip->duration) { animation.time -= clip->duration; wrapped = true; }
                while (animation.time < 0.0f) { animation.time += clip->duration; wrapped = true; }
            }
            else
            {
                if (animation.time >= clip->duration) { animation.time = clip->duration; animation.playing = false; }
                if (animation.time < 0.0f) { animation.time = 0.0f; animation.playing = false; }
            }
            Keyframe pose = sample(*clip, animation.time);
            if (!animation.nextClipPath.empty() && animation.blendDuration > 0.0f)
            {
                if (const Clip* nextClip = LoadClip(animation.nextClipPath); nextClip && !nextClip->keys.empty() && nextClip->duration > 0.0f)
                {
                    animation.blendTime += std::abs(deltaTime);
                    const float nextTime = std::fmod(std::max(0.0f, animation.time), nextClip->duration);
                    const Keyframe nextPose = sample(*nextClip, nextTime);
                    const float alpha = std::clamp(animation.blendTime / animation.blendDuration, 0.0f, 1.0f);
                    pose.position += (nextPose.position - pose.position) * alpha; pose.rotation += (nextPose.rotation - pose.rotation) * alpha; pose.scale += (nextPose.scale - pose.scale) * alpha;
                    if (alpha >= 1.0f) { animation.clipPath = animation.nextClipPath; animation.nextClipPath.clear(); animation.time = nextTime; animation.blendTime = 0.0f; animation.blendDuration = 0.0f; }
                }
            }
            if (animation.rootMotion)
            {
                const Keyframe previousPose = sample(*clip, std::clamp(previousTime, 0.0f, clip->duration));
                Vec3 motion = pose.position - previousPose.position;
                if (wrapped && animation.speed >= 0.0f) motion = (sample(*clip, clip->duration).position - previousPose.position) + (pose.position - sample(*clip, 0.0f).position);
                transform.position += motion; transform.rotation = pose.rotation; transform.scale = pose.scale;
            }
            else { transform.position = pose.position; transform.rotation = pose.rotation; transform.scale = pose.scale; }
        });
    }
}
