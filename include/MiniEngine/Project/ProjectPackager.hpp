#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
    struct PackageOptions
    {
        std::filesystem::path projectFile;
        std::filesystem::path outputDirectory;
        std::filesystem::path gameExecutable;
    };

    struct PackageResult
    {
        bool success = false;
        std::filesystem::path outputDirectory;
        std::filesystem::path executable;
        std::filesystem::path projectFile;
        std::filesystem::path archive;
        std::size_t stagedFiles = 0;
        std::uint64_t archiveBytes = 0;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
    };

    class ProjectPackager
    {
    public:
        static PackageResult Package(const PackageOptions& options);
    };
}
