// 文件：include/MiniEngine/Import/AssimpImporter.hpp
// 作用：通过 Assimp 导入 FBX、glTF、OBJ 等模型及骨骼动画，并生成引擎资产。
// 约定：导入阶段处理不可信外部数据，必须保留边界校验和错误上下文。
// 类型：公开声明。

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
// 导入选项只描述源到引擎格式的转换；resourcePrefix 决定生成资产中记录的逻辑路径。
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

// 面向兼容性测试和编辑器诊断的统计，不参与运行时资源加载。
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

// 无状态导入入口。ImportModel 先完整构建中间结果，成功后才写出 Prefab 及其依赖。
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

// 编辑器异步导入队列。Snapshot 是锁内状态的副本，可安全地在 UI 线程轮询。
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
