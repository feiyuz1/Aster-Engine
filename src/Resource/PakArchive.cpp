#include "MiniEngine/Resource/PakArchive.hpp"

#include "MiniEngine/Platform/Platform.hpp"

#include <algorithm>
#include <fstream>
#include <limits>

namespace MiniEngine
{
namespace
{
constexpr char Magic[4] = {'T', 'P', 'A', 'K'};
constexpr std::uint32_t Version = 1;

bool ReadHeader(std::ifstream &stream, std::uint32_t &count)
{
    char magic[4]{};
    std::uint32_t version = 0;
    stream.read(magic, sizeof(magic));
    stream.read(reinterpret_cast<char *>(&version), sizeof(version));
    stream.read(reinterpret_cast<char *>(&count), sizeof(count));
    return stream && std::equal(std::begin(magic), std::end(magic), std::begin(Magic)) && version == Version;
}
} // namespace

bool PakArchive::PackDirectory(const std::filesystem::path &inputDirectory, const std::filesystem::path &outputFile)
{
    if (!std::filesystem::is_directory(inputDirectory))
        return false;
    std::vector<std::filesystem::path> files;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(inputDirectory))
        if (entry.is_regular_file())
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    if (outputFile.has_parent_path())
        std::filesystem::create_directories(outputFile.parent_path());
    std::ofstream stream(outputFile, std::ios::binary | std::ios::trunc);
    if (!stream)
        return false;
    const std::uint32_t count = static_cast<std::uint32_t>(files.size());
    stream.write(Magic, sizeof(Magic));
    stream.write(reinterpret_cast<const char *>(&Version), sizeof(Version));
    stream.write(reinterpret_cast<const char *>(&count), sizeof(count));
    for (const auto &file : files)
    {
        const std::string relative = std::filesystem::relative(file, inputDirectory).generic_string();
        const auto data = PlatformFile::ReadAllBytes(file);
        const std::uint32_t pathLength = static_cast<std::uint32_t>(relative.size());
        const std::uint64_t byteSize = static_cast<std::uint64_t>(data.size());
        stream.write(reinterpret_cast<const char *>(&pathLength), sizeof(pathLength));
        stream.write(reinterpret_cast<const char *>(&byteSize), sizeof(byteSize));
        stream.write(relative.data(), static_cast<std::streamsize>(relative.size()));
        stream.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    return stream.good();
}

std::vector<PakEntry> PakArchive::List(const std::filesystem::path &archive)
{
    std::ifstream stream(archive, std::ios::binary);
    std::uint32_t count = 0;
    if (!ReadHeader(stream, count))
        return {};
    std::vector<PakEntry> entries;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t pathLength = 0;
        std::uint64_t byteSize = 0;
        stream.read(reinterpret_cast<char *>(&pathLength), sizeof(pathLength));
        stream.read(reinterpret_cast<char *>(&byteSize), sizeof(byteSize));
        if (!stream || pathLength > 1024 * 1024)
            return {};
        std::string path(pathLength, '\0');
        stream.read(path.data(), pathLength);
        stream.seekg(static_cast<std::streamoff>(byteSize), std::ios::cur);
        if (!stream)
            return {};
        entries.push_back({std::move(path), byteSize});
    }
    return entries;
}

std::optional<std::vector<std::uint8_t>> PakArchive::ReadFile(const std::filesystem::path &archive, const std::string &requestedPath)
{
    std::ifstream stream(archive, std::ios::binary);
    std::uint32_t count = 0;
    if (!ReadHeader(stream, count))
        return std::nullopt;
    const std::string normalized = std::filesystem::path(requestedPath).lexically_normal().generic_string();
    for (std::uint32_t index = 0; index < count; ++index)
    {
        std::uint32_t pathLength = 0;
        std::uint64_t byteSize = 0;
        stream.read(reinterpret_cast<char *>(&pathLength), sizeof(pathLength));
        stream.read(reinterpret_cast<char *>(&byteSize), sizeof(byteSize));
        if (!stream || pathLength > 1024 * 1024 || byteSize > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
            return std::nullopt;
        std::string path(pathLength, '\0');
        stream.read(path.data(), pathLength);
        if (path == normalized)
        {
            std::vector<std::uint8_t> data(static_cast<std::size_t>(byteSize));
            stream.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(byteSize));
            return stream ? std::optional<std::vector<std::uint8_t>>(std::move(data)) : std::nullopt;
        }
        stream.seekg(static_cast<std::streamoff>(byteSize), std::ios::cur);
    }
    return std::nullopt;
}
} // namespace MiniEngine
