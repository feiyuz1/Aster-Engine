// 文件：include/tinyengine/import/assimp_importer.h
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

namespace tinyengine
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
    /// @brief 判断是否可以 `Import` 条件。
    /// @param source 源数据、源资源或源位置。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    static bool canImport(const std::filesystem::path& source);
    /// @brief 获取 `SupportedExtensions`。
    /// @return 返回查询或处理得到的结果集合。
    static std::vector<std::string> GetSupportedExtensions();
    /// @brief 导入 `Model`。
    /// @param source 源数据、源资源或源位置。
    /// @param outputDirectory 生成文件的输出目录。
    /// @param options 控制本次操作的选项。
    /// @return 返回接口计算或查询得到的结果。
    static ModelImportResult importModel(const std::filesystem::path& source,
                                         const std::filesystem::path& outputDirectory,
                                         const ModelImportOptions& options = {});
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
    /// @brief 初始化对象及其依赖资源。
    /// @param jobs 接口所需的输入参数。
    void initialize(JobSystem* jobs);
    /// @brief 提交任务并返回跟踪信息。
    /// @param source 源数据、源资源或源位置。
    /// @param outputDirectory 生成文件的输出目录。
    /// @param options 控制本次操作的选项。
    /// @return 返回计算得到的数值、数量或索引。
    std::uint64_t submit(std::filesystem::path source, std::filesystem::path outputDirectory,
                         ModelImportOptions options = {});
    /// @brief 判断是否可以 `cel` 条件。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool Cancel(std::uint64_t id);
    /// @brief 获取目标对象或值。
    /// @param id 目标对象、消息或任务标识符。
    /// @return 返回查询或处理得到的结果集合。
    AssetImportSnapshot Get(std::uint64_t id) const;
    /// @brief 清空 `Finished`。
    void clearFinished();

  private:
    struct TaskState;
    JobSystem* m_jobs = nullptr;
    std::uint64_t m_nextId = 1;
    mutable std::mutex m_mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<TaskState>> m_tasks;
};
} // namespace tinyengine
