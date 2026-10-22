// 文件：include/tinyengine/render/render_graph.h
// 作用：描述渲染通道及资源依赖，负责编译顺序、资源别名和执行。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：公开声明。

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tinyengine
{
enum class RenderGraphResourceType
{
    Texture,
    Buffer
};

struct RenderGraphResourceHandle
{
    // 0 保留为无效句柄，外部只能通过 RenderGraph 创建或导入资源。
    std::uint32_t id = 0;
    /// @brief 实现 `bool` 运算符语义。
    explicit operator bool() const
    {
        return id != 0;
    }
    /// @brief 实现 `==` 运算符语义。
    /// @param other 用于比较的另一个对象。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool operator==(const RenderGraphResourceHandle& other) const
    {
        return id == other.id;
    }
};

struct RenderGraphResourceDesc
{
    RenderGraphResourceType type = RenderGraphResourceType::Texture;
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::uint32_t format = 0;
    bool external = false;
    bool transient = true;
};

// 编译阶段计算出的逻辑资源生存期；生存期不重叠的瞬时资源可以共用 aliasSlot。
struct RenderGraphResourceLifetime
{
    std::size_t firstPass = 0;
    std::size_t lastPass = 0;
    std::uint32_t aliasSlot = 0;
};

struct RenderGraphPassDesc
{
    std::string name;
    std::vector<RenderGraphResourceHandle> reads;
    std::vector<RenderGraphResourceHandle> writes;
    std::function<void()> execute;
    bool sideEffect = false;
};

// 每帧重建的轻量渲染图。Compile 完成依赖排序、无效引用检查和瞬时资源别名分配。
class RenderGraph
{
  public:
    /// @brief 创建 `Resource`。
    /// @param desc 控制本次操作行为的配置。
    /// @return 返回接口计算或查询得到的结果。
    RenderGraphResourceHandle createResource(RenderGraphResourceDesc desc);
    /// @brief 导入 `Resource`。
    /// @param desc 控制本次操作行为的配置。
    /// @return 返回接口计算或查询得到的结果。
    RenderGraphResourceHandle importResource(RenderGraphResourceDesc desc);
    /// @brief 添加 `Pass`。
    /// @param pass 要执行的渲染通道。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t addPass(RenderGraphPassDesc pass);
    /// @brief 编译当前描述并生成可执行结果。
    /// @param error 可选的错误信息输出；可为空。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool compile(std::string* error = nullptr);
    /// @brief 执行已准备好的操作。
    /// @param error 可选的错误信息输出；可为空。
    /// @return 操作成功时返回 `true`，否则返回 `false`。
    bool execute(std::string* error = nullptr) const;
    /// @brief 将对象恢复为初始状态。
    void reset();

    /// @brief 获取 `ExecutionOrder`。
    /// @return 返回对应对象的引用；有效期不超过其所属对象。
    const std::vector<std::size_t>& GetExecutionOrder() const;
    /// @brief 获取 `Resource`。
    /// @param handle 目标资源或对象句柄。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const RenderGraphResourceDesc* getResource(RenderGraphResourceHandle handle) const;
    /// @brief 获取 `Lifetime`。
    /// @param handle 目标资源或对象句柄。
    /// @return 返回目标对象指针；对象不存在时返回 `nullptr`。
    const RenderGraphResourceLifetime* getLifetime(RenderGraphResourceHandle handle) const;
    /// @brief 获取 `PassCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getPassCount() const;
    /// @brief 获取 `PhysicalResourceCount`。
    /// @return 返回计算得到的数值、数量或索引。
    std::size_t getPhysicalResourceCount() const;

  private:
    /// @brief 判断是否满足 `Valid` 条件。
    /// @param handle 目标资源或对象句柄。
    /// @return 条件成立时返回 `true`，否则返回 `false`。
    bool isValid(RenderGraphResourceHandle handle) const;

    std::vector<RenderGraphResourceDesc> m_resources;
    std::vector<RenderGraphPassDesc> m_passes;
    std::vector<RenderGraphResourceLifetime> m_lifetimes;
    std::vector<std::size_t> m_executionOrder;
    std::size_t m_physicalResourceCount = 0;
    bool m_compiled = false;
};
} // namespace tinyengine
