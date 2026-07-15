#pragma once

#include "MiniEngine/Animation/SkeletalAnimation.hpp"

#include <functional>
#include <string>
#include <vector>

namespace MiniEngine
{
struct SequenceTransformKey
{
    float time = 0.0f;
    BonePose transform;
};

class TransformTrack
{
  public:
    void AddKey(SequenceTransformKey key);
    BonePose Sample(float time) const;
    const std::vector<SequenceTransformKey> &GetKeys() const;

  private:
    std::vector<SequenceTransformKey> m_keys;
};

struct SequenceEvent
{
    float time = 0.0f;
    std::string name;
};

class SequencePlayer
{
  public:
    using EventCallback = std::function<void(const std::string &)>;
    void SetDuration(float duration);
    void SetLooping(bool looping);
    void SetEvents(std::vector<SequenceEvent> events);
    void SetEventCallback(EventCallback callback);
    void Play();
    void Pause();
    void Stop();
    void Seek(float time);
    void Update(float deltaTime);
    float GetTime() const;
    bool IsPlaying() const;

  private:
    void Dispatch(float from, float to);
    std::vector<SequenceEvent> m_events;
    EventCallback m_callback;
    float m_duration = 0.0f;
    float m_time = 0.0f;
    bool m_looping = false;
    bool m_playing = false;
};
} // namespace MiniEngine
