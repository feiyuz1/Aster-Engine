#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

struct SDL_Window;

namespace tinyengine::vk
{
struct Buffer
{
    VkBuffer handle{};
    VkDeviceMemory memory{};
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};
struct Image
{
    VkImage handle{};
    VkDeviceMemory memory{};
    VkImageView view{};
    VkFormat format{};
    std::uint32_t width = 0, height = 0;
    std::uint32_t mipLevels = 1;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageUsageFlags usage = 0;
};
struct Slice
{
    VkBuffer buffer{};
    VkDeviceSize offset = 0, size = 0;
};

// Single-threaded recording on one graphics/compute queue. Shared offscreen
// resources are serialized by waiting for the last submitted frame in beginFrame.
// Resource destruction is the caller's responsibility after its GPU use ends.
class Context
{
  public:
    Context();
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    VkInstance instance{};
    VkPhysicalDevice physicalDevice{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t queueFamily = 0;
    VkExtent2D extent{};
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    std::string error;
    std::uint32_t validationErrors = 0;

    bool initialize(SDL_Window*, bool validation = true);
    void shutdown();
    bool beginFrame();
    void present();
    void waitIdle();
    bool isFatal() const;
    VkCommandBuffer command() const;
    VkImageView swapchainView() const;
    VkImage swapchainImage() const;
    std::uint32_t imageCount() const;

    Buffer createBuffer(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags);
    void destroy(Buffer&);
    Image createImage(std::uint32_t, std::uint32_t, VkFormat, VkImageUsageFlags,
                      VkImageAspectFlags = VK_IMAGE_ASPECT_COLOR_BIT, std::uint32_t mipLevels = 1);
    void destroy(Image&);
    // Requires active frame recording; uploadImage/readImage also establish a
    // temporary recording scope when called outside a frame. The destination
    // stage/access describe the next use; source use is tracked conservatively.
    void transition(Image&, VkImageLayout, VkPipelineStageFlags2 = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VkAccessFlags2 = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    // Active-frame-only allocations. The two 64 MiB coherent rings, reusable
    // overflow pages and transient descriptor pools remain valid until that
    // frame context's fence completes and the context is reused. Failure returns
    // an empty slice; the caller must not bind or copy it.
    Slice upload(const void*, VkDeviceSize, VkDeviceSize alignment = 256);
    VkDescriptorSet allocateDescriptor(VkDescriptorSetLayout);
    // Tightly packed data for one mip/layer; the caller supplies enough bytes
    // for that mip's format and TRANSFER_DST|SAMPLED image usage. Leaves all mips
    // in one shader-readable layout. Outside a frame it completes without acquire.
    // Returns false without recording a copy if staging cannot be allocated.
    bool uploadImage(Image&, const void*, VkDeviceSize, std::uint32_t mipLevel = 0);
    // Base mip must be initialized. Requires color, TRANSFER_SRC|TRANSFER_DST|
    // SAMPLED usage and format support for linear filtered blits. Returns false
    // before recording when unsupported; callers must not sample missing mips.
    // All mip levels are shader-readable on success. Compressed assets upload
    // their explicitly supplied mip payloads through uploadImage instead.
    bool generateMipmaps(Image&);
    // Requires TRANSFER_SRC usage, bytesPerPixel matching the format, a single
    // color/depth/stencil aspect, and no open dynamic-rendering scope. Restores
    // the prior image layout.
    // During a frame this submits ALL recorded work, consumes acquire if needed,
    // waits a fence, resets/rebegins the command buffer, and keeps the same image,
    // ring offsets and descriptor pools. All command-buffer binding state must be
    // rebound afterward. Outside a frame it uses a synchronous transfer command.
    // Intended for tests/screenshots, not normal frame rendering.
    std::vector<std::uint8_t> readImage(Image&, std::uint32_t bytesPerPixel, std::uint32_t mipLevel = 0);
    void beginSwapchainRendering(bool clear = false);
    // Establishes the color-write -> color-read/write dependency needed by a
    // subsequent LOAD overlay, including an externally recorded ImGui scope.
    void endSwapchainRendering();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool check(VkResult, const char*);
    bool recreateSwapchain();
    bool beginTransfer();
    bool finishTransfer();
    bool submitChunk(bool resume);
    void transitionSwapchain(VkImageLayout);
    std::uint32_t memoryType(std::uint32_t, VkMemoryPropertyFlags) const;
};
} // namespace tinyengine::vk
