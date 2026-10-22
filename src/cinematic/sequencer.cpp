// 文件：src/cinematic/sequencer.cpp
// 作用：提供镜头、属性轨道和关键帧驱动的轻量级过场序列。
// 约定：序列只写入被轨道驱动的属性，停止时由调用者决定是否恢复。
// 类型：实现文件。

#include "tinyengine/cinematic/sequencer.h"

#include <algorithm>
#include <cmath>

namespace tinyengine
{
void TransformTrack::addKey(SequenceTransformKey key)
{
    key.time = std::max(0.0f, key.time);
    m_keys.push_back(std::move(key));
    std::sort(m_keys.begin(), m_keys.end(),
              [](const SequenceTransformKey& left, const SequenceTransformKey& right)
              { return left.time < right.time; });
}

BonePose TransformTrack::sample(const float time) const
{
    if (m_keys.empty())
        return {};
    const auto right =
        std::upper_bound(m_keys.begin(), m_keys.end(), time,
                         [](const float value, const SequenceTransformKey& key) { return value < key.time; });
    if (right == m_keys.begin())
        return right->transform;
    if (right == m_keys.end())
        return m_keys.back().transform;
    const auto& left = *(right - 1);
    const float alpha =
        right->time > left.time ? std::clamp((time - left.time) / (right->time - left.time), 0.0f, 1.0f) : 0.0f;
    return {left.transform.position * (1.0f - alpha) + right->transform.position * alpha,
            left.transform.rotation * (1.0f - alpha) + right->transform.rotation * alpha,
            left.transform.scale * (1.0f - alpha) + right->transform.scale * alpha};
}

const std::vector<SequenceTransformKey>& TransformTrack::GetKeys() const
{
    return m_keys;
}

void SequencePlayer::setDuration(const float duration)
{
    m_duration = std::max(0.0f, duration);
    m_time = std::min(m_time, m_duration);
}

void SequencePlayer::setLooping(const bool looping)
{
    m_looping = looping;
}

void SequencePlayer::setEvents(std::vector<SequenceEvent> events)
{
    m_events = std::move(events);
    std::sort(m_events.begin(), m_events.end(),
              [](const SequenceEvent& left, const SequenceEvent& right) { return left.time < right.time; });
}

void SequencePlayer::setEventCallback(EventCallback callback)
{
    m_callback = std::move(callback);
}

void SequencePlayer::play()
{
    if (m_duration > 0.0f)
        m_playing = true;
}

void SequencePlayer::pause()
{
    m_playing = false;
}

void SequencePlayer::stop()
{
    m_playing = false;
    m_time = 0.0f;
}

void SequencePlayer::seek(const float time)
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
        dispatch(previous, m_time);
        return;
    }
    dispatch(previous, m_duration);
    if (m_looping)
    {
        m_time = std::fmod(m_time, m_duration);
        dispatch(0.0f, m_time);
    }
    else
    {
        m_time = m_duration;
        m_playing = false;
    }
}

float SequencePlayer::getTime() const
{
    return m_time;
}

bool SequencePlayer::isPlaying() const
{
    return m_playing;
}

void SequencePlayer::dispatch(const float from, const float to)
{
    if (!m_callback)
        return;
    for (const auto& event : m_events)
        if (event.time > from && event.time <= to)
            m_callback(event.name);
}
} // namespace tinyengine
