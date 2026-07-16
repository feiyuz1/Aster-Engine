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
