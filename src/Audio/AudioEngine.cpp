#include "MiniEngine/Audio/AudioEngine.hpp"

#include "MiniEngine/Core/Logger.hpp"

#include <SDL.h>
#include <algorithm>
#include <vector>

namespace MiniEngine
{
    bool AudioEngine::Initialize(Logger* logger)
    {
        m_logger = logger;
        SDL_AudioSpec desired{}; desired.freq = 44100; desired.format = AUDIO_S16SYS; desired.channels = 2; desired.samples = 2048;
        m_device = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
        if (!m_device) { if (m_logger) m_logger->Warning(std::string("Audio unavailable: ") + SDL_GetError()); return false; }
        SDL_PauseAudioDevice(m_device, 0); return true;
    }

    void AudioEngine::Shutdown() { if (m_device) { SDL_ClearQueuedAudio(m_device); SDL_CloseAudioDevice(m_device); m_device = 0; } m_logger = nullptr; }

    bool AudioEngine::PlayOneShot(const std::string& path, const float volume)
    {
        if (!m_device) return false;
        SDL_AudioSpec sourceSpec{}; Uint8* source = nullptr; Uint32 sourceLength = 0;
        if (!SDL_LoadWAV(path.c_str(), &sourceSpec, &source, &sourceLength)) return false;
        SDL_AudioCVT cvt{};
        if (SDL_BuildAudioCVT(&cvt, sourceSpec.format, sourceSpec.channels, sourceSpec.freq, AUDIO_S16SYS, 2, 44100) < 0) { SDL_FreeWAV(source); return false; }
        cvt.len = static_cast<int>(sourceLength); std::vector<Uint8> converted(static_cast<std::size_t>(sourceLength) * std::max(1, cvt.len_mult));
        std::copy(source, source + sourceLength, converted.begin()); SDL_FreeWAV(source); cvt.buf = converted.data();
        if (SDL_ConvertAudio(&cvt) < 0) return false;
        const int clampedVolume = static_cast<int>(std::clamp(volume, 0.0f, 1.0f) * SDL_MIX_MAXVOLUME);
        if (clampedVolume != SDL_MIX_MAXVOLUME)
        {
            std::vector<Uint8> scaled(static_cast<std::size_t>(cvt.len_cvt), 0);
            SDL_MixAudioFormat(scaled.data(), converted.data(), AUDIO_S16SYS, static_cast<Uint32>(cvt.len_cvt), clampedVolume);
            return SDL_QueueAudio(m_device, scaled.data(), static_cast<Uint32>(scaled.size())) == 0;
        }
        return SDL_QueueAudio(m_device, converted.data(), static_cast<Uint32>(cvt.len_cvt)) == 0;
    }

    void AudioEngine::StopAll() { if (m_device) SDL_ClearQueuedAudio(m_device); }
    bool AudioEngine::IsInitialized() const { return m_device != 0; }
}
