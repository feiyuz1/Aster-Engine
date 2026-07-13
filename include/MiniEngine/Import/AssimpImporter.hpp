#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace MiniEngine
{
    struct ModelImportOptions
    {
        float scale = 1.0f;
        bool generateSmoothNormals = true;
        bool flipUvs = false;
        bool optimizeMeshes = true;
        std::filesystem::path resourcePrefix;
    };

    struct ImportedAsset
    {
        std::string type;
        std::filesystem::path path;
    };

    struct ModelImportResult
    {
        bool success = false;
        std::filesystem::path prefabPath;
        std::vector<ImportedAsset> assets;
        std::vector<std::string> warnings;
        std::string error;
    };

    class AssimpImporter
    {
    public:
        static bool CanImport(const std::filesystem::path& source);
        static std::vector<std::string> GetSupportedExtensions();
        static ModelImportResult ImportModel(const std::filesystem::path& source, const std::filesystem::path& outputDirectory, const ModelImportOptions& options = {});
    };
}
