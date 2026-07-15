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
