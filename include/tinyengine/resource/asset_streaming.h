// 文件：include/tinyengine/resource/asset_streaming.h
// 作用：根据依赖、距离和内存预算调度大型资产的分块流式加载。
// 约定：资源对象可能异步完成，调用方必须检查状态或显式等待。
// 类型：公开声明。

#pragma once

#include "tinyengine/resource/asset_registry.h"
#include "tinyengine/resource/resource_manager.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tinyengine
{
template <typename T> class AssetHandle
{
  public:
    /// @brief 构造 `AssetHandle` 对象。
    AssetHandle() = default;
    /// @brief 构造 `AssetHandle` 对象。
    /// @param path 文件、资源或目录路径。
    /// @param resource 要跟踪、加载或释放的资源对象。
    AssetHandle(std::string path, std::shared_ptr<T> resource)
        : m_path(std::move(path)), m_resource(std::move(resource))
    {
    }
    /// @brief 获取 `Path`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::string& GetPath() const
    {
        return m_path;
    }
    /// @brief 判断是否满足 `Valid` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isValid() const
    {
        return static_cast<bool>(m_resource);
    }
    /// @brief 判断是否满足 `Ready` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isReady() const
    {
        return m_resource && m_resource->isReady();
    }
    /// @brief 等待操作完成，并在失败时传播错误。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool wait() const
    {
        return m_resource && m_resource->wait();
    }
    /// @brief 获取目标对象或值。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    T* Get() const
    {
        return isReady() ? m_resource.get() : nullptr;
    }
    /// @brief 获取 `Shared`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::shared_ptr<T>& GetShared() const
    {
        return m_resource;
    }

  private:
    std::string m_path;
    std::shared_ptr<T> m_resource;
};

// 从 Registry 构建只读依赖图，并能计算传递闭包及报告循环依赖。
class AssetDependencyGraph
{
  public:
    /// @brief 根据当前输入构建结果。
    /// @param registry 要查询或更新的资产注册表。
    void build(const AssetRegistry& registry);
    /// @brief 获取 `Dependencies`。
    /// @param path 文件、资源或目录路径。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<std::string> GetDependencies(const std::string& path) const;
    /// @brief 解析 `Closure`。
    /// @param path 文件、资源或目录路径。
    /// @param missing 接收缺失依赖列表的可选输出参数。
    /// @return 返回查询或处理得到的结果集合。
    std::vector<std::string> ResolveClosure(const std::string& path, std::vector<std::string>* missing = nullptr) const;
    /// @brief 判断是否包含 `Cycles` 条件。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool hasCycles() const;

  private:
    /// @brief 访问节点并检测重复或循环。
    /// @param path 文件、资源或目录路径。
    /// @param visiting 由接口读取并可能修改的输入/输出参数。
    /// @param visited 由接口读取并可能修改的输入/输出参数。
    /// @param output 接收调用结果的输出参数。
    /// @param missing 接收缺失依赖列表的可选输出参数。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool Visit(const std::string& path, std::unordered_set<std::string>& visiting,
               std::unordered_set<std::string>& visited, std::vector<std::string>& output,
               std::vector<std::string>* missing) const;
    std::unordered_map<std::string, std::vector<std::string>> m_dependencies;
    std::unordered_set<std::string> m_knownAssets;
    bool m_hasCycles = false;
};

// 基于请求优先级和预算推进流式状态机；Update 只提交有限工作，避免单帧加载尖峰。
class AssetStreamingManager
{
  public:
    /// @brief 初始化对象及其依赖资源。
    /// @param resources 相关文件、资源或目录路径。
    /// @param memoryBudgetBytes 目标尺寸、数量、索引或限制值。
    void initialize(ResourceManager* resources, std::size_t memoryBudgetBytes = 256 * 1024 * 1024);
    /// @brief 停止服务并释放对象持有的资源。
    void shutdown();

    /// @brief 提交资源或操作请求。
    /// @param path 文件、资源或目录路径。
    /// @param pinned 是否固定资源以禁止自动回收。
    /// @return 返回查询或处理得到的结果集合。
    template <typename T> AssetHandle<T> Request(const std::string& path, const bool pinned = false)
    {
        if (!m_resources)
            return {};
        auto resource = m_resources->loadAsync<T>(path);
        track(path, resource, pinned);
        return {path, std::move(resource)};
    }

    /// @brief 设置资源是否禁止自动回收。
    /// @param path 文件、资源或目录路径。
    /// @param pinned 是否固定资源以禁止自动回收。
    void pin(const std::string& path, bool pinned = true);
    /// @brief 更新对象最近一次使用时间。
    /// @param path 文件、资源或目录路径。
    void touch(const std::string& path);
    /// @brief 更新对象当前状态。
    void Update();
    /// @brief 设置 `MemoryBudget`。
    /// @param bytes 要处理的字节数据或字节数量。
    void setMemoryBudget(std::size_t bytes);
    /// @brief 获取 `MemoryBudget`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getMemoryBudget() const;
    /// @brief 获取 `ResidentBytes`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getResidentBytes() const;
    /// @brief 获取 `TrackedAssetCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getTrackedAssetCount() const;
    /// @brief 获取 `DependencyGraph`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const AssetDependencyGraph& GetDependencyGraph() const;
    /// @brief 重新构建 `DependencyGraph`。
    void rebuildDependencyGraph();

  private:
    struct Entry
    {
        std::string path;
        std::weak_ptr<Resource> resource;
        std::uint64_t lastTouched = 0;
        bool pinned = false;
    };
    /// @brief 开始跟踪资源及其使用状态。
    /// @param path 文件、资源或目录路径。
    /// @param resource 要跟踪、加载或释放的资源对象。
    /// @param pinned 是否固定资源以禁止自动回收。
    void track(const std::string& path, const std::shared_ptr<Resource>& resource, bool pinned);

    ResourceManager* m_resources = nullptr;
    std::unordered_map<std::string, Entry> m_entries;
    AssetDependencyGraph m_graph;
    std::size_t m_budget = 256 * 1024 * 1024;
    std::uint64_t m_tick = 0;
};
} // namespace tinyengine
