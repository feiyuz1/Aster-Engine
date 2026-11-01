#include "../src/render/vulkan_context.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
int failures = 0;
VkResult nextAcquireFailure = VK_SUCCESS;
std::uint32_t acquireCalls = 0;
void check(bool result, const char* label)
{
    std::cout << (result ? "[PASS] " : "[FAIL] ") << label << '\n';
    failures += !result;
}

// Only terminal acquisition failure is injected; successful acquisitions still
// execute against the real surface, semaphores, driver and validation layer.
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
                                                                std::uint64_t timeout, VkSemaphore semaphore,
                                                                VkFence fence, std::uint32_t* imageIndex)
{
    ++acquireCalls;
    if (nextAcquireFailure != VK_SUCCESS)
    {
        const auto failure = nextAcquireFailure;
        nextAcquireFailure = VK_SUCCESS;
        return failure;
    }
    const auto acquire =
        reinterpret_cast<PFN_vkAcquireNextImageKHR>(vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR"));
    return acquire(device, swapchain, timeout, semaphore, fence, imageIndex);
}
void pump()
{
    SDL_PumpEvents();
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
    }
}
} // namespace

int main()
{
    // Missing transfer barriers, premature ring reuse, and unsafe swapchain
    // semaphore reuse must fail these real GPU checks or synchronization validation.
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Vulkan context regression", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          96, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    check(window != nullptr, "create Vulkan SDL window");
    if (!window)
    {
        SDL_Quit();
        return 1;
    }
    tinyengine::vk::Context context;
    check(context.initialize(window, true), "initialize real Vulkan 1.3 with validation");
    if (!context.device)
    {
        std::cerr << context.error << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    check(context.instance && context.physicalDevice && context.queue && context.imageCount() >= 2,
          "expose device and swapchain handles");
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(context.physicalDevice, &deviceProperties);
    std::cout << "GPU: " << deviceProperties.deviceName << '\n';
    auto mapped = context.createBuffer(64, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(mapped.handle && mapped.mapped && mapped.size == 64, "create host coherent mapped buffer");
    context.destroy(mapped);
    check(!mapped.handle && !mapped.memory && !mapped.mapped, "destroy clears buffer ownership");

    auto image = context.createImage(4, 2, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    check(image.handle && image.view && image.width == 4 && image.height == 2, "create offscreen image");
    const std::vector<std::uint8_t> pattern = {255, 0,   0,   255, 0,   255, 0,  255, 0,   0,  255,
                                               255, 255, 255, 0,   255, 8,   16, 32,  255, 64, 96,
                                               128, 255, 160, 192, 224, 255, 3,  7,   11,  255};
    // Resource upload/readback outside a frame does not acquire a swapchain image.
    check(context.uploadImage(image, pattern.data(), pattern.size()), "report successful image upload");
    check(context.readImage(image, 4) == pattern, "upload and read back exact asymmetric RGBA pixels");
    check(!context.command(), "standalone transfers leave no frame recording");
    check(!context.uploadImage(image, pattern.data(), std::numeric_limits<VkDeviceSize>::max()) && !context.command() &&
              context.readImage(image, 4) == pattern,
          "failed standalone staging reports failure, closes recording and preserves image contents");
    context.error.clear();
    {
        auto mipImage = context.createImage(4, 2, VK_FORMAT_R8G8B8A8_UNORM,
                                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                VK_IMAGE_USAGE_SAMPLED_BIT,
                                            VK_IMAGE_ASPECT_COLOR_BIT, 2);
        const std::vector<std::uint8_t> smallerMip{13, 27, 51, 255, 71, 91, 111, 255};
        check(mipImage.handle && mipImage.mipLevels == 2, "create a texture containing the requested mip levels");
        if (mipImage.handle)
        {
            check(context.uploadImage(mipImage, pattern.data(), pattern.size()) &&
                      context.uploadImage(mipImage, smallerMip.data(), smallerMip.size(), 1),
                  "upload independent asymmetric payloads to two mip levels");
            check(context.readImage(mipImage, 4) == pattern && context.readImage(mipImage, 4, 1) == smallerMip,
                  "read back both explicit mip levels without overwriting base contents");
            check(!context.uploadImage(mipImage, smallerMip.data(), smallerMip.size(), 2) &&
                      context.readImage(mipImage, 4, 2).empty() && !context.command(),
                  "reject out-of-range mip uploads and readbacks before recording");
            context.destroy(mipImage);
        }
        auto invalidMips = context.createImage(4, 2, VK_FORMAT_R8G8B8A8_UNORM,
                                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                               VK_IMAGE_ASPECT_COLOR_BIT, 4);
        check(!invalidMips.handle, "reject mip counts larger than the image dimensions permit");
        context.destroy(invalidMips);
        invalidMips = context.createImage(4, 2, VK_FORMAT_R8G8B8A8_UNORM,
                                          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                          VK_IMAGE_ASPECT_COLOR_BIT, 0);
        check(!invalidMips.handle, "reject zero mip levels before Vulkan allocation");
        context.destroy(invalidMips);
        context.error.clear();
    }
    {
        auto generated = context.createImage(4, 4, VK_FORMAT_R8G8B8A8_UNORM,
                                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                 VK_IMAGE_USAGE_SAMPLED_BIT,
                                             VK_IMAGE_ASPECT_COLOR_BIT, 3);
        const std::vector<std::uint8_t> baseMip{
            0,   0,   0,  255, 0,   0,   0,  255, 64,  128, 192, 255, 64,  128, 192, 255, 0,   0,   0,   255, 0,  0,
            0,   255, 64, 128, 192, 255, 64, 128, 192, 255, 128, 64,  0,   255, 128, 64,  0,   255, 192, 192, 64, 255,
            192, 192, 64, 255, 128, 64,  0,  255, 128, 64,  0,   255, 192, 192, 64,  255, 192, 192, 64,  255};
        const std::vector<std::uint8_t> halfMip{0, 0, 0, 255, 64, 128, 192, 255, 128, 64, 0, 255, 192, 192, 64, 255};
        const std::vector<std::uint8_t> lastMip{96, 96, 64, 255};
        check(generated.handle && context.uploadImage(generated, baseMip.data(), baseMip.size()) &&
                  context.generateMipmaps(generated),
              "generate the complete RGBA mip chain using supported linear blits");
        if (generated.handle)
        {
            check(context.readImage(generated, 4) == baseMip && context.readImage(generated, 4, 1) == halfMip &&
                      context.readImage(generated, 4, 2) == lastMip &&
                      generated.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  "generated mip pixels preserve quadrants, average correctly and restore the complete image layout");
            context.destroy(generated);
        }
        auto integerMips = context.createImage(4, 4, VK_FORMAT_R32_UINT,
                                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                   VK_IMAGE_USAGE_SAMPLED_BIT,
                                               VK_IMAGE_ASPECT_COLOR_BIT, 3);
        const std::array<std::uint32_t, 16> integerPixels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        if (integerMips.handle)
        {
            check(context.uploadImage(integerMips, integerPixels.data(), sizeof(integerPixels)) &&
                      !context.generateMipmaps(integerMips) && !context.command(),
                  "unsupported linear mip filtering reports failure before recording a blit");
            const auto preserved = context.readImage(integerMips, sizeof(std::uint32_t));
            check(preserved.size() == sizeof(integerPixels) &&
                      std::memcmp(preserved.data(), integerPixels.data(), sizeof(integerPixels)) == 0,
                  "unsupported generation preserves the initialized base image");
            context.destroy(integerMips);
        }
        context.error.clear();
    }
    // An HDR image can exceed the ordinary upload ring even before any draws.
    {
        auto hdr = context.createImage(4096, 2304, VK_FORMAT_R16G16B16A16_SFLOAT,
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                           VK_IMAGE_USAGE_SAMPLED_BIT);
        std::vector<std::uint8_t> hdrPixels(4096u * 2304u * 8u, 0x3c);
        check(context.uploadImage(hdr, hdrPixels.data(), hdrPixels.size()), "report successful oversized HDR upload");
        check(context.readImage(hdr, 8) == hdrPixels,
              "standalone HDR image larger than 64 MiB uploads and reads back exactly");
        check(!context.command(), "oversized standalone transfer closes its recording scope");
        context.destroy(hdr);
    }
    tinyengine::vk::Image compressed;
    tinyengine::vk::Buffer compressedReadback;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(context.physicalDevice, &deviceFeatures);
    const std::array<std::uint8_t, 8> bc1Block{0, 0xf8, 0, 0, 0, 0, 0, 0};
    const std::array<std::uint8_t, 8> bc1Mip{0xe0, 0x07, 0, 0, 0, 0, 0, 0};
    std::array<std::uint8_t, 32> bc1Base{};
    for (std::size_t offset = 0; offset < bc1Base.size(); offset += bc1Block.size())
        std::memcpy(bc1Base.data() + offset, bc1Block.data(), bc1Block.size());
    if (deviceFeatures.textureCompressionBC)
    {
        compressed = context.createImage(8, 8, VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                             VK_IMAGE_USAGE_SAMPLED_BIT,
                                         VK_IMAGE_ASPECT_COLOR_BIT, 2);
        compressedReadback =
            context.createBuffer(40, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(compressed.handle && compressedReadback.handle, "create BC1 image when compression is supported");
        if (compressed.handle)
            check(context.uploadImage(compressed, bc1Base.data(), bc1Base.size()) &&
                      context.uploadImage(compressed, bc1Mip.data(), bc1Mip.size(), 1),
                  "upload both explicitly supplied BC1 mip levels");
    }

    check(context.beginFrame(), "acquire and begin frame");
    check(context.command() && context.swapchainView() && context.swapchainImage(), "expose acquired frame handles");
    {
        auto narrow = context.createImage(2, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                              VK_IMAGE_USAGE_SAMPLED_BIT,
                                          VK_IMAGE_ASPECT_COLOR_BIT, 2);
        const std::array<std::uint8_t, 8> base{0, 32, 64, 255, 128, 96, 192, 255};
        const std::vector<std::uint8_t> averaged{64, 64, 128, 255};
        check(narrow.handle && context.uploadImage(narrow, base.data(), base.size()) &&
                  context.generateMipmaps(narrow) && context.readImage(narrow, 4, 1) == averaged && context.command(),
              "generate a one-dimensional mip inside an active frame and resume after readback");
        context.destroy(narrow);
    }
    const std::array<std::uint32_t, 4> values{0x12345678u, 0xabcdef01u, 42u, 17u};
    auto first = context.upload(values.data(), sizeof(values), 256);
    auto second = context.upload(values.data(), sizeof(values), 256);
    check(first.buffer && first.buffer == second.buffer && second.offset >= first.offset + first.size &&
              first.offset % 256 == 0 && second.offset % 256 == 0,
          "uploads share a non-overlapping aligned frame ring");
    auto unusualAlignment = context.upload(values.data(), sizeof(values), 300);
    check(unusualAlignment.buffer && unusualAlignment.offset % 300 == 0 &&
              unusualAlignment.offset % deviceProperties.limits.minUniformBufferOffsetAlignment == 0,
          "ring satisfies caller and device alignment together");
    auto overflowReadback =
        context.createBuffer(48, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    std::vector<std::uint8_t> uploadPage(24u * 1024u * 1024u);
    std::array<tinyengine::vk::Slice, 3> largeSlices{};
    bool overflowValid = overflowReadback.handle != VK_NULL_HANDLE;
    for (std::size_t i = 0; i < largeSlices.size(); ++i)
    {
        std::fill(uploadPage.begin(), uploadPage.end(), static_cast<std::uint8_t>(0x31 + i));
        largeSlices[i] = context.upload(uploadPage.data(), uploadPage.size(), 256);
        overflowValid &= largeSlices[i].buffer != VK_NULL_HANDLE;
        if (largeSlices[i].buffer && overflowReadback.handle)
        {
            VkBufferCopy copy{largeSlices[i].offset + largeSlices[i].size - 16, i * 16, 16};
            vkCmdCopyBuffer(context.command(), largeSlices[i].buffer, overflowReadback.handle, 1, &copy);
        }
    }
    check(overflowValid, "multiple general uploads continue after the primary frame ring is full");
    if (overflowReadback.handle)
    {
        VkBufferMemoryBarrier2 host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = overflowReadback.handle;
        host.size = VK_WHOLE_SIZE;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &host;
        vkCmdPipelineBarrier2(context.command(), &dependency);
    }
    context.transition(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                       VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkClearColorValue clear{{0.25f, 0.5f, 0.75f, 1.0f}};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(context.command(), image.handle, image.layout, &clear, 1, &range);
    if (compressed.handle && compressedReadback.handle)
    {
        context.transition(compressed, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                           VK_ACCESS_2_TRANSFER_READ_BIT);
        std::array<VkBufferImageCopy, 2> blockCopy{};
        blockCopy[0].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blockCopy[0].imageExtent = {8, 8, 1};
        blockCopy[1].bufferOffset = 32;
        blockCopy[1].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 1, 0, 1};
        blockCopy[1].imageExtent = {4, 4, 1};
        vkCmdCopyImageToBuffer(context.command(), compressed.handle, compressed.layout, compressedReadback.handle,
                               static_cast<std::uint32_t>(blockCopy.size()), blockCopy.data());
        VkBufferMemoryBarrier2 host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = compressedReadback.handle;
        host.size = VK_WHOLE_SIZE;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.bufferMemoryBarrierCount = 1;
        dependency.pBufferMemoryBarriers = &host;
        vkCmdPipelineBarrier2(context.command(), &dependency);
        context.transition(compressed, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    auto pixels = context.readImage(image, 4);
    if (overflowValid)
    {
        const auto* readbackBytes = static_cast<const std::uint8_t*>(overflowReadback.mapped);
        for (std::size_t i = 0; i < 48; ++i)
            overflowValid &= readbackBytes[i] == 0x31 + i / 16;
    }
    check(overflowValid, "overflow allocations preserve all earlier upload bytes until GPU completion");
    check(pixels.size() == 32 && pixels[0] >= 63 && pixels[0] <= 64 && pixels[1] >= 127 && pixels[1] <= 128 &&
              pixels[2] >= 191 && pixels[2] <= 192 && pixels[3] == 255,
          "submit recording chunk and read GPU clear");
    check(context.command() != VK_NULL_HANDLE, "readback resumes active frame recording");
    if (compressed.handle && compressedReadback.handle)
        check(std::memcmp(compressedReadback.mapped, bc1Base.data(), bc1Base.size()) == 0 &&
                  std::memcmp(static_cast<const std::uint8_t*>(compressedReadback.mapped) + 32, bc1Mip.data(),
                              bc1Mip.size()) == 0,
              "all compressed mip bytes survive whole-image transitions without uncompressed-size assumptions");
    context.destroy(compressed);
    context.destroy(compressedReadback);
    const auto afterReadback = context.upload(values.data(), sizeof(values), 256);
    check(afterReadback.buffer == first.buffer &&
              afterReadback.offset >= unusualAlignment.offset + unusualAlignment.size,
          "readback preserves frame ring allocations");
    {
        auto rgba = context.createImage(4096, 4096, VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                            VK_IMAGE_USAGE_SAMPLED_BIT);
        std::vector<std::uint8_t> rgbaPixels(4096u * 4096u * 4u, 0xa7);
        rgbaPixels.front() = 0x13;
        rgbaPixels.back() = 0xeb;
        check(context.uploadImage(rgba, rgbaPixels.data(), rgbaPixels.size()),
              "report successful full-ring RGBA upload");
        if (overflowValid)
        {
            VkBufferCopy copy{largeSlices[2].offset + largeSlices[2].size - 16, 32, 16};
            vkCmdCopyBuffer(context.command(), largeSlices[2].buffer, overflowReadback.handle, 1, &copy);
            VkBufferMemoryBarrier2 host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            host.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            host.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            host.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
            host.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
            host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.buffer = overflowReadback.handle;
            host.size = VK_WHOLE_SIZE;
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.bufferMemoryBarrierCount = 1;
            dependency.pBufferMemoryBarriers = &host;
            vkCmdPipelineBarrier2(context.command(), &dependency);
        }
        check(context.readImage(rgba, 4) == rgbaPixels,
              "4096 squared RGBA upload succeeds after earlier frame allocations and chunk submission");
        if (overflowValid)
        {
            const auto* bytes = static_cast<const std::uint8_t*>(overflowReadback.mapped);
            overflowValid = std::all_of(bytes + 32, bytes + 48, [](auto value) { return value == 0x33; });
        }
        check(overflowValid, "readback chunks preserve overflow pages for later commands in the same frame");
        context.destroy(rgba);
    }
    context.destroy(overflowReadback);
    auto depth = context.createImage(4, 2, VK_FORMAT_D32_SFLOAT,
                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                     VK_IMAGE_ASPECT_DEPTH_BIT);
    check(depth.handle != VK_NULL_HANDLE, "create sampled depth attachment");
    if (depth.handle)
    {
        context.transition(depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                           VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                           VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depthAttachment.imageView = depth.view;
        depthAttachment.imageLayout = depth.layout;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {0.375f, 0};
        VkRenderingInfo renderDepth{VK_STRUCTURE_TYPE_RENDERING_INFO};
        renderDepth.renderArea.extent = {4, 2};
        renderDepth.layerCount = 1;
        renderDepth.pDepthAttachment = &depthAttachment;
        vkCmdBeginRendering(context.command(), &renderDepth);
        vkCmdEndRendering(context.command());
        const auto depthPixels = context.readImage(depth, 4);
        float firstDepth = 0;
        if (depthPixels.size() >= 4)
            std::memcpy(&firstDepth, depthPixels.data(), 4);
        check(depthPixels.size() == 32 && firstDepth == 0.375f &&
                  depth.layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
              "read depth after a second recording chunk and restore its attachment layout");
        context.destroy(depth);
    }
    context.beginSwapchainRendering(true);
    context.endSwapchainRendering();
    auto beginKHR =
        reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(context.device, "vkCmdBeginRenderingKHR"));
    auto endKHR =
        reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(context.device, "vkCmdEndRenderingKHR"));
    check(beginKHR && endKHR, "enable editor dynamic-rendering KHR aliases");
    if (beginKHR && endKHR)
    {
        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = context.swapchainView();
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo overlay{VK_STRUCTURE_TYPE_RENDERING_INFO};
        overlay.renderArea.extent = context.extent;
        overlay.layerCount = 1;
        overlay.colorAttachmentCount = 1;
        overlay.pColorAttachments = &color;
        beginKHR(context.command(), &overlay);
        endKHR(context.command());
    }
    context.present();
    check(!context.command(), "present ends frame recording");

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    VkDescriptorSetLayout layout{};
    check(vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &layout) == VK_SUCCESS,
          "create test descriptor layout");
    bool repeated = true;
    VkBuffer ringBuffers[2]{};
    VkBuffer overflowBuffers[2]{};
    bool pagesReused = true;
    for (int frame = 0; frame < 24; ++frame)
    {
        pump();
        if (!context.beginFrame())
        {
            repeated = false;
            break;
        }
        auto slice = context.upload(values.data(), sizeof(values), 256);
        if (frame < 2)
            ringBuffers[frame] = slice.buffer;
        if (frame < 4)
        {
            for (int allocation = 0; allocation < 3; ++allocation)
                slice = context.upload(uploadPage.data(), uploadPage.size(), 256);
            pagesReused &= slice.buffer && slice.buffer != ringBuffers[frame % 2];
            if (frame < 2)
                overflowBuffers[frame] = slice.buffer;
            else
                pagesReused &= slice.buffer == overflowBuffers[frame % 2];
        }
        for (int set = 0; set < (frame < 2 ? 4200 : 40); ++set)
            repeated &= context.allocateDescriptor(layout) != VK_NULL_HANDLE;
        context.beginSwapchainRendering(true);
        context.endSwapchainRendering();
        context.present();
    }
    check(repeated && ringBuffers[0] && ringBuffers[1] && ringBuffers[0] != ringBuffers[1],
          "24 frames recycle two rings and frame descriptor pools");
    check(pagesReused, "overflow pages recycle after each owning frame fence completes");
    context.waitIdle();
    vkDestroyDescriptorSetLayout(context.device, layout, nullptr);

    SDL_SetWindowSize(window, 137, 91);
    pump();
    check(context.beginFrame(), "begin resized frame");
    check(context.extent.width == 137 && context.extent.height == 91, "recreate swapchain at resized extent");
    context.beginSwapchainRendering(true);
    context.endSwapchainRendering();
    context.present();
    SDL_MinimizeWindow(window);
    SDL_Delay(50);
    pump();
    check(!context.beginFrame(), "minimized window defers acquire without resetting a frame fence");
    SDL_RestoreWindow(window);
    SDL_Delay(50);
    pump();
    check(context.beginFrame(), "restored window resumes frame submission");
    context.beginSwapchainRendering(true);
    context.endSwapchainRendering();
    context.present();
    context.waitIdle();
    nextAcquireFailure = VK_ERROR_SURFACE_LOST_KHR;
    check(!context.beginFrame(), "surface loss acquisition fails the frame");
    const auto failedAcquireCalls = acquireCalls;
    const bool stopped = !context.beginFrame() && acquireCalls == failedAcquireCalls;
    check(stopped && context.isFatal(), "terminal acquisition error prevents later acquire and submission");
    if (context.command())
    {
        context.beginSwapchainRendering(true);
        context.endSwapchainRendering();
        context.present();
        context.waitIdle();
    }
    context.error.clear();
    context.destroy(image);
    check(!image.handle && !image.view && !image.memory, "destroy clears image ownership");
    context.shutdown();
    check(context.validationErrors == 0, "zero Vulkan validation errors including cleanup");
    check(!context.device && !context.instance, "shutdown clears Vulkan handles");
    context.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!context.error.empty())
        std::cerr << "Context diagnostic: " << context.error << '\n';
    return failures ? 1 : 0;
}
