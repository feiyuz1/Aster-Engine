#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
    class PlatformFile
    {
    public:
        static bool Exists(const std::filesystem::path& path);
        static std::vector<std::uint8_t> ReadAllBytes(const std::filesystem::path& path);
        static std::string ReadAllText(const std::filesystem::path& path);
        static bool WriteAllBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& data);
        static bool WriteAllText(const std::filesystem::path& path, const std::string& text);
        static std::filesystem::path Normalize(const std::filesystem::path& path);
    };

    class EngineClock
    {
    public:
        void Reset();
        float Tick();
        double GetTotalSeconds() const;

    private:
        using Clock = std::chrono::steady_clock;
        Clock::time_point m_start = Clock::now();
        Clock::time_point m_previous = m_start;
    };

    class FileWatcher
    {
    public:
        bool HasChanged(const std::filesystem::path& path);

    private:
        std::filesystem::file_time_type m_lastWrite{};
        bool m_initialized = false;
    };
}
