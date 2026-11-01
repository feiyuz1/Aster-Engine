#include "editor_gui_renderer.h"
#include "tinyengine/render/renderer.h"
#include "tinyengine/render/sdl_gl_render_device.h"
#include <SDL.h>
#include <algorithm>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <imgui.h>
#include <unordered_map>

#ifdef TINYENGINE_HAS_VULKAN
#include "tinyengine/render/sdl_vulkan_render_device.h"
#include <backends/imgui_impl_vulkan.h>
#endif

namespace tinyengine
{
struct EditorGuiRenderer::Impl
{
    RenderDevice* device = nullptr;
    bool platformInitialized = false, rendererInitialized = false;
#ifdef TINYENGINE_HAS_VULKAN
    SdlVulkanRenderDevice* vulkan = nullptr;
    VkDevice nativeDevice{};
    VkDescriptorPool descriptorPool{};
    VkFormat colorFormat{};
    VkDescriptorSet sceneDescriptor{};
    VkImageView sceneView{};
    std::uint64_t sceneGeneration = 0;
    VkCommandBuffer frameCommand{};
    std::unordered_map<VkImageView, VkDescriptorSet> uiDescriptors;
#endif
};

EditorGuiRenderer::EditorGuiRenderer() : m_impl(std::make_unique<Impl>()) {}
EditorGuiRenderer::~EditorGuiRenderer()
{
    shutdown();
}

bool EditorGuiRenderer::initialize(RenderDevice& device)
{
    shutdown();
    m_impl->device = &device;
    auto* window = static_cast<SDL_Window*>(device.getNativeWindow());
    if (!window)
        return false;
    if (device.getBackend() == RenderBackend::OpenGL)
    {
        auto* gl = dynamic_cast<SdlGlRenderDevice*>(&device);
        if (!gl)
            return false;
        m_impl->platformInitialized = ImGui_ImplSDL2_InitForOpenGL(window, gl->getGlContext());
        if (m_impl->platformInitialized)
            m_impl->rendererInitialized = ImGui_ImplOpenGL3_Init("#version 330");
    }
#ifdef TINYENGINE_HAS_VULKAN
    else if (device.getBackend() == RenderBackend::Vulkan)
    {
        m_impl->vulkan = dynamic_cast<SdlVulkanRenderDevice*>(&device);
        if (!m_impl->vulkan)
            return false;
        const auto native = m_impl->vulkan->getVulkanInterop();
        m_impl->nativeDevice = native.device;
        m_impl->colorFormat = native.colorFormat;
        m_impl->platformInitialized = ImGui_ImplSDL2_InitForVulkan(window);
        const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096},
                                              {VK_DESCRIPTOR_TYPE_SAMPLER, 32}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool.maxSets = 4128;
        pool.poolSizeCount = 2;
        pool.pPoolSizes = sizes;
        if (m_impl->platformInitialized &&
            vkCreateDescriptorPool(native.device, &pool, nullptr, &m_impl->descriptorPool) == VK_SUCCESS)
        {
            ImGui_ImplVulkan_InitInfo info{};
            info.ApiVersion = VK_API_VERSION_1_3;
            info.Instance = native.instance;
            info.PhysicalDevice = native.physicalDevice;
            info.Device = native.device;
            info.QueueFamily = native.queueFamily;
            info.Queue = native.queue;
            info.DescriptorPool = m_impl->descriptorPool;
            info.MinImageCount = std::max(2u, native.imageCount);
            info.ImageCount = std::max(2u, native.imageCount);
            info.UseDynamicRendering = true;
            auto& pipeline = info.PipelineInfoMain;
            pipeline.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            pipeline.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            pipeline.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
            pipeline.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_impl->colorFormat;
            m_impl->rendererInitialized = ImGui_ImplVulkan_Init(&info);
        }
    }
#endif
    if (!m_impl->rendererInitialized)
    {
        shutdown();
        return false;
    }
    device.setEventCallback([](const void* event)
                            { ImGui_ImplSDL2_ProcessEvent(static_cast<const SDL_Event*>(event)); });
    return true;
}

void EditorGuiRenderer::shutdown()
{
    if (m_impl->device)
        m_impl->device->setEventCallback({});
#ifdef TINYENGINE_HAS_VULKAN
    if (m_impl->nativeDevice)
    {
        vkDeviceWaitIdle(m_impl->nativeDevice);
        if (m_impl->rendererInitialized)
        {
            if (m_impl->sceneDescriptor)
                ImGui_ImplVulkan_RemoveTexture(m_impl->sceneDescriptor);
            for (const auto& [view, descriptor] : m_impl->uiDescriptors)
                ImGui_ImplVulkan_RemoveTexture(descriptor);
            ImGui_ImplVulkan_Shutdown();
        }
        if (m_impl->descriptorPool)
            vkDestroyDescriptorPool(m_impl->nativeDevice, m_impl->descriptorPool, nullptr);
    }
    else
#endif
        if (m_impl->rendererInitialized)
        ImGui_ImplOpenGL3_Shutdown();
    if (m_impl->platformInitialized)
        ImGui_ImplSDL2_Shutdown();
    *m_impl = {};
}

void EditorGuiRenderer::newFrame()
{
#ifdef TINYENGINE_HAS_VULKAN
    if (m_impl->vulkan)
    {
        const auto native = m_impl->vulkan->getVulkanInterop();
        m_impl->frameCommand =
            native.colorView && native.extent.width && native.extent.height ? native.commandBuffer : VK_NULL_HANDLE;
        // Acquire can be skipped before the context waits for its previous
        // submission. Keep descriptors and pipelines intact on those UI frames.
        if (m_impl->frameCommand)
        {
            // The renderer waits for its preceding submission before this overlay starts.
            // Re-register visible UI views each frame so cache eviction and handle reuse
            // cannot leave descriptors pointing at a previously destroyed image view.
            for (const auto& [view, descriptor] : m_impl->uiDescriptors)
                ImGui_ImplVulkan_RemoveTexture(descriptor);
            m_impl->uiDescriptors.clear();
            if (native.colorFormat != m_impl->colorFormat)
            {
                m_impl->colorFormat = native.colorFormat;
                ImGui_ImplVulkan_PipelineInfo pipeline{};
                pipeline.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
                pipeline.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
                pipeline.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
                pipeline.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_impl->colorFormat;
                ImGui_ImplVulkan_CreateMainPipeline(&pipeline);
            }
        }
        // In pinned ImGui 1.92.8 this only checks backend state. Deferred font
        // texture creation/update happens in RenderDrawData on an acquired frame.
        ImGui_ImplVulkan_NewFrame();
    }
    else
#endif
        ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
}

void EditorGuiRenderer::renderDrawData()
{
#ifdef TINYENGINE_HAS_VULKAN
    if (m_impl->vulkan)
    {
        const auto native = m_impl->vulkan->getVulkanInterop();
        if (!m_impl->frameCommand || native.commandBuffer != m_impl->frameCommand || !native.colorView ||
            !native.extent.width || !native.extent.height)
            return;
        // Dynamic-rendering scopes have no implicit external dependency. Make
        // EndFrame's clear/presentation writes visible to this LOAD and blending.
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(native.commandBuffer, &dependency);
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView = native.colorView;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = native.extent;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &attachment;
        vkCmdBeginRendering(native.commandBuffer, &rendering);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), native.commandBuffer);
        vkCmdEndRendering(native.commandBuffer);
        return;
    }
#endif
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

std::uintptr_t EditorGuiRenderer::sceneTexture()
{
#ifdef TINYENGINE_HAS_VULKAN
    if (m_impl->vulkan)
    {
        if (!m_impl->frameCommand)
            return 0;
        const auto generation = m_impl->vulkan->getSceneGeneration();
        const auto view = m_impl->vulkan->getSceneImageView();
        if (generation != m_impl->sceneGeneration || view != m_impl->sceneView)
        {
            // Scene replacement is deferred until the previous frame is complete.
            if (m_impl->sceneDescriptor)
                ImGui_ImplVulkan_RemoveTexture(m_impl->sceneDescriptor);
            m_impl->sceneDescriptor =
                view ? ImGui_ImplVulkan_AddTexture(view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) : VK_NULL_HANDLE;
            m_impl->sceneGeneration = generation;
            m_impl->sceneView = view;
        }
        return reinterpret_cast<std::uintptr_t>(m_impl->sceneDescriptor);
    }
#endif
    return m_impl->device ? m_impl->device->getSceneColorTexture() : 0;
}

std::uintptr_t EditorGuiRenderer::uiTexture(const std::string& path)
{
#ifdef TINYENGINE_HAS_VULKAN
    if (m_impl->vulkan)
    {
        if (!m_impl->frameCommand)
            return 0;
        const auto view = m_impl->vulkan->getUiImageView(path);
        if (!view)
            return 0;
        auto& descriptor = m_impl->uiDescriptors[view];
        if (!descriptor)
            descriptor = ImGui_ImplVulkan_AddTexture(view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return reinterpret_cast<std::uintptr_t>(descriptor);
    }
#endif
    return m_impl->device ? m_impl->device->getUiTexture(path) : 0;
}
} // namespace tinyengine
