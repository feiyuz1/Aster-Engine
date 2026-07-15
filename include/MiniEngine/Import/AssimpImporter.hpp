#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace MiniEngine
{
struct ModelImportOptions
{
    float scale = 1.0f;
    bool generateSmoothNormals = true;
    bool flipUvs = false;
    bool optimizeMeshes = true;
    bool compressAnimations = true;
    float animationPositionTolerance = 0.001f;
    float animationRotationToleranceDegrees = 0.1f;
    float animationScaleTolerance = 0.001f;
    std::filesystem::path resourcePrefix;
};

struct ImportedAsset
{
    std::string type;
    std::filesystem::path path;
};

struct ModelCompatibilityReport
{
    std::size_t sourceMeshes = 0;
    std::size_t sourceMaterials = 0;
    std::size_t sourceAnimations = 0;
    std::size_t importedBones = 0;
    std::size_t importedAnimationClips = 0;
    std::size_t importedMeshVertices = 0;
    std::size_t importedTriangles = 0;
    std::size_t truncatedBoneCount = 0;
    float animationCompressionRatio = 1.0f;
};

struct ModelImportResult
{
    bool success = false;
    std::filesystem::path prefabPath;
    std::vector<ImportedAsset> assets;
    std::vector<std::string> warnings;
    ModelCompatibilityReport compatibility;
    std::string error;
};

class AssimpImporter
{
  public:
    static bool CanImport(const std::filesystem::path &source);
    static std::vector<std::string> GetSupportedExtensions();
    static ModelImportResult ImportModel(const std::filesystem::path &source, const std::filesystem::path &outputDirectory,
                                         const ModelImportOptions &options = {});
};

class JobSystem;
enum class AssetImportStatus
{
    Invalid,
    Queued,
    Running,
    Completed,
    Failed,
    Cancelled
};
struct AssetImportSnapshot
{
    std::uint64_t id = 0;
    AssetImportStatus status = AssetImportStatus::Invalid;
    float progress = 0.0f;
    ModelImportResult result;
};

class AssetImportQueue
{
  public:
    void Initialize(JobSystem *jobs);
    std::uint64_t Submit(std::filesystem::path source, std::filesystem::path outputDirectory, ModelImportOptions options = {});
    bool Cancel(std::uint64_t id);
    AssetImportSnapshot Get(std::uint64_t id) const;
    void ClearFinished();

  private:
    struct TaskState;
    JobSystem *m_jobs = nullptr;
    std::uint64_t m_nextId = 1;
    mutable std::mutex m_mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<TaskState>> m_tasks;
};
} // namespace MiniEngine
