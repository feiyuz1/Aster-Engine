// 文件：src/Platform/Platform.cpp
// 作用：封装计时、文件、动态库、进程和平台输入等操作系统差异。
// 约定：平台差异集中封装在此层，上层模块不直接调用系统 API。
// 类型：实现文件。

#include "MiniEngine/Platform/Platform.hpp"
#include "MiniEngine/Resource/PakArchive.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>

namespace MiniEngine
{
namespace
{
struct Mount
{
    std::filesystem::path path;
    int priority = 0;
    bool pak = false;
};
std::vector<Mount> mounts;
std::mutex mountMutex;

std::optional<std::vector<std::uint8_t>> ReadMounted(const std::filesystem::path &path)
{
    if (path.is_absolute())
        return std::nullopt;
    const std::string normalized = path.lexically_normal().generic_string();
    std::lock_guard<std::mutex> lock(mountMutex);
    for (const auto &mount : mounts)
    {
        if (mount.pak)
        {
            if (auto data = PakArchive::ReadFile(mount.path, normalized))
                return data;
        }
        else
        {
            const auto candidate = mount.path / path;
            std::ifstream stream(candidate, std::ios::binary);
            if (stream)
                return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        }
    }
    return std::nullopt;
}
} // namespace

bool PlatformFile::Exists(const std::filesystem::path &path)
{
    if (std::filesystem::exists(path))
        return true;
    return ReadMounted(path).has_value();
}

std::vector<std::uint8_t> PlatformFile::ReadAllBytes(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        auto mounted = ReadMounted(path);
        return mounted ? std::move(*mounted) : std::vector<std::uint8_t>{};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string PlatformFile::ReadAllText(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        auto mounted = ReadMounted(path);
        return mounted ? std::string(mounted->begin(), mounted->end()) : std::string{};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool PlatformFile::WriteAllBytes(const std::filesystem::path &path, const std::vector<std::uint8_t> &data)
{
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return false;
    stream.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    return stream.good();
}

bool PlatformFile::WriteAllText(const std::filesystem::path &path, const std::string &text)
{
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return false;
    stream << text;
    return stream.good();
}

std::filesystem::path PlatformFile::Normalize(const std::filesystem::path &path)
{
    return path.lexically_normal();
}

void PlatformFile::MountDirectory(const std::filesystem::path &directory, const int priority)
{
    if (!std::filesystem::is_directory(directory))
        return;
    std::lock_guard<std::mutex> lock(mountMutex);
    mounts.push_back({std::filesystem::absolute(directory).lexically_normal(), priority, false});
    std::stable_sort(mounts.begin(), mounts.end(), [](const Mount &a, const Mount &b) { return a.priority > b.priority; });
}

bool PlatformFile::MountPak(const std::filesystem::path &archive, const int priority)
{
    if (PakArchive::List(archive).empty())
        return false;
    std::lock_guard<std::mutex> lock(mountMutex);
    mounts.push_back({std::filesystem::absolute(archive).lexically_normal(), priority, true});
    std::stable_sort(mounts.begin(), mounts.end(), [](const Mount &a, const Mount &b) { return a.priority > b.priority; });
    return true;
}

void PlatformFile::ClearMounts()
{
    std::lock_guard<std::mutex> lock(mountMutex);
    mounts.clear();
}

void EngineClock::Reset()
{
    m_start = m_previous = Clock::now();
}

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

bool FileWatcher::HasChanged(const std::filesystem::path &path)
{
    if (!std::filesystem::exists(path))
        return false;
    const auto current = std::filesystem::last_write_time(path);
    if (!m_initialized)
    {
        m_initialized = true;
        m_lastWrite = current;
        return false;
    }
    if (current == m_lastWrite)
        return false;
    m_lastWrite = current;
    return true;
}
} // namespace MiniEngine
