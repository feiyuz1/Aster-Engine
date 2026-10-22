// 文件：src/media/image_sequence.cpp
// 作用：读取图片序列并按时间输出帧，供媒体与过场系统使用。
// 约定：媒体解码与显示时钟分离，丢帧优先于阻塞主循环。
// 类型：实现文件。

#include "tinyengine/media/image_sequence.h"

#include <algorithm>
#include <cmath>

namespace tinyengine
{
bool ImageSequencePlayer::setFrames(std::vector<std::string> frames, const float framesPerSecond)
{
    if (frames.empty() || framesPerSecond <= 0.0f ||
        std::any_of(frames.begin(), frames.end(), [](const std::string& frame) { return frame.empty(); }))
        return false;
    m_frames = std::move(frames);
    m_framesPerSecond = framesPerSecond;
    m_time = 0.0f;
    return true;
}

void ImageSequencePlayer::setLooping(const bool looping)
{
    m_looping = looping;
}

void ImageSequencePlayer::play()
{
    if (!m_frames.empty())
        m_playing = true;
}

void ImageSequencePlayer::pause()
{
    m_playing = false;
}

void ImageSequencePlayer::stop()
{
    m_playing = false;
    m_time = 0.0f;
}

void ImageSequencePlayer::seek(const float seconds)
{
    m_time = std::clamp(seconds, 0.0f, getDuration());
}

void ImageSequencePlayer::Update(const float deltaTime)
{
    if (!m_playing || deltaTime <= 0.0f)
        return;
    m_time += deltaTime;
    const float duration = getDuration();
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

const std::string& ImageSequencePlayer::GetCurrentFrame() const
{
    static const std::string empty;
    return m_frames.empty() ? empty : m_frames[getFrameIndex()];
}

std::size_t ImageSequencePlayer::getFrameIndex() const
{
    if (m_frames.empty() || m_framesPerSecond <= 0.0f)
        return 0;
    return std::min(static_cast<std::size_t>(m_time * m_framesPerSecond), m_frames.size() - 1);
}

float ImageSequencePlayer::getDuration() const
{
    return m_framesPerSecond > 0.0f ? static_cast<float>(m_frames.size()) / m_framesPerSecond : 0.0f;
}

bool ImageSequencePlayer::isPlaying() const
{
    return m_playing;
}
} // namespace tinyengine
