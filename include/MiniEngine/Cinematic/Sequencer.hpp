// 文件：include/MiniEngine/Cinematic/Sequencer.hpp
// 作用：提供镜头、属性轨道和关键帧驱动的轻量级过场序列。
// 约定：序列只写入被轨道驱动的属性，停止时由调用者决定是否恢复。
// 类型：公开声明。

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
    /// @brief 添加 `Key`。
    /// @param key 用于查找数据的键。
    void AddKey(SequenceTransformKey key);
    /// @brief 在指定位置或时间采样数据。
    /// @param time 采样或播放时间，单位为秒。
    /// @return 返回接口计算或查询得到的结果。
    BonePose Sample(float time) const;
    /// @brief 获取 `Keys`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
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
    /// @brief 设置 `Duration`。
    /// @param duration 时间值，单位按接口约定为秒。
    void SetDuration(float duration);
    /// @brief 设置 `Looping`。
    /// @param looping 是否循环播放。
    void SetLooping(bool looping);
    /// @brief 设置 `Events`。
    /// @param events 接口所需的输入参数。
    void SetEvents(std::vector<SequenceEvent> events);
    /// @brief 设置 `EventCallback`。
    /// @param callback 满足条件时调用的回调函数。
    void SetEventCallback(EventCallback callback);
    /// @brief 开始或继续播放。
    void Play();
    /// @brief 暂停播放并保留当前位置。
    void Pause();
    /// @brief 停止对应任务或服务。
    void Stop();
    /// @brief 跳转到指定播放时间。
    /// @param time 采样或播放时间，单位为秒。
    void Seek(float time);
    /// @brief 更新对象当前状态。
    /// @param deltaTime 本帧时间间隔，单位为秒。
    void Update(float deltaTime);
    /// @brief 获取 `Time`。
    /// @return 返回计算得到的数值、数量或索引。
    float GetTime() const;
    /// @brief 判断是否满足 `Playing` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsPlaying() const;

  private:
    /// @brief 分派待处理事件或任务。
    /// @param from 操作的起点、源状态或源节点。
    /// @param to 操作的终点、目标状态或目标节点。
    void Dispatch(float from, float to);
    std::vector<SequenceEvent> m_events;
    EventCallback m_callback;
    float m_duration = 0.0f;
    float m_time = 0.0f;
    bool m_looping = false;
    bool m_playing = false;
};
} // 结束 MiniEngine 命名空间
