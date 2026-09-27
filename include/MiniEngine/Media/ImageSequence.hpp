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
    /// @brief 设置 `Frames`。
    /// @param frames 目标尺寸、数量、索引或限制值。
    /// @param framesPerSecond 目标尺寸、数量、索引或限制值。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool SetFrames(std::vector<std::string> frames, float framesPerSecond);
    /// @brief 设置 `Looping`。
    /// @param looping 是否循环播放。
    void SetLooping(bool looping);
    /// @brief 开始或继续播放。
    void Play();
    /// @brief 暂停播放并保留当前位置。
    void Pause();
    /// @brief 停止对应任务或服务。
    void Stop();
    /// @brief 跳转到指定播放时间。
    /// @param seconds 时间长度，单位为秒。
    void Seek(float seconds);
    /// @brief 更新对象当前状态。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void Update(float deltaTime);
    /// @brief 获取 `CurrentFrame`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string &GetCurrentFrame() const;
    /// @brief 获取 `FrameIndex`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t GetFrameIndex() const;
    /// @brief 获取 `Duration`。
    /// @return 返回计算得到的数值、数量或索引。
    float GetDuration() const;
    /// @brief 判断是否满足 `Playing` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsPlaying() const;

  private:
    std::vector<std::string> m_frames;
    float m_framesPerSecond = 0.0f;
    float m_time = 0.0f;
    bool m_looping = false;
    bool m_playing = false;
};
} // 结束 MiniEngine 命名空间
