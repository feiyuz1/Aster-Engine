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
            animation.time += deltaTime * animation.speed;
            if (animation.loop) animation.time = std::fmod(std::max(0.0f, animation.time), clip->duration);
            else if (animation.time >= clip->duration) { animation.time = clip->duration; animation.playing = false; }
            const auto upper = std::upper_bound(clip->keys.begin(), clip->keys.end(), animation.time, [](float time, const Keyframe& key) { return time < key.time; });
            const Keyframe& next = upper == clip->keys.end() ? clip->keys.back() : *upper;
            const Keyframe& previous = upper == clip->keys.begin() ? clip->keys.front() : *(upper - 1);
            const float span = next.time - previous.time;
            const float alpha = span > 0.0001f ? std::clamp((animation.time - previous.time) / span, 0.0f, 1.0f) : 0.0f;
            const auto lerp = [alpha](const Vec3& a, const Vec3& b) { return a + (b - a) * alpha; };
            transform.position = lerp(previous.position, next.position);
            transform.rotation = lerp(previous.rotation, next.rotation);
            transform.scale = lerp(previous.scale, next.scale);
        });
    }
}
