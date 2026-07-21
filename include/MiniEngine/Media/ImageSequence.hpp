// 文件：include/MiniEngine/Media/ImageSequence.hpp
// 作用：读取图片序列并按时间输出帧，供媒体与过场系统使用。
// 约定：媒体解码与显示时钟分离，丢帧优先于阻塞主循环。
// 类型：公开声明。

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace MiniEngine
{
class ImageSequencePlayer
{
  public:
    bool SetFrames(std::vector<std::string> frames, float framesPerSecond);
    void SetLooping(bool looping);
    void Play();
    void Pause();
    void Stop();
    void Seek(float seconds);
    void Update(float deltaTime);
    const std::string &GetCurrentFrame() const;
    std::size_t GetFrameIndex() const;
    float GetDuration() const;
    bool IsPlaying() const;

  private:
    std::vector<std::string> m_frames;
    float m_framesPerSecond = 0.0f;
    float m_time = 0.0f;
    bool m_looping = false;
    bool m_playing = false;
};
} // namespace MiniEngine
