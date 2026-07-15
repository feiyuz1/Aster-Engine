#include "MiniEngine/Cinematic/Sequencer.hpp"

#include <algorithm>
#include <cmath>

namespace MiniEngine
{
void TransformTrack::AddKey(SequenceTransformKey key)
{
    key.time = std::max(0.0f, key.time);
    m_keys.push_back(std::move(key));
    std::sort(m_keys.begin(), m_keys.end(),
              [](const SequenceTransformKey &left, const SequenceTransformKey &right) { return left.time < right.time; });
}

BonePose TransformTrack::Sample(const float time) const
{
    if (m_keys.empty())
        return {};
    const auto right = std::upper_bound(m_keys.begin(), m_keys.end(), time,
                                        [](const float value, const SequenceTransformKey &key) { return value < key.time; });
    if (right == m_keys.begin())
        return right->transform;
    if (right == m_keys.end())
        return m_keys.back().transform;
    const auto &left = *(right - 1);
    const float alpha = right->time > left.time ? std::clamp((time - left.time) / (right->time - left.time), 0.0f, 1.0f) : 0.0f;
    return {left.transform.position * (1.0f - alpha) + right->transform.position * alpha,
            left.transform.rotation * (1.0f - alpha) + right->transform.rotation * alpha,
            left.transform.scale * (1.0f - alpha) + right->transform.scale * alpha};
}

const std::vector<SequenceTransformKey> &TransformTrack::GetKeys() const
{
    return m_keys;
}

void SequencePlayer::SetDuration(const float duration)
{
    m_duration = std::max(0.0f, duration);
    m_time = std::min(m_time, m_duration);
}

void SequencePlayer::SetLooping(const bool looping)
{
    m_looping = looping;
}

void SequencePlayer::SetEvents(std::vector<SequenceEvent> events)
{
    m_events = std::move(events);
    std::sort(m_events.begin(), m_events.end(),
              [](const SequenceEvent &left, const SequenceEvent &right) { return left.time < right.time; });
}

void SequencePlayer::SetEventCallback(EventCallback callback)
{
    m_callback = std::move(callback);
}

void SequencePlayer::Play()
{
    if (m_duration > 0.0f)
        m_playing = true;
}

void SequencePlayer::Pause()
{
    m_playing = false;
}

void SequencePlayer::Stop()
{
    m_playing = false;
    m_time = 0.0f;
}

void SequencePlayer::Seek(const float time)
{
    m_time = std::clamp(time, 0.0f, m_duration);
}

void SequencePlayer::Update(const float deltaTime)
{
    if (!m_playing || deltaTime <= 0.0f || m_duration <= 0.0f)
        return;
    const float previous = m_time;
    m_time += deltaTime;
    if (m_time < m_duration)
    {
        Dispatch(previous, m_time);
        return;
    }
    Dispatch(previous, m_duration);
    if (m_looping)
    {
        m_time = std::fmod(m_time, m_duration);
        Dispatch(0.0f, m_time);
    }
    else
    {
        m_time = m_duration;
        m_playing = false;
    }
}

float SequencePlayer::GetTime() const
{
    return m_time;
}

bool SequencePlayer::IsPlaying() const
{
    return m_playing;
}

void SequencePlayer::Dispatch(const float from, const float to)
{
    if (!m_callback)
        return;
    for (const auto &event : m_events)
        if (event.time > from && event.time <= to)
            m_callback(event.name);
}
} // namespace MiniEngine
