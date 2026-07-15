#include "MiniEngine/Media/ImageSequence.hpp"

#include <algorithm>
#include <cmath>

namespace MiniEngine
{
bool ImageSequencePlayer::SetFrames(std::vector<std::string> frames, const float framesPerSecond)
{
    if (frames.empty() || framesPerSecond <= 0.0f ||
        std::any_of(frames.begin(), frames.end(), [](const std::string &frame) { return frame.empty(); }))
        return false;
    m_frames = std::move(frames);
    m_framesPerSecond = framesPerSecond;
    m_time = 0.0f;
    return true;
}

void ImageSequencePlayer::SetLooping(const bool looping)
{
    m_looping = looping;
}

void ImageSequencePlayer::Play()
{
    if (!m_frames.empty())
        m_playing = true;
}

void ImageSequencePlayer::Pause()
{
    m_playing = false;
}

void ImageSequencePlayer::Stop()
{
    m_playing = false;
    m_time = 0.0f;
}

void ImageSequencePlayer::Seek(const float seconds)
{
    m_time = std::clamp(seconds, 0.0f, GetDuration());
}

void ImageSequencePlayer::Update(const float deltaTime)
{
    if (!m_playing || deltaTime <= 0.0f)
        return;
    m_time += deltaTime;
    const float duration = GetDuration();
    if (m_time < duration)
        return;
    if (m_looping && duration > 0.0f)
        m_time = std::fmod(m_time, duration);
    else
    {
        m_time = duration;
        m_playing = false;
    }
}

const std::string &ImageSequencePlayer::GetCurrentFrame() const
{
    static const std::string empty;
    return m_frames.empty() ? empty : m_frames[GetFrameIndex()];
}

std::size_t ImageSequencePlayer::GetFrameIndex() const
{
    if (m_frames.empty() || m_framesPerSecond <= 0.0f)
        return 0;
    return std::min(static_cast<std::size_t>(m_time * m_framesPerSecond), m_frames.size() - 1);
}

float ImageSequencePlayer::GetDuration() const
{
    return m_framesPerSecond > 0.0f ? static_cast<float>(m_frames.size()) / m_framesPerSecond : 0.0f;
}

bool ImageSequencePlayer::IsPlaying() const
{
    return m_playing;
}
} // namespace MiniEngine
