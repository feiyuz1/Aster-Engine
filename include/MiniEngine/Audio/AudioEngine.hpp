#pragma once

#include <cstdint>
#include <string>

namespace MiniEngine
{
    class Logger;
    class AudioEngine
    {
    public:
        bool Initialize(Logger* logger = nullptr);
        void Shutdown();
        bool PlayOneShot(const std::string& path, float volume = 1.0f);
        void StopAll();
        bool IsInitialized() const;
    private:
        std::uint32_t m_device = 0;
        Logger* m_logger = nullptr;
    };
}
