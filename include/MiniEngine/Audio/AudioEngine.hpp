#pragma once

#include "MiniEngine/Math/Math.hpp"
#include "MiniEngine/Types.hpp"

#include <cstdint>
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
    bool Initialize(Logger *logger = nullptr);
    void Shutdown();
    bool PlayOneShot(const std::string &path, float volume = 1.0f, const std::string &bus = "SFX");
    bool PlaySpatial(const std::string &path, const Vec3 &source, const Vec3 &listener, float volume = 1.0f, float minimumDistance = 1.0f,
                     float maximumDistance = 25.0f, const std::string &bus = "SFX");
    void UpdateWorld(const World &world);
    void SetBusVolume(const std::string &bus, float volume);
    float GetBusVolume(const std::string &bus) const;
    void StopAll();
    void ClearWorldState();
    bool IsInitialized() const;

  private:
    struct Voice
    {
        std::uint64_t id = 0;
        Entity entity = InvalidEntity;
        std::vector<std::int16_t> samples;
        std::size_t cursor = 0;
        float volume = 1.0f;
        float attenuation = 1.0f;
        float pan = 0.0f;
        bool loop = false;
        std::string bus;
    };
    static void MixCallback(void *userData, std::uint8_t *stream, int length);
    bool StartVoice(const std::string &path, float volume, const std::string &bus, bool loop, Entity entity, float attenuation, float pan);
    std::uint32_t m_device = 0;
    Logger *m_logger = nullptr;
    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    std::unordered_map<std::string, float> m_busVolumes{{"Master", 1.0f}, {"SFX", 1.0f}, {"Music", 1.0f}, {"UI", 1.0f}};
    std::unordered_map<Entity, std::uint64_t> m_entityVoices;
    std::uint64_t m_nextVoiceId = 1;
};
} // namespace MiniEngine
