#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace MiniEngine
{
struct PakEntry
{
    std::string path;
    std::uint64_t byteSize = 0;
};

class PakArchive
{
  public:
    static bool PackDirectory(const std::filesystem::path &inputDirectory, const std::filesystem::path &outputFile);
    static std::vector<PakEntry> List(const std::filesystem::path &archive);
    static std::optional<std::vector<std::uint8_t>> ReadFile(const std::filesystem::path &archive, const std::string &path);
};
} // namespace MiniEngine
