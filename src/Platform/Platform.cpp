#include "MiniEngine/Platform/Platform.hpp"

#include <fstream>
#include <iterator>

namespace MiniEngine
{
    bool PlatformFile::Exists(const std::filesystem::path& path) { return std::filesystem::exists(path); }

    std::vector<std::uint8_t> PlatformFile::ReadAllBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return {};
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::string PlatformFile::ReadAllText(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return {};
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    bool PlatformFile::WriteAllBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& data)
    {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        return stream.good();
    }

    bool PlatformFile::WriteAllText(const std::filesystem::path& path, const std::string& text)
    {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream << text;
        return stream.good();
    }

    std::filesystem::path PlatformFile::Normalize(const std::filesystem::path& path)
    {
        return path.lexically_normal();
    }

    void EngineClock::Reset() { m_start = m_previous = Clock::now(); }

    float EngineClock::Tick()
    {
        const auto now = Clock::now();
        const float delta = std::chrono::duration<float>(now - m_previous).count();
        m_previous = now;
        return delta;
    }

    double EngineClock::GetTotalSeconds() const
    {
        return std::chrono::duration<double>(Clock::now() - m_start).count();
    }

    bool FileWatcher::HasChanged(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path)) return false;
        const auto current = std::filesystem::last_write_time(path);
        if (!m_initialized)
        {
            m_initialized = true;
            m_lastWrite = current;
            return false;
        }
        if (current == m_lastWrite) return false;
        m_lastWrite = current;
        return true;
    }
}
