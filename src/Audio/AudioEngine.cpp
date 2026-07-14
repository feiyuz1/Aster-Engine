#include "MiniEngine/Audio/AudioEngine.hpp"

#include "MiniEngine/Core/Logger.hpp"
#include "MiniEngine/ECS/Components.hpp"
#include "MiniEngine/ECS/ECS.hpp"
#include "MiniEngine/Platform/Platform.hpp"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace MiniEngine
{
    bool AudioEngine::Initialize(Logger* logger)
    {
        m_logger = logger;
        SDL_AudioSpec desired{};
        desired.freq = 44100; desired.format = AUDIO_S16SYS; desired.channels = 2; desired.samples = 1024;
        desired.callback = &AudioEngine::MixCallback; desired.userdata = this;
        m_device = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
        if (!m_device) { if (m_logger) m_logger->Warning(std::string("Audio unavailable: ") + SDL_GetError()); return false; }
        SDL_PauseAudioDevice(m_device, 0); return true;
    }

    void AudioEngine::Shutdown()
    {
        if (m_device) { SDL_PauseAudioDevice(m_device, 1); SDL_CloseAudioDevice(m_device); m_device = 0; }
        std::lock_guard<std::mutex> lock(m_mutex); m_voices.clear(); m_entityVoices.clear(); m_logger = nullptr;
    }

    bool AudioEngine::StartVoice(const std::string& path, const float volume, const std::string& bus, const bool loop, const Entity entity, const float attenuation, const float pan)
    {
        if (!m_device) return false;
        const auto bytes = PlatformFile::ReadAllBytes(path);
        if (bytes.empty()) return false;
        SDL_RWops* memory = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
        SDL_AudioSpec sourceSpec{}; Uint8* source = nullptr; Uint32 sourceLength = 0;
        if (!memory || !SDL_LoadWAV_RW(memory, 1, &sourceSpec, &source, &sourceLength)) return false;
        SDL_AudioCVT cvt{};
        if (SDL_BuildAudioCVT(&cvt, sourceSpec.format, sourceSpec.channels, sourceSpec.freq, AUDIO_S16SYS, 2, 44100) < 0) { SDL_FreeWAV(source); return false; }
        cvt.len = static_cast<int>(sourceLength);
        std::vector<Uint8> converted(static_cast<std::size_t>(sourceLength) * static_cast<std::size_t>(std::max(1, cvt.len_mult)));
        std::copy(source, source + sourceLength, converted.begin()); SDL_FreeWAV(source); cvt.buf = converted.data();
        if (SDL_ConvertAudio(&cvt) < 0) return false;
        Voice voice;
        voice.id = m_nextVoiceId++; voice.entity = entity; voice.volume = std::clamp(volume, 0.0f, 1.0f); voice.attenuation = std::clamp(attenuation, 0.0f, 1.0f);
        voice.pan = std::clamp(pan, -1.0f, 1.0f); voice.loop = loop; voice.bus = bus;
        voice.samples.resize(static_cast<std::size_t>(cvt.len_cvt) / sizeof(std::int16_t));
        std::memcpy(voice.samples.data(), converted.data(), static_cast<std::size_t>(cvt.len_cvt));
        std::lock_guard<std::mutex> lock(m_mutex);
        if (entity != InvalidEntity) m_entityVoices[entity] = voice.id;
        m_voices.push_back(std::move(voice));
        return true;
    }

    bool AudioEngine::PlayOneShot(const std::string& path, const float volume, const std::string& bus)
    {
        return StartVoice(path, volume, bus, false, InvalidEntity, 1.0f, 0.0f);
    }

    bool AudioEngine::PlaySpatial(const std::string& path, const Vec3& source, const Vec3& listener, const float volume, const float minimumDistance, const float maximumDistance, const std::string& bus)
    {
        const Vec3 offset = source - listener;
        const float distance = Length(offset), range = std::max(0.001f, maximumDistance - minimumDistance);
        const float attenuation = 1.0f - std::clamp((distance - minimumDistance) / range, 0.0f, 1.0f);
        const float pan = distance > 0.001f ? std::clamp(offset.x / distance, -1.0f, 1.0f) : 0.0f;
        return StartVoice(path, volume, bus, false, InvalidEntity, attenuation, pan);
    }

    void AudioEngine::UpdateWorld(const World& world)
    {
        Vec3 listener{};
        for (const Entity entity : world.GetEntityManager().GetAliveEntities())
        {
            const auto* camera = world.GetComponent<CameraComponent>(entity);
            const auto* transform = world.GetComponent<TransformComponent>(entity);
            if (camera && camera->primary && transform) { listener = transform->position; break; }
        }
        std::unordered_set<Entity> activeSources;
        for (const Entity entity : world.GetEntityManager().GetAliveEntities())
        {
            const auto* sourcePointer = world.GetComponent<AudioSourceComponent>(entity);
            if (!sourcePointer) continue;
            const auto& source = *sourcePointer;
            if (source.clipPath.empty() || !source.playOnStart) continue;
            const auto* transform = world.GetComponent<TransformComponent>(entity);
            const Vec3 position = transform ? transform->position : Vec3{};
            const Vec3 offset = position - listener; const float distance = Length(offset);
            const float range = std::max(0.001f, source.maximumDistance - source.minimumDistance);
            const float attenuation = source.spatial ? 1.0f - std::clamp((distance - source.minimumDistance) / range, 0.0f, 1.0f) : 1.0f;
            const float pan = source.spatial && distance > 0.001f ? std::clamp(offset.x / distance, -1.0f, 1.0f) : 0.0f;
            activeSources.insert(entity);
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto indexed = m_entityVoices.find(entity);
            if (indexed != m_entityVoices.end())
            {
                bool updated = false;
                for (auto& voice : m_voices) if (voice.id == indexed->second) { voice.volume = source.volume; voice.attenuation = attenuation; voice.pan = pan; voice.loop = source.loop; voice.bus = source.bus; updated = true; break; }
                if (updated) continue;
                m_entityVoices.erase(indexed);
            }
            // Starting a voice loads data, so do it outside the callback lock.
        }
        for (const Entity entity : world.GetEntityManager().GetAliveEntities())
        {
            const auto* sourcePointer = world.GetComponent<AudioSourceComponent>(entity);
            if (!sourcePointer) continue;
            const auto& source = *sourcePointer;
            if (source.clipPath.empty() || !source.playOnStart) continue;
            { std::lock_guard<std::mutex> lock(m_mutex); if (m_entityVoices.count(entity)) continue; }
            const auto* transform = world.GetComponent<TransformComponent>(entity); const Vec3 position = transform ? transform->position : Vec3{};
            const Vec3 offset = position - listener; const float distance = Length(offset), range = std::max(0.001f, source.maximumDistance - source.minimumDistance);
            const float attenuation = source.spatial ? 1.0f - std::clamp((distance - source.minimumDistance) / range, 0.0f, 1.0f) : 1.0f;
            const float pan = source.spatial && distance > 0.001f ? std::clamp(offset.x / distance, -1.0f, 1.0f) : 0.0f;
            StartVoice(source.clipPath, source.volume, source.bus, source.loop, entity, attenuation, pan);
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto iterator = m_entityVoices.begin(); iterator != m_entityVoices.end();)
            if (!activeSources.count(iterator->first)) iterator = m_entityVoices.erase(iterator); else ++iterator;
    }

    void AudioEngine::MixCallback(void* userData, std::uint8_t* stream, const int length)
    {
        auto& engine = *static_cast<AudioEngine*>(userData);
        std::memset(stream, 0, static_cast<std::size_t>(length));
        auto* output = reinterpret_cast<std::int16_t*>(stream); const std::size_t sampleCount = static_cast<std::size_t>(length) / sizeof(std::int16_t);
        std::lock_guard<std::mutex> lock(engine.m_mutex);
        for (std::size_t frame = 0; frame + 1 < sampleCount; frame += 2)
        {
            int mixedLeft = 0, mixedRight = 0;
            for (auto& voice : engine.m_voices)
            {
                if (voice.samples.empty()) continue;
                if (voice.cursor + 1 >= voice.samples.size()) { if (voice.loop) voice.cursor = 0; else continue; }
                const auto bus = engine.m_busVolumes.find(voice.bus); const float busVolume = bus == engine.m_busVolumes.end() ? 1.0f : bus->second;
                const float master = engine.m_busVolumes["Master"], gain = voice.volume * voice.attenuation * busVolume * master;
                const float leftGain = gain * (voice.pan > 0.0f ? 1.0f - voice.pan : 1.0f), rightGain = gain * (voice.pan < 0.0f ? 1.0f + voice.pan : 1.0f);
                mixedLeft += static_cast<int>(voice.samples[voice.cursor] * leftGain); mixedRight += static_cast<int>(voice.samples[voice.cursor + 1] * rightGain); voice.cursor += 2;
            }
            output[frame] = static_cast<std::int16_t>(std::clamp(mixedLeft, -32768, 32767)); output[frame + 1] = static_cast<std::int16_t>(std::clamp(mixedRight, -32768, 32767));
        }
        engine.m_voices.erase(std::remove_if(engine.m_voices.begin(), engine.m_voices.end(), [&engine](const Voice& voice)
        {
            const bool finished = !voice.loop && voice.cursor + 1 >= voice.samples.size();
            if (finished && voice.entity != InvalidEntity) engine.m_entityVoices.erase(voice.entity);
            return finished;
        }), engine.m_voices.end());
    }

    void AudioEngine::SetBusVolume(const std::string& bus, const float volume) { std::lock_guard<std::mutex> lock(m_mutex); m_busVolumes[bus] = std::clamp(volume, 0.0f, 1.0f); }
    float AudioEngine::GetBusVolume(const std::string& bus) const { std::lock_guard<std::mutex> lock(m_mutex); const auto found = m_busVolumes.find(bus); return found == m_busVolumes.end() ? 1.0f : found->second; }
    void AudioEngine::StopAll() { std::lock_guard<std::mutex> lock(m_mutex); m_voices.clear(); m_entityVoices.clear(); }
    void AudioEngine::ClearWorldState() { StopAll(); }
    bool AudioEngine::IsInitialized() const { return m_device != 0; }
}
