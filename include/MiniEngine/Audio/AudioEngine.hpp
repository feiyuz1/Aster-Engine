// 文件：include/MiniEngine/Audio/AudioEngine.hpp
// 作用：管理音频解码、流式播放、空间声源、Voice 并发以及 Mixer/Bus。
// 约定：Voice 生命周期由音频系统拥有，流式缓冲不得在回调中阻塞。
// 类型：公开声明。

#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Types.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
class Logger;
class World;
class AudioEngine
{
  public:
    /// @brief 初始化对象及其依赖资源。
    /// @param logger 用于输出诊断信息的日志服务。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Initialize(Logger *logger = nullptr);
    /// @brief 停止服务并释放对象持有的资源。
    void Shutdown();
    /// @brief 播放 `OneShot`。
    /// @param path 文件、资源或目录路径。
    /// @param volume 线性音量系数。
    /// @param bus 目标音频 Bus 名称。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool PlayOneShot(const std::string &path, float volume = 1.0f, const std::string &bus = "SFX");
    /// @brief 播放 `Stream`。
    /// @param path 文件、资源或目录路径。
    /// @param volume 线性音量系数。
    /// @param loop 是否循环执行或播放。
    /// @param bus 目标音频 Bus 名称。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool PlayStream(const std::string &path, float volume = 1.0f, bool loop = true, const std::string &bus = "Music");
    /// @brief 播放 `Spatial`。
    /// @param path 文件、资源或目录路径。
    /// @param source 源数据、源资源或源位置。
    /// @param listener 监听器对象或音频听者位置。
    /// @param volume 线性音量系数。
    /// @param minimumDistance 目标尺寸、数量、索引或限制值。
    /// @param maximumDistance 目标尺寸、数量、索引或限制值。
    /// @param bus 目标音频 Bus 名称。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool PlaySpatial(const std::string &path, const Vec3 &source, const Vec3 &listener, float volume = 1.0f, float minimumDistance = 1.0f,
                     float maximumDistance = 25.0f, const std::string &bus = "SFX");
    /// @brief 更新 `World`。
    /// @param world 要读取或修改的 World。
    void UpdateWorld(const World &world);
    /// @brief 设置 `BusVolume`。
    /// @param bus 目标音频 Bus 名称。
    /// @param volume 线性音量系数。
    void SetBusVolume(const std::string &bus, float volume);
    /// @brief 获取 `BusVolume`。
    /// @param bus 目标音频 Bus 名称。
    /// @return 返回计算得到的数值、数量或索引。
    float GetBusVolume(const std::string &bus) const;
    /// @brief 停止 `All`。
    void StopAll();
    /// @brief 清空 `WorldState`。
    void ClearWorldState();
    /// @brief 判断是否满足 `Initialized` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool IsInitialized() const;

#ifdef TINYENGINE_TEST_HOOKS
    // 混音状态仅在测试构建中开放，供自动化用例检查。
    std::size_t TestGetVoiceCount() const;
    std::vector<std::int16_t> TestMixFrames(std::size_t frameCount);
#endif

  private:
    struct Voice
    {
        std::uint64_t id = 0;
        Entity entity = InvalidEntity;
        std::vector<std::int16_t> samples;
        std::vector<std::uint8_t> compressedData;
        std::shared_ptr<void> decoder;
        std::vector<std::int16_t> streamBuffer;
        std::size_t streamFrames = 0;
        std::size_t streamCursor = 0;
        double streamPhase = 0.0;
        int streamSampleRate = 44100;
        bool streaming = false;
        std::size_t cursor = 0;
        float volume = 1.0f;
        float attenuation = 1.0f;
        float pan = 0.0f;
        bool loop = false;
        std::string bus;
    };
    /// @brief 在实时音频线程混合一段输出采样。
    /// @param userData 要处理的输入或输出数据。
    /// @param stream 接口所需的输入参数。
    /// @param length 数据长度或几何长度。
    static void MixCallback(void *userData, std::uint8_t *stream, int length);
    /// @brief 启动 `Voice`。
    /// @param path 文件、资源或目录路径。
    /// @param volume 线性音量系数。
    /// @param bus 目标音频 Bus 名称。
    /// @param loop 是否循环执行或播放。
    /// @param entity 目标实体句柄。
    /// @param attenuation 接口所需的输入参数。
    /// @param pan 接口所需的输入参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool StartVoice(const std::string &path, float volume, const std::string &bus, bool loop, Entity entity, float attenuation, float pan);
    std::uint32_t m_device = 0;
    Logger *m_logger = nullptr;
    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    std::unordered_map<std::string, float> m_busVolumes{{"Master", 1.0f}, {"SFX", 1.0f}, {"Music", 1.0f}, {"UI", 1.0f}};
    std::unordered_map<Entity, std::uint64_t> m_entityVoices;
    std::uint64_t m_nextVoiceId = 1;
};
} // 结束 MiniEngine 命名空间
