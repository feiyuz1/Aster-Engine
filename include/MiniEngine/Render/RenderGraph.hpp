// 文件：include/MiniEngine/Render/RenderGraph.hpp
// 作用：描述渲染通道及资源依赖，负责编译顺序、资源别名和执行。
// 约定：GPU 对象只应在拥有图形上下文的线程创建、使用和销毁。
// 类型：公开声明。

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace MiniEngine
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
    explicit operator bool() const
    {
        return id != 0;
    }
    bool operator==(const RenderGraphResourceHandle &other) const
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
    RenderGraphResourceHandle CreateResource(RenderGraphResourceDesc desc);
    RenderGraphResourceHandle ImportResource(RenderGraphResourceDesc desc);
    std::size_t AddPass(RenderGraphPassDesc pass);
    bool Compile(std::string *error = nullptr);
    bool Execute(std::string *error = nullptr) const;
    void Reset();

    const std::vector<std::size_t> &GetExecutionOrder() const;
    const RenderGraphResourceDesc *GetResource(RenderGraphResourceHandle handle) const;
    const RenderGraphResourceLifetime *GetLifetime(RenderGraphResourceHandle handle) const;
    std::size_t GetPassCount() const;
    std::size_t GetPhysicalResourceCount() const;

  private:
    bool IsValid(RenderGraphResourceHandle handle) const;

    std::vector<RenderGraphResourceDesc> m_resources;
    std::vector<RenderGraphPassDesc> m_passes;
    std::vector<RenderGraphResourceLifetime> m_lifetimes;
    std::vector<std::size_t> m_executionOrder;
    std::size_t m_physicalResourceCount = 0;
    bool m_compiled = false;
};
} // namespace MiniEngine
