#include "tinyengine/render/sdl_vulkan_render_device.h"
#include "render_shaders.h"
#include "sdl_window.h"
#include "tinyengine/platform/platform.h"
#include "tinyengine/render/material_compiler.h"
#include "tinyengine/render/shader_pipeline.h"
#include "tinyengine/ui/text_layout.h"
#include "tinyengine/vfx/particle_system.h"
#include "vulkan_context.h"
#include "vulkan_shader.h"
#include <SDL.h>
#include <SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace tinyengine
{
namespace
{
using namespace RenderShaders;
using vk::Buffer;
using vk::Image;
using vk::UniformValues;
template <class T> void value(UniformValues& values, const std::string& name, const T& v)
{
    auto& out = values[name];
    out.elementSize = sizeof(T);
    out.count = 1;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&v);
    out.data.assign(bytes, bytes + sizeof(T));
}
template <class T> void arrayValue(UniformValues& values, const std::string& name, const T* v, std::size_t count)
{
    auto& out = values[name];
    out.elementSize = sizeof(T);
    out.count = static_cast<std::uint32_t>(count);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(v);
    out.data.assign(bytes, bytes + sizeof(T) * count);
}
void checked(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
Mat4 inverseProjection(const Mat4& p)
{
    Mat4 r{};
    r.at(0, 0) = 1 / p.at(0, 0);
    r.at(1, 1) = 1 / p.at(1, 1);
    r.at(0, 3) = p.at(0, 2) / p.at(0, 0);
    r.at(1, 3) = p.at(1, 2) / p.at(1, 1);
    r.at(2, 3) = -1;
    r.at(3, 2) = 1 / p.at(2, 3);
    r.at(3, 3) = p.at(2, 2) / p.at(2, 3);
    return r;
}
Mat4 inverseViewProjection(const FrameView& v)
{
    Mat4 r = Mat4::identity();
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            r.at(row, col) = v.view.at(col, row);
    r.at(0, 3) = v.cameraPosition.x;
    r.at(1, 3) = v.cameraPosition.y;
    r.at(2, 3) = v.cameraPosition.z;
    return r * inverseProjection(v.projection);
}
float halfFloat(std::uint16_t h)
{
    const std::uint32_t sign = std::uint32_t(h & 0x8000) << 16;
    std::uint32_t exponent = (h >> 10) & 31, mantissa = h & 1023, bits;
    if (exponent == 0)
    {
        if (!mantissa)
            bits = sign;
        else
        {
            int shift = 0;
            while (!(mantissa & 1024))
            {
                mantissa <<= 1;
                ++shift;
            }
            bits = sign | std::uint32_t(127 - 14 - shift) << 23 | (mantissa & 1023) << 13;
        }
    }
    else
        bits = sign | ((exponent == 31 ? 255 : exponent + 112) << 23) | (mantissa << 13);
    float result;
    std::memcpy(&result, &bits, 4);
    return result;
}
struct BoundResource
{
    std::vector<Image*> images;
    VkBufferView texel{};
    VkDescriptorBufferInfo buffer{};
};
using Resources = std::unordered_map<std::string, BoundResource>;
struct NativeProgram
{
    VkDevice device{};
    std::uint64_t lastUsedFrame = 0;
    vk::Program reflected;
    VkShaderModule vertex{}, fragment{}, compute{};
    VkDescriptorSetLayout set{};
    VkPipelineLayout layout{};
    std::unordered_map<std::string, VkPipeline> pipelines;
    ~NativeProgram()
    {
        if (!device)
            return;
        for (const auto& [key, pipeline] : pipelines)
            vkDestroyPipeline(device, pipeline, nullptr);
        if (layout)
            vkDestroyPipelineLayout(device, layout, nullptr);
        if (set)
            vkDestroyDescriptorSetLayout(device, set, nullptr);
        for (auto module : {vertex, fragment, compute})
            if (module)
                vkDestroyShaderModule(device, module, nullptr);
    }
};
enum class VertexKind
{
    None,
    Mesh,
    Particle,
    Ui
};
struct MeshGpu
{
    Buffer vertices, indices, lines;
    std::uint32_t count = 0, lineCount = 0;
    std::weak_ptr<MeshResource> owner;
};
struct TextureGpu
{
    Image image;
    std::weak_ptr<TextureResource> owner;
};
const char* kWindowFragment = R"GLSL(#version 430 core
in vec2 vUv;layout(location=0) out vec4 outColor;
uniform sampler2D uSceneColor;uniform int uSwapchainSrgb;
void main(){vec3 c=texture(uSceneColor,vec2(vUv.x,1.0-vUv.y)).rgb;
if(uSwapchainSrgb!=0)c=mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));outColor=vec4(c,1);})GLSL";
} // namespace

struct SdlVulkanRenderDevice::Impl
{
    SdlWindow window;
    vk::Context context;
    int requestedWidth = 1280, requestedHeight = 720, width = 0, height = 0;
    std::string title, error;
    bool initialized = false, recording = false, presentScene = true, deferred = false, history = false,
         initializeImages = true;
    bool fxaa = true, bloom = true, gtao = true, taa = true, retro = false, trimPending = false,
         programTrimPending = false;
    float exposure = 1, blockSize = 3, colorLevels = 32, gpuMs = 0;
    std::size_t budget = 512u * 1024 * 1024, cascade = 0;
    std::uint64_t generation = 0;
    std::uint64_t frameSerial = 0;
    int temporal = 0;
    FrameView frame;
    MaterialTemplateSources templates;
    Image hdr, ambient, depth, velocity, color, retroColor, gtaoImage, gtaoRaw;
    std::array<Image, 4> gbuffer;
    std::array<Image, 3> shadows;
    std::array<Image, 2> temporalColor, temporalDepth;
    std::array<Image, 5> bloomImages;
    std::vector<Image> hzb;
    Image white, normal;
    VkSampler sampler{}, nearestSampler{}, repeatSampler{};
    std::unordered_set<VkImageView> repeatViews;
    VkQueryPool timestamps{};
    float timestampPeriod = 0;
    bool timestampPending = false;
    std::unordered_map<std::string, std::unique_ptr<NativeProgram>> programs;
    std::unordered_map<std::string, MeshGpu> meshes;
    std::unordered_map<std::string, TextureGpu> textures;
    std::unordered_map<std::string, std::shared_ptr<TextureResource>> uiResources;
    GlyphAtlas glyphAtlas;
    struct FontPage
    {
        Image image;
        std::uint64_t revision = 0;
    };
    std::vector<FontPage> fontPages;
    std::uint64_t fontGeneration = 0;
    std::vector<VkBufferView> frameViews;
    std::vector<Image*> targets;
    Image* depthTarget = nullptr;
    bool drawDepth = false, writeDepth = false, blend = false, cull = false, frontCull = false;
    VkBufferView lights{}, clusters{};
    std::unordered_map<std::string, std::filesystem::file_time_type> watched;
    std::unordered_map<std::string, std::weak_ptr<const CompiledMaterial>> activeMaterials;
    std::shared_ptr<const MaterialRenderProxy> errorMaterial;
    std::unordered_set<std::string> loggedMaterialErrors;
    std::chrono::steady_clock::time_point nextShaderCheck{};

    Image makeImage(int w, int h, VkFormat format, bool isDepth = false)
    {
        auto image = context.createImage(
            w, h, format,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                (isDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
            isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
        if (!image.handle)
            throw std::runtime_error(context.error);
        return image;
    }
    vk::Slice upload(const void* data, VkDeviceSize size, VkDeviceSize alignment = 256)
    {
        const auto slice = context.upload(data, size, alignment);
        if (!slice.buffer)
            throw std::runtime_error(context.error);
        return slice;
    }
    void destroyTargets()
    {
        for (Image* p : {&hdr, &ambient, &depth, &velocity, &color, &retroColor, &gtaoImage, &gtaoRaw})
            context.destroy(*p);
        for (auto* collection : {&gbuffer})
            for (auto& image : *collection)
                context.destroy(image);
        for (auto& image : shadows)
            context.destroy(image);
        for (auto& image : temporalColor)
            context.destroy(image);
        for (auto& image : temporalDepth)
            context.destroy(image);
        for (auto& image : bloomImages)
            context.destroy(image);
        for (auto& image : hzb)
            context.destroy(image);
        hzb.clear();
        targets.clear();
        depthTarget = nullptr;
    }
    void createTargets()
    {
        context.waitIdle();
        destroyTargets();
        width = std::max(1, requestedWidth);
        height = std::max(1, requestedHeight);
        hdr = makeImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT);
        ambient = makeImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT);
        depth = makeImage(width, height, VK_FORMAT_D32_SFLOAT, true);
        velocity = makeImage(width, height, VK_FORMAT_R16G16_SFLOAT);
        color = makeImage(width, height, VK_FORMAT_R8G8B8A8_UNORM);
        retroColor = makeImage(width, height, VK_FORMAT_R8G8B8A8_UNORM);
        gtaoImage = makeImage(std::max(1, width / 2), std::max(1, height / 2), VK_FORMAT_R16G16B16A16_SFLOAT);
        gtaoRaw = makeImage(std::max(1, width / 2), std::max(1, height / 2), VK_FORMAT_R16G16B16A16_SFLOAT);
        for (std::size_t i = 0; i < gbuffer.size(); ++i)
            gbuffer[i] = makeImage(width, height, i == 3 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT);
        for (auto& image : shadows)
            image =
                makeImage(FrameView::kShadowMapResolution, FrameView::kShadowMapResolution, VK_FORMAT_D32_SFLOAT, true);
        for (auto& image : temporalColor)
            image = makeImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT);
        for (auto& image : temporalDepth)
            image = makeImage(width, height, VK_FORMAT_R32_SFLOAT);
        int w = width, h = height;
        for (auto& image : bloomImages)
        {
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
            image = makeImage(w, h, VK_FORMAT_R16G16B16A16_SFLOAT);
        }
        w = std::max(1, (width + 1) / 2);
        h = std::max(1, (height + 1) / 2);
        for (;;)
        {
            hzb.push_back(makeImage(w, h, VK_FORMAT_R32_SFLOAT));
            if (w == 1 && h == 1)
                break;
            w = std::max(1, (w + 1) / 2);
            h = std::max(1, (h + 1) / 2);
        }
        history = false;
        initializeImages = true;
        temporal = 0;
        ++generation;
    }
    void clearImage(Image& image, float number = 0)
    {
        context.transition(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageSubresourceRange range{image.aspect, 0, 1, 0, 1};
        if (image.aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
        {
            VkClearDepthStencilValue clear{1, 0};
            vkCmdClearDepthStencilImage(context.command(), image.handle, image.layout, &clear, 1, &range);
        }
        else
        {
            VkClearColorValue clear{};
            clear.float32[0] = clear.float32[1] = clear.float32[2] = clear.float32[3] = number;
            vkCmdClearColorImage(context.command(), image.handle, image.layout, &clear, 1, &range);
        }
        sample(image);
    }
    void sample(Image& image)
    {
        context.transition(image,
                           image.aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    bool ensure()
    {
        if (recording)
            return true;
        if (presentScene)
        {
            int w = 0, h = 0;
            SDL_Vulkan_GetDrawableSize(window.get(), &w, &h);
            if (w > 0 && h > 0)
            {
                requestedWidth = w;
                requestedHeight = h;
            }
        }
        if (width != requestedWidth || height != requestedHeight)
            createTargets();
        if (!context.beginFrame())
            return false;
        recording = true;
        ++frameSerial;
        // The context serializes shared offscreen resources; previously used views can now retire.
        for (auto view : frameViews)
            vkDestroyBufferView(context.device, view, nullptr);
        frameViews.clear();
        if (timestamps)
        {
            if (timestampPending)
            {
                std::uint64_t ticks[2]{};
                if (vkGetQueryPoolResults(context.device, timestamps, 0, 2, sizeof(ticks), ticks, sizeof(std::uint64_t),
                                          VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
                    gpuMs = float(ticks[1] - ticks[0]) * timestampPeriod / 1000000.f;
            }
            vkCmdResetQueryPool(context.command(), timestamps, 0, 2);
            vkCmdWriteTimestamp2(context.command(), VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, timestamps, 0);
            timestampPending = false;
        }
        trimCaches();
        if (programTrimPending)
        {
            destroyPrograms();
            programTrimPending = false;
        }
        for (auto it = programs.begin(); it != programs.end();)
            if (frameSerial - it->second->lastUsedFrame > 120)
                it = programs.erase(it);
            else
                ++it;
        for (auto it = activeMaterials.begin(); it != activeMaterials.end();)
            if (it->second.expired())
                it = activeMaterials.erase(it);
            else
                ++it;
        glyphAtlas.beginFrame();
        if (std::chrono::steady_clock::now() >= nextShaderCheck)
        {
            nextShaderCheck = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            bool changed = false;
            for (auto& [path, time] : watched)
            {
                std::error_code ec;
                const auto stamp = std::filesystem::last_write_time(path, ec);
                if (stamp != time)
                {
                    time = stamp;
                    changed = true;
                }
            }
            if (changed)
                reloadTemplates();
        }
        if (initializeImages)
        {
            for (auto& image : shadows)
                clearImage(image, 1);
            for (auto& image : temporalColor)
                clearImage(image);
            for (auto& image : temporalDepth)
                clearImage(image, 1);
            for (auto& image : hzb)
                clearImage(image, 1);
            initializeImages = false;
        }
        return true;
    }
    void beginRendering(bool clear = false)
    {
        std::vector<VkRenderingAttachmentInfo> attachments;
        std::uint32_t w = depthTarget ? depthTarget->width : 0, h = depthTarget ? depthTarget->height : 0;
        for (auto* image : targets)
        {
            VkRenderingAttachmentInfo a{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            a.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            if (image)
            {
                context.transition(*image, a.imageLayout, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
                a.imageView = image->view;
                w = image->width;
                h = image->height;
            }
            attachments.push_back(a);
        }
        VkRenderingAttachmentInfo d{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        if (depthTarget)
        {
            d.imageView = depthTarget->view;
            d.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            context.transition(
                *depthTarget, d.imageLayout,
                VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            d.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            d.clearValue.depthStencil = {1, 0};
        }
        VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
        info.renderArea.extent = {w, h};
        info.layerCount = 1;
        info.colorAttachmentCount = static_cast<std::uint32_t>(attachments.size());
        info.pColorAttachments = attachments.data();
        info.pDepthAttachment = depthTarget ? &d : nullptr;
        vkCmdBeginRendering(context.command(), &info);
        VkViewport viewport{0, 0, float(w), float(h), 0, 1};
        VkRect2D scissor{{0, 0}, {w, h}};
        vkCmdSetViewport(context.command(), 0, 1, &viewport);
        vkCmdSetScissor(context.command(), 0, 1, &scissor);
    }
    void select(std::vector<Image*> images, Image* z = nullptr, bool clear = false)
    {
        targets = std::move(images);
        depthTarget = z;
        if (clear)
        {
            beginRendering(true);
            vkCmdEndRendering(context.command());
        }
    }
    NativeProgram& program(const std::string& vertex, const std::string& fragment)
    {
        const std::string key = vertex + '\n' + fragment;
        if (auto it = programs.find(key); it != programs.end())
        {
            it->second->lastUsedFrame = frameSerial;
            return *it->second;
        }
        auto p = std::make_unique<NativeProgram>();
        p->device = context.device;
        p->lastUsedFrame = frameSerial;
        if (!vk::compileGraphics(vertex, fragment, p->reflected, error))
            throw std::runtime_error(error);
        const auto module = [&](const std::vector<std::uint32_t>& words)
        {
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = words.size() * 4;
            info.pCode = words.data();
            VkShaderModule result{};
            checked(vkCreateShaderModule(context.device, &info, nullptr, &result), "create shader module");
            return result;
        };
        p->vertex = module(p->reflected.vertex);
        p->fragment = module(p->reflected.fragment);
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (const auto& d : p->reflected.descriptors)
        {
            if (d.set != 0)
                throw std::runtime_error("unsupported descriptor set from shader compiler");
            bindings.push_back({d.binding, d.type, d.count, d.stages, nullptr});
        }
        VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        setInfo.pBindings = bindings.data();
        checked(vkCreateDescriptorSetLayout(context.device, &setInfo, nullptr, &p->set), "create descriptor layout");
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &p->set;
        checked(vkCreatePipelineLayout(context.device, &layoutInfo, nullptr, &p->layout), "create pipeline layout");
        auto* result = p.get();
        programs.emplace(key, std::move(p));
        return *result;
    }
    NativeProgram& computeProgram(const std::string& source)
    {
        if (auto it = programs.find(source); it != programs.end())
        {
            it->second->lastUsedFrame = frameSerial;
            return *it->second;
        }
        auto p = std::make_unique<NativeProgram>();
        p->device = context.device;
        p->lastUsedFrame = frameSerial;
        if (!vk::compileCompute(source, p->reflected, error))
            throw std::runtime_error(error);
        VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module.codeSize = p->reflected.compute.size() * 4;
        module.pCode = p->reflected.compute.data();
        checked(vkCreateShaderModule(context.device, &module, nullptr, &p->compute), "create compute module");
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (const auto& d : p->reflected.descriptors)
            bindings.push_back({d.binding, d.type, d.count, d.stages, nullptr});
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set.bindingCount = static_cast<std::uint32_t>(bindings.size());
        set.pBindings = bindings.data();
        checked(vkCreateDescriptorSetLayout(context.device, &set, nullptr, &p->set), "create compute descriptors");
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &p->set;
        checked(vkCreatePipelineLayout(context.device, &layout, nullptr, &p->layout), "create compute layout");
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = p->compute;
        info.stage.pName = "main";
        info.layout = p->layout;
        VkPipeline pipeline{};
        checked(vkCreateComputePipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                "create culling pipeline");
        p->pipelines.emplace("compute", pipeline);
        auto* result = p.get();
        programs.emplace(source, std::move(p));
        return *result;
    }
    vk::Slice cullInstances(const std::vector<RenderItem>& items, std::uint32_t indexCount, vk::Slice& indirect)
    {
        // Keep the shared occlusion algorithm; Vulkan consumes interleaved current/previous matrices.
        static const std::string source = []
        {
            std::string s = kGpuCullComputeShaderSource;
            const auto replace = [&](const std::string& from, const std::string& to)
            { s.replace(s.find(from), from.size(), to); };
            replace("layout(std430, binding = 1) writeonly buffer VisibleModels { mat4 visibleModels[]; };",
                    "struct VisibleInstance {mat4 model;mat4 previousModel;}; layout(std430,binding=1) writeonly "
                    "buffer VisibleModels {VisibleInstance visibleInstances[];};");
            replace(
                "layout(std430, binding = 2) writeonly buffer VisiblePreviousModels { mat4 visiblePreviousModels[]; };",
                "");
            replace("visibleModels[visibleIndex] = instance.model;",
                    "visibleInstances[visibleIndex].model=instance.model;");
            replace("visiblePreviousModels[visibleIndex] = instance.previousModel;",
                    "visibleInstances[visibleIndex].previousModel=instance.previousModel;");
            return s;
        }();
        auto& p = computeProgram(source);
        struct Input
        {
            Mat4 model, previous;
            Vec4 minimum, maximum;
        };
        std::vector<Input> input;
        for (const auto& item : items)
            input.push_back({item.model,
                             item.previousModel,
                             {item.worldBounds.minimum.x, item.worldBounds.minimum.y, item.worldBounds.minimum.z, 0},
                             {item.worldBounds.maximum.x, item.worldBounds.maximum.y, item.worldBounds.maximum.z, 0}});
        const auto inputs = upload(input.data(), input.size() * sizeof(Input), 16);
        std::vector<Mat4> empty(items.size() * 2);
        const auto output = upload(empty.data(), empty.size() * sizeof(Mat4), 16);
        const VkDrawIndexedIndirectCommand command{indexCount, 0, 0, 0, 0};
        indirect = upload(&command, sizeof(command), 16);
        auto u = frameUniforms();
        value(u, "uInputCount", static_cast<std::uint32_t>(items.size()));
        value(u, "uHzbValid", int(frame.occlusionHistoryValid && !frame.resetTemporalHistory));
        value(u, "uHzbCount", int(std::min<std::size_t>(12, hzb.size())));
        Resources resources;
        for (int i = 0; i < 12; ++i)
            resources["uHzb"].images.push_back(&hzb[std::min<std::size_t>(i, hzb.size() - 1)]);
        for (const auto& d : p.reflected.descriptors)
            if (d.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            {
                const auto& slice = d.binding == 0 ? inputs : d.binding == 1 ? output : indirect;
                resources[d.name].buffer = {slice.buffer, slice.offset, slice.size};
            }
        const auto set = descriptors(p, u, std::move(resources));
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(context.command(), &dependency);
        vkCmdBindPipeline(context.command(), VK_PIPELINE_BIND_POINT_COMPUTE, p.pipelines.at("compute"));
        vkCmdBindDescriptorSets(context.command(), VK_PIPELINE_BIND_POINT_COMPUTE, p.layout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(context.command(), static_cast<std::uint32_t>((items.size() + 63) / 64), 1, 1);
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        vkCmdPipelineBarrier2(context.command(), &dependency);
        return output;
    }
    void watchTemplates()
    {
        for (const auto& [name, source] : templates.files)
        {
            const auto path = "assets/shaders/material/" + name;
            std::error_code ec;
            watched[path] = std::filesystem::last_write_time(path, ec);
        }
    }
    bool reloadTemplates()
    {
        const auto candidate = ShaderPipeline::loadMaterialTemplates();
        std::vector<std::shared_ptr<const CompiledMaterial>> materials{
            MaterialCompiler{}.compile(makeBuiltinPbrDefinition()).material};
        for (auto it = activeMaterials.begin(); it != activeMaterials.end();)
            if (auto m = it->second.lock())
            {
                materials.push_back(m);
                ++it;
            }
            else
                it = activeMaterials.erase(it);
        for (const auto& material : materials)
            for (auto pass : {MaterialPass::Base, MaterialPass::Shadow, MaterialPass::GBuffer})
                for (auto geometry : {MaterialGeometry::Static, MaterialGeometry::Skinned, MaterialGeometry::Instanced})
                {
                    auto source = MaterialCompiler{}.generatePass(*material, pass, geometry, candidate);
                    vk::Program compiled;
                    if (!source.error.empty())
                    {
                        error = source.error;
                        return false;
                    }
                    if (!vk::compileGraphics(source.vertex, source.fragment, compiled, error))
                        return false;
                }
        auto fragment = candidate.files.at("DeferredLighting.frag");
        const auto include = fragment.find("#include \"Lighting.glsl\"");
        if (include != std::string::npos)
            fragment.replace(include, 24, candidate.files.at("Lighting.glsl"));
        vk::Program deferredProgram;
        if (!vk::compileGraphics(kToneVertexShaderSource, fragment, deferredProgram, error))
            return false;
        templates = candidate;
        watchTemplates();
        history = false;
        programTrimPending = true;
        error.clear();
        return true;
    }
    std::shared_ptr<const MaterialRenderProxy> fallbackMaterial()
    {
        if (errorMaterial)
            return errorMaterial;
        auto definition = makeBuiltinPbrDefinition();
        definition.renderState.shadingModel = MaterialShadingModel::Unlit;
        definition.renderState.twoSided = true;
        auto proxy = std::make_shared<MaterialRenderProxy>();
        proxy->compiled = MaterialCompiler{}.compile(definition).material;
        proxy->state = definition.renderState;
        for (const auto& slot : proxy->compiled->layout.numericSlots)
        {
            const auto p = std::find_if(definition.parameters.begin(), definition.parameters.end(),
                                        [&](const auto& p) { return p.name == slot.name; });
            proxy->numericSlots.push_back(slot.name == "BaseColor" ? Vec4{1, 0, 1, 1}
                                                                   : materialNumericSlot(p->defaultValue));
        }
        errorMaterial = proxy;
        return errorMaterial;
    }
    VkPipeline pipeline(NativeProgram& p, VertexKind kind, bool wire = false,
                        VkFormat windowFormat = VK_FORMAT_UNDEFINED)
    {
        std::vector<VkFormat> formats;
        if (windowFormat != VK_FORMAT_UNDEFINED)
            formats.push_back(windowFormat);
        else
            for (auto* image : targets)
                formats.push_back(image ? image->format : VK_FORMAT_UNDEFINED);
        std::string key = std::to_string(int(kind)) + ':' + std::to_string(drawDepth) + std::to_string(writeDepth) +
                          std::to_string(blend) + std::to_string(cull) + std::to_string(frontCull) +
                          std::to_string(wire);
        for (auto format : formats)
            key += ':' + std::to_string(format);
        key += ':' + std::to_string(depthTarget ? depthTarget->format : VK_FORMAT_UNDEFINED);
        if (auto found = p.pipelines.find(key); found != p.pipelines.end())
            return found->second;
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (int i = 0; i < 2; ++i)
        {
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = i ? p.fragment : p.vertex;
            stages[i].pName = "main";
        }
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
        if (kind == VertexKind::Mesh)
        {
            bindings = {{0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX},
                        {1, sizeof(Mat4) * 2, VK_VERTEX_INPUT_RATE_INSTANCE}};
            attributes = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, position)},
                          {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)},
                          {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshVertex, uv)},
                          {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, tangent)},
                          {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, boneIndices)},
                          {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, boneWeights)}};
            for (std::uint32_t i = 0; i < 8; ++i)
                attributes.push_back({8 + i, 1, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16});
        }
        else if (kind == VertexKind::Particle)
        {
            bindings = {{0, 48, VK_VERTEX_INPUT_RATE_INSTANCE}};
            for (std::uint32_t i = 0; i < 3; ++i)
                attributes.push_back({i, 0, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16});
        }
        else if (kind == VertexKind::Ui)
        {
            bindings = {{0, 16, VK_VERTEX_INPUT_RATE_VERTEX}};
            attributes = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32_SFLOAT, 8}};
        }
        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
        vertexInput.pVertexBindingDescriptions = bindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = wire ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.cullMode = cull ? (frontCull ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT) : VK_CULL_MODE_NONE;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = drawDepth;
        ds.depthWriteEnable = writeDepth;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        std::vector<VkPipelineColorBlendAttachmentState> colors(formats.size());
        for (auto& a : colors)
        {
            a.colorWriteMask = 15;
            a.blendEnable = blend;
            a.srcColorBlendFactor = a.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            a.dstColorBlendFactor = a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            a.colorBlendOp = a.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = static_cast<std::uint32_t>(colors.size());
        cb.pAttachments = colors.data();
        const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2;
        dy.pDynamicStates = dynamics;
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = static_cast<std::uint32_t>(formats.size());
        rendering.pColorAttachmentFormats = formats.data();
        rendering.depthAttachmentFormat = depthTarget ? depthTarget->format : VK_FORMAT_UNDEFINED;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &vp;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &ms;
        info.pDepthStencilState = &ds;
        info.pColorBlendState = &cb;
        info.pDynamicState = &dy;
        info.layout = p.layout;
        VkPipeline result{};
        checked(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &result),
                "create graphics pipeline");
        p.pipelines.emplace(key, result);
        return result;
    }
    VkDescriptorSet descriptors(NativeProgram& p, const UniformValues& values, Resources resources)
    {
        const auto set = context.allocateDescriptor(p.set);
        if (!set)
            throw std::runtime_error(context.error);
        // Update each binding separately so pointers remain valid throughout vkUpdateDescriptorSets.
        for (const auto& d : p.reflected.descriptors)
        {
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = set;
            write.dstBinding = d.binding;
            write.descriptorCount = d.count;
            write.descriptorType = d.type;
            VkDescriptorBufferInfo buffer{};
            std::vector<VkDescriptorImageInfo> images;
            const auto found = resources.find(d.name);
            const BoundResource* bound = found == resources.end() ? nullptr : &found->second;
            if (d.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
            {
                const auto bytes = vk::packUniforms(d, values);
                const auto slice = upload(bytes.data(), bytes.size(), 256);
                buffer = {slice.buffer, slice.offset, slice.size};
                write.pBufferInfo = &buffer;
            }
            else if (d.type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)
            {
                if (!bound || !bound->texel)
                    throw std::runtime_error("Missing texel buffer: " + d.name);
                write.pTexelBufferView = &bound->texel;
            }
            else if (d.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            {
                if (!bound)
                    throw std::runtime_error("Missing storage buffer: " + d.name);
                buffer = bound->buffer;
                write.pBufferInfo = &buffer;
            }
            else
            {
                for (std::uint32_t i = 0; i < d.count; ++i)
                {
                    Image* image = bound && i < bound->images.size() ? bound->images[i] : &white;
                    sample(*image);
                    const auto filter =
                        (image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) || d.name == "uHzb" || d.name == "uHistoryDepth"
                            ? nearestSampler
                        : repeatViews.contains(image->view) ? repeatSampler
                                                            : sampler;
                    images.push_back({filter, image->view, image->layout});
                }
                write.pImageInfo = images.data();
            }
            vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
        }
        return set;
    }
    VkBufferView texel(const void* bytes, std::size_t size, VkFormat format)
    {
        const auto slice = upload(bytes, size, 256);
        VkBufferViewCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
        info.buffer = slice.buffer;
        info.format = format;
        info.offset = slice.offset;
        info.range = slice.size;
        VkBufferView result{};
        checked(vkCreateBufferView(context.device, &info, nullptr, &result), "create texel buffer view");
        frameViews.push_back(result);
        return result;
    }
    Image* texture(const std::shared_ptr<TextureResource>& resource, bool srgb, std::uint64_t version = 0)
    {
        if (!resource || !resource->isReady())
            return &white;
        const std::string key = resource->GetPath() + ':' +
                                std::to_string(reinterpret_cast<std::uintptr_t>(resource.get())) + ':' +
                                std::to_string(srgb) + ':' + std::to_string(version);
        if (auto found = textures.find(key); found != textures.end())
            return &found->second.image;
        auto format = resource->isHdr() ? VK_FORMAT_R32G32B32A32_SFLOAT
                      : srgb            ? VK_FORMAT_R8G8B8A8_SRGB
                                        : VK_FORMAT_R8G8B8A8_UNORM;
        using Compression = TextureResource::Compression;
        switch (resource->getCompression())
        {
        case Compression::BC1:
            format = srgb ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            break;
        case Compression::BC2:
            format = srgb ? VK_FORMAT_BC2_SRGB_BLOCK : VK_FORMAT_BC2_UNORM_BLOCK;
            break;
        case Compression::BC3:
            format = srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
            break;
        case Compression::BC4:
            format = VK_FORMAT_BC4_UNORM_BLOCK;
            break;
        case Compression::BC5:
            format = VK_FORMAT_BC5_UNORM_BLOCK;
            break;
        case Compression::BC6H:
            format = VK_FORMAT_BC6H_UFLOAT_BLOCK;
            break;
        case Compression::BC7:
            format = srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
            break;
        default:
            break;
        }
        VkFormatProperties support{};
        vkGetPhysicalDeviceFormatProperties(context.physicalDevice, format, &support);
        if (!(support.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            throw std::runtime_error("Vulkan texture format unsupported: " + resource->GetPath());
        const auto& mips = resource->GetMipLevels();
        const bool generate = resource->getCompression() == Compression::None && mips.empty();
        const auto levels = generate
                                ? std::bit_width(std::uint32_t(std::max(resource->getWidth(), resource->getHeight())))
                                : std::uint32_t(std::max<std::size_t>(1, mips.size()));
        auto image = context.createImage(resource->getWidth(), resource->getHeight(), format,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                         VK_IMAGE_ASPECT_COLOR_BIT, levels);
        if (!image.handle)
            throw std::runtime_error(context.error);
        const void* data =
            resource->isHdr() ? static_cast<const void*>(resource->GetFloatData().data()) : resource->GetData().data();
        const auto size =
            resource->isHdr() ? resource->GetFloatData().size() * sizeof(float) : resource->GetData().size();
        bool uploaded = true;
        if (mips.empty())
            uploaded = context.uploadImage(image, data, size);
        else
            for (std::uint32_t level = 0; level < mips.size() && uploaded; ++level)
            {
                const auto& mip = mips[level];
                if (mip.offset > size || mip.size > size - mip.offset)
                {
                    context.destroy(image);
                    throw std::runtime_error("Texture mip payload exceeds asset data: " + resource->GetPath());
                }
                uploaded =
                    context.uploadImage(image, static_cast<const std::uint8_t*>(data) + mip.offset, mip.size, level);
            }
        if (uploaded && generate && levels > 1)
            uploaded = context.generateMipmaps(image);
        if (!uploaded)
        {
            context.destroy(image);
            throw std::runtime_error(context.error);
        }
        sample(image);
        repeatViews.insert(image.view);
        auto found = textures.emplace(key, TextureGpu{image, resource}).first;
        return &found->second.image;
    }
    Image* uiTexture(const std::string& path)
    {
        if (path.empty())
            return &white;
        auto it = uiResources.find(path);
        if (it == uiResources.end())
        {
            auto resource = std::make_shared<TextureResource>();
            if (!resource->load(path))
                return &white;
            it = uiResources.emplace(path, std::move(resource)).first;
        }
        return texture(it->second, true);
    }
    UniformValues frameUniforms()
    {
        UniformValues u;
        value(u, "uView", frame.view);
        value(u, "uProjection", frame.projection);
        value(u, "uViewProjection", frame.projection * frame.view);
        value(u, "uPreviousViewProjection", frame.previousViewProjection);
        value(u, "uCameraPosition", frame.cameraPosition);
        value(u, "uLightCount", frame.lightCount);
        arrayValue(u, "uShadowViewProjections", frame.shadowViewProjections.data(), 3);
        arrayValue(u, "uShadowCascadeSplits", frame.shadowCascadeSplits.data(), 3);
        value(u, "uLightViewProjection", frame.shadowViewProjections[cascade]);
        const std::array<int, 3> dims = {int(frame.clusteredLights.tilesX), int(frame.clusteredLights.tilesY),
                                         int(frame.clusteredLights.slicesZ)};
        value(u, "uClusterDimensions", dims);
        value(u, "uClusterDepthRange", Vec2{frame.clusteredLights.nearPlane, frame.clusteredLights.farPlane});
        value(u, "uViewportSize", Vec2{float(width), float(height)});
        value(u, "uViewportRenderMode", int(frame.renderMode));
        return u;
    }
    Resources frameResources(UniformValues& u)
    {
        Resources r;
        r["uLightData"].texel = lights;
        r["uClusterData"].texel = clusters;
        for (auto& image : shadows)
            r["uShadowMaps"].images.push_back(&image);
        const auto irradiance = frame.environmentIrradiance ? frame.environmentIrradiance : frame.environmentTexture;
        const auto prefilter = frame.environmentPrefilter ? frame.environmentPrefilter : frame.environmentTexture;
        r["uEnvironmentIrradiance"].images = {texture(irradiance, false)};
        r["uEnvironmentPrefilter"].images = {texture(prefilter, false)};
        r["uEnvironmentBrdfLut"].images = {texture(frame.environmentBrdfLut, false)};
        value(u, "uHasEnvironment",
              int(irradiance && irradiance->isReady() && prefilter && prefilter->isReady() &&
                  frame.environmentBrdfLut && frame.environmentBrdfLut->isReady()));
        return r;
    }
    void fullscreen(const char* fragment, UniformValues u, Resources r, const std::vector<Image*>& output)
    {
        select(output);
        drawDepth = writeDepth = blend = cull = frontCull = false;
        auto& p = program(kToneVertexShaderSource, fragment);
        const auto set = descriptors(p, u, std::move(r));
        const auto state = pipeline(p, VertexKind::None);
        beginRendering();
        vkCmdBindPipeline(context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, state);
        vkCmdBindDescriptorSets(context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &set, 0, nullptr);
        vkCmdDraw(context.command(), 3, 1, 0, 0);
        vkCmdEndRendering(context.command());
    }
    MeshGpu& mesh(const std::shared_ptr<MeshResource>& resource)
    {
        const auto key = resource->GetPath() + ':' + std::to_string(reinterpret_cast<std::uintptr_t>(resource.get()));
        if (auto found = meshes.find(key); found != meshes.end())
            return found->second;
        MeshGpu result;
        const auto& v = resource->GetVertices();
        const auto& i = resource->GetIndices();
        result.vertices =
            context.createBuffer(v.size() * sizeof(MeshVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        result.indices =
            context.createBuffer(i.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!result.vertices.handle || !result.indices.handle)
        {
            context.destroy(result.vertices);
            context.destroy(result.indices);
            throw std::runtime_error(context.error);
        }
        std::memcpy(result.vertices.mapped, v.data(), v.size() * sizeof(MeshVertex));
        std::memcpy(result.indices.mapped, i.data(), i.size() * 4);
        result.count = static_cast<std::uint32_t>(i.size());
        std::vector<std::uint32_t> line;
        for (std::size_t n = 0; n + 2 < i.size(); n += 3)
            line.insert(line.end(), {i[n], i[n + 1], i[n + 1], i[n + 2], i[n + 2], i[n]});
        result.lines = context.createBuffer(line.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!result.lines.handle)
        {
            context.destroy(result.vertices);
            context.destroy(result.indices);
            context.destroy(result.lines);
            throw std::runtime_error(context.error);
        }
        std::memcpy(result.lines.mapped, line.data(), line.size() * 4);
        result.lineCount = static_cast<std::uint32_t>(line.size());
        result.owner = resource;
        return meshes.emplace(key, result).first->second;
    }
    std::size_t residentBytes() const
    {
        std::size_t bytes = 0;
        for (const auto& [key, m] : meshes)
            bytes += m.vertices.size + m.indices.size + m.lines.size;
        for (const auto& [key, t] : textures)
            if (auto resource = t.owner.lock())
                bytes += resource->getByteSize();
            else
                bytes += std::size_t(t.image.width) * t.image.height * 4;
        return bytes;
    }
    void trimCaches()
    {
        // beginFrame has waited for the previous submission; no current draw refers to these caches yet.
        const bool overBudget = residentBytes() > budget;
        for (auto it = meshes.begin(); it != meshes.end();)
            if (trimPending || it->second.owner.expired() || overBudget)
            {
                context.destroy(it->second.vertices);
                context.destroy(it->second.indices);
                context.destroy(it->second.lines);
                it = meshes.erase(it);
            }
            else
                ++it;
        for (auto it = textures.begin(); it != textures.end();)
            if (trimPending || it->second.owner.expired() || overBudget)
            {
                repeatViews.erase(it->second.image.view);
                context.destroy(it->second.image);
                it = textures.erase(it);
            }
            else
                ++it;
        if (trimPending || overBudget)
            ++generation;
        trimPending = false;
    }
    void drawItems(const std::vector<RenderItem>& items, MaterialPass pass)
    {
        if (items.empty() || !ensure() || !items.front().mesh || !items.front().mesh->isReady() ||
            items.front().mesh->GetIndices().empty())
            return;
        const auto& item = items.front();
        auto proxy = item.materialProxy;
        if (!proxy || !proxy->compiled || !proxy->legacyShaderPath.empty())
        {
            error = "Vulkan material has no portable compiled surface; using error material";
            if (loggedMaterialErrors.insert(error).second)
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%s", error.c_str());
            auto fallback = items;
            for (auto& replacement : fallback)
                replacement.materialProxy = fallbackMaterial();
            drawItems(fallback, pass);
            return;
        }
        activeMaterials[proxy->compiled->canonicalProgramContent] = proxy->compiled;
        const auto geometry = items.size() > 1            ? MaterialGeometry::Instanced
                              : item.skinMatrices.empty() ? MaterialGeometry::Static
                                                          : MaterialGeometry::Skinned;
        const auto sources = MaterialCompiler{}.generatePass(*proxy->compiled, pass, geometry, templates);
        NativeProgram* compiled = nullptr;
        try
        {
            if (!sources.error.empty())
                throw std::runtime_error(sources.error);
            compiled = &program(sources.vertex, sources.fragment);
        }
        catch (const std::exception& e)
        {
            error = e.what();
            if (proxy == errorMaterial)
                throw;
            if (loggedMaterialErrors.insert(error).second)
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Vulkan material: %s", e.what());
            auto fallback = items;
            for (auto& replacement : fallback)
                replacement.materialProxy = fallbackMaterial();
            drawItems(fallback, pass);
            return;
        }
        auto& p = *compiled;
        auto u = frameUniforms();
        auto resources = frameResources(u);
        value(u, "uModel", item.model);
        value(u, "uPreviousModel", item.previousModel);
        value(u, "uMvp", frame.projection * frame.view * item.model);
        value(u, "uInstanced", int(items.size() > 1));
        std::array<Mat4, 64> bones;
        std::fill(bones.begin(), bones.end(), Mat4::identity());
        std::copy_n(item.skinMatrices.begin(), std::min(item.skinMatrices.size(), bones.size()), bones.begin());
        arrayValue(u, "uBones", bones.data(), bones.size());
        std::array<Vec4, 64> parameters{};
        std::copy_n(proxy->numericSlots.begin(), std::min(proxy->numericSlots.size(), parameters.size()),
                    parameters.begin());
        arrayValue(u, "te_parameters", parameters.data(), parameters.size());
        value(u, "uMaterialTwoSided", int(proxy->state.twoSided));
        value(u, "uMaterialMaskCutoff", proxy->state.maskCutoff);
        for (const auto& slot : proxy->compiled->layout.textureSlots)
        {
            const auto binding = std::find_if(proxy->textures.begin(), proxy->textures.end(),
                                              [&](const auto& t) { return t.slot == slot.slot; });
            Image* image = slot.sampling == TextureSampling::Normal ? &normal : &white;
            if (binding != proxy->textures.end() && binding->resource && binding->resource->isReady())
                image = texture(binding->resource, slot.sampling == TextureSampling::SRgb, binding->version);
            resources["te_texture_" + std::to_string(slot.slot)].images = {image};
        }
        const auto set = descriptors(p, u, std::move(resources));
        auto& geometryBuffer = mesh(item.mesh);
        drawDepth = writeDepth = true;
        blend = pass == MaterialPass::Base && proxy->state.blendMode == MaterialBlendMode::Alpha;
        if (blend)
            writeDepth = false;
        cull = !proxy->state.twoSided;
        frontCull = pass == MaterialPass::Shadow;
        const bool wire = frame.renderMode == ViewportRenderMode::Wireframe && pass != MaterialPass::Shadow;
        const auto state = pipeline(p, VertexKind::Mesh, wire);
        struct Instance
        {
            Mat4 model, previous;
        };
        std::vector<Instance> instances;
        for (const auto& instance : items)
            instances.push_back({instance.model, instance.previousModel});
        vk::Slice indirect{};
        const bool gpuCull = items.size() > 1 && pass != MaterialPass::Shadow;
        const auto instanceSlice =
            gpuCull ? cullInstances(items, wire ? geometryBuffer.lineCount : geometryBuffer.count, indirect)
                    : upload(instances.data(), instances.size() * sizeof(Instance), 16);
        VkBuffer buffers[] = {geometryBuffer.vertices.handle, instanceSlice.buffer};
        VkDeviceSize offsets[] = {0, instanceSlice.offset};
        beginRendering();
        vkCmdBindPipeline(context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, state);
        vkCmdBindDescriptorSets(context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &set, 0, nullptr);
        vkCmdBindVertexBuffers(context.command(), 0, 2, buffers, offsets);
        vkCmdBindIndexBuffer(context.command(), wire ? geometryBuffer.lines.handle : geometryBuffer.indices.handle, 0,
                             VK_INDEX_TYPE_UINT32);
        if (gpuCull)
            vkCmdDrawIndexedIndirect(context.command(), indirect.buffer, indirect.offset, 1,
                                     sizeof(VkDrawIndexedIndirectCommand));
        else
            vkCmdDrawIndexed(context.command(), wire ? geometryBuffer.lineCount : geometryBuffer.count,
                             static_cast<std::uint32_t>(items.size()), 0, 0, 0);
        vkCmdEndRendering(context.command());
    }
    void destroyPrograms()
    {
        programs.clear();
    }
};

SdlVulkanRenderDevice::SdlVulkanRenderDevice(int w, int h, std::string name) : m_impl(std::make_unique<Impl>())
{
    m_impl->requestedWidth = w;
    m_impl->requestedHeight = h;
    m_impl->title = std::move(name);
}
SdlVulkanRenderDevice::~SdlVulkanRenderDevice()
{
    shutdown();
}
bool SdlVulkanRenderDevice::initialize()
{
    auto& p = *m_impl;
    try
    {
        if (!p.window.initialize(p.requestedWidth, p.requestedHeight, p.title,
                                 SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI))
            throw std::runtime_error(SDL_GetError());
        const char* validation = SDL_getenv("TINYENGINE_VULKAN_VALIDATION");
        if (!p.context.initialize(p.window.get(), validation && std::string(validation) == "1"))
            throw std::runtime_error(p.context.error);
        VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler.maxLod = 0;
        checked(vkCreateSampler(p.context.device, &sampler, nullptr, &p.sampler), "create sampler");
        sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
        checked(vkCreateSampler(p.context.device, &sampler, nullptr, &p.nearestSampler), "create depth sampler");
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler.maxLod = VK_LOD_CLAMP_NONE;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        checked(vkCreateSampler(p.context.device, &sampler, nullptr, &p.repeatSampler), "create texture sampler");
        p.white = p.context.createImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        p.normal = p.context.createImage(1, 1, VK_FORMAT_R16G16B16A16_SFLOAT,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        const std::uint8_t white[] = {255, 255, 255, 255};
        // Match GL's exact (0.5,0.5,1) neutral tangent normal; UNORM8 tilts sharp specular highlights.
        const std::uint16_t normal[] = {0x3800, 0x3800, 0x3c00, 0x3c00};
        if (!p.context.uploadImage(p.white, white, 4) || !p.context.uploadImage(p.normal, normal, sizeof(normal)))
            throw std::runtime_error(p.context.error);
        p.templates = ShaderPipeline::loadMaterialTemplates();
        p.watchTemplates();
        p.createTargets();
        p.initialized = true;
        if (p.ensure())
        {
            p.context.beginSwapchainRendering(true);
            p.context.endSwapchainRendering();
            p.context.present();
            p.recording = false;
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(p.context.physicalDevice, &properties);
        p.timestampPeriod = properties.limits.timestampPeriod;
        std::uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(p.context.physicalDevice, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(p.context.physicalDevice, &queueCount, queues.data());
        if (queues[p.context.queueFamily].timestampValidBits)
        {
            VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            queries.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queries.queryCount = 2;
            checked(vkCreateQueryPool(p.context.device, &queries, nullptr, &p.timestamps), "create GPU timestamps");
        }
        SDL_Log("Vulkan backend active: %s (Vulkan %u.%u)", properties.deviceName,
                VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion));
        return true;
    }
    catch (const std::exception& e)
    {
        p.error = e.what();
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Vulkan initialize: %s", e.what());
        shutdown();
        return false;
    }
}
void SdlVulkanRenderDevice::shutdown()
{
    if (!m_impl)
        return;
    auto& p = *m_impl;
    if (p.context.device)
    {
        p.context.waitIdle();
        p.destroyPrograms();
        p.destroyTargets();
        for (auto& item : p.meshes)
        {
            p.context.destroy(item.second.vertices);
            p.context.destroy(item.second.indices);
            p.context.destroy(item.second.lines);
        }
        p.meshes.clear();
        for (auto& item : p.textures)
            p.context.destroy(item.second.image);
        p.textures.clear();
        for (auto& page : p.fontPages)
            p.context.destroy(page.image);
        p.fontPages.clear();
        p.glyphAtlas.clear();
        for (auto view : p.frameViews)
            vkDestroyBufferView(p.context.device, view, nullptr);
        p.frameViews.clear();
        p.context.destroy(p.white);
        p.context.destroy(p.normal);
        for (auto s : {p.sampler, p.nearestSampler, p.repeatSampler})
            if (s)
                vkDestroySampler(p.context.device, s, nullptr);
        p.sampler = p.nearestSampler = p.repeatSampler = {};
        p.repeatViews.clear();
        if (p.timestamps)
            vkDestroyQueryPool(p.context.device, p.timestamps, nullptr);
        p.timestamps = {};
        p.timestampPending = false;
        p.context.shutdown();
    }
    p.window.shutdown();
    p.initialized = p.recording = false;
}
RenderBackend SdlVulkanRenderDevice::getBackend() const
{
    return RenderBackend::Vulkan;
}
bool SdlVulkanRenderDevice::pumpEvents()
{
    if (m_impl->context.isFatal())
    {
        m_impl->error = m_impl->context.error;
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Vulkan device stopped: %s", m_impl->error.c_str());
        return false;
    }
    return m_impl->window.pumpEvents(getViewportSize());
}
const InputState& SdlVulkanRenderDevice::GetInputState() const
{
    return m_impl->window.input;
}
float SdlVulkanRenderDevice::getAspectRatio() const
{
    return float(std::max(1, m_impl->width)) / std::max(1, m_impl->height);
}
Vec2 SdlVulkanRenderDevice::getViewportSize() const
{
    return {float(m_impl->width), float(m_impl->height)};
}
bool SdlVulkanRenderDevice::supportsDeferredRendering() const
{
    return m_impl->initialized;
}
void SdlVulkanRenderDevice::beginShadowPass(const FrameView& v, std::size_t index)
{
    auto& p = *m_impl;
    p.frame = v;
    p.cascade = std::min<std::size_t>(2, index);
    if (!p.ensure())
        return;
    p.deferred = false;
    p.select({}, &p.shadows[p.cascade], true);
}
void SdlVulkanRenderDevice::drawShadow(const RenderItem& item)
{
    m_impl->drawItems({item}, MaterialPass::Shadow);
}
void SdlVulkanRenderDevice::drawShadowInstanced(const std::vector<RenderItem>& items)
{
    m_impl->drawItems(items, MaterialPass::Shadow);
}
void SdlVulkanRenderDevice::endShadowPass()
{
    auto& p = *m_impl;
    if (p.recording)
        p.sample(p.shadows[p.cascade]);
}
void SdlVulkanRenderDevice::beginFrame(const FrameView& v)
{
    auto& p = *m_impl;
    if (!p.ensure())
        return;
    if (v.resetTemporalHistory || p.frame.renderMode != v.renderMode)
        p.history = false;
    p.frame = v;
    p.cascade = 0;
    p.deferred = false;
    p.select({&p.hdr, &p.velocity, &p.ambient}, &p.depth, true);
    std::vector<float> lightData;
    for (int i = 0; i < v.lightCount; ++i)
    {
        const auto& l = v.lights[i];
        lightData.insert(lightData.end(), {l.position.x, l.position.y, l.position.z, float(l.type), l.direction.x,
                                           l.direction.y, l.direction.z, l.range, l.color.x, l.color.y, l.color.z,
                                           l.intensity, l.innerConeCosine, l.outerConeCosine, 0, 0});
    }
    if (lightData.empty())
        lightData.resize(16);
    p.lights = p.texel(lightData.data(), lightData.size() * 4, VK_FORMAT_R32G32B32A32_SFLOAT);
    auto& grid = p.frame.clusteredLights;
    if (grid.offsets.empty())
    {
        grid.tilesX = grid.tilesY = grid.slicesZ = 1;
        grid.offsets = {0};
        grid.counts = {0};
    }
    std::vector<std::uint32_t> clusterData;
    for (std::size_t i = 0; i < grid.offsets.size(); ++i)
    {
        clusterData.push_back(grid.offsets[i]);
        clusterData.push_back(grid.counts[i]);
    }
    clusterData.insert(clusterData.end(), grid.lightIndices.begin(), grid.lightIndices.end());
    p.clusters = p.texel(clusterData.data(), clusterData.size() * 4, VK_FORMAT_R32_UINT);
}
void SdlVulkanRenderDevice::drawSkybox()
{
    auto& p = *m_impl;
    if (!p.recording || !p.frame.environmentSky || !p.frame.environmentSky->isReady() ||
        p.frame.renderMode == ViewportRenderMode::Wireframe)
        return;
    auto u = p.frameUniforms();
    Resources r;
    r["uEnvironmentSky"].images = {p.texture(p.frame.environmentSky, false)};
    p.fullscreen(kSkyFragmentShaderSource, std::move(u), std::move(r), {&p.hdr, &p.velocity, &p.ambient});
    p.select({&p.hdr, &p.velocity, &p.ambient}, &p.depth);
}
void SdlVulkanRenderDevice::draw(const RenderItem& item)
{
    m_impl->drawItems({item}, m_impl->deferred ? MaterialPass::GBuffer : MaterialPass::Base);
}
void SdlVulkanRenderDevice::drawInstanced(const std::vector<RenderItem>& items)
{
    m_impl->drawItems(items, m_impl->deferred ? MaterialPass::GBuffer : MaterialPass::Base);
}
void SdlVulkanRenderDevice::beginGBufferPass()
{
    auto& p = *m_impl;
    if (!p.ensure())
        return;
    p.deferred = true;
    p.select({&p.gbuffer[0], &p.gbuffer[1], &p.gbuffer[2], &p.gbuffer[3], &p.velocity}, nullptr, true);
    p.depthTarget = &p.depth;
}
void SdlVulkanRenderDevice::endGBufferPass()
{
    auto& p = *m_impl;
    p.deferred = false;
    p.select({&p.hdr, &p.velocity, &p.ambient}, &p.depth);
}
void SdlVulkanRenderDevice::executeDeferredLighting()
{
    auto& p = *m_impl;
    if (!p.recording)
        return;
    auto u = p.frameUniforms();
    value(u, "uInverseViewProjection", inverseViewProjection(p.frame));
    auto r = p.frameResources(u);
    r["uGBufferBase"].images = {&p.gbuffer[0]};
    r["uGBufferNormal"].images = {&p.gbuffer[1]};
    r["uGBufferEmissive"].images = {&p.gbuffer[2]};
    r["uGBufferFlags"].images = {&p.gbuffer[3]};
    r["uGBufferDepth"].images = {&p.depth};
    auto shader = p.templates.files.at("DeferredLighting.frag");
    const auto include = shader.find("#include \"Lighting.glsl\"");
    if (include != std::string::npos)
        shader.replace(include, 24, p.templates.files.at("Lighting.glsl"));
    p.fullscreen(shader.c_str(), std::move(u), std::move(r), {&p.hdr, nullptr, &p.ambient});
    p.select({&p.hdr, &p.velocity, &p.ambient}, &p.depth);
}
void SdlVulkanRenderDevice::executePostProcess(PostProcessPass pass)
{
    auto& p = *m_impl;
    if (!p.recording)
        return;
    const auto blit = [&](const char* source, Image* input, Image& output, UniformValues u = UniformValues{})
    {
        Resources r;
        r["uSource"].images = {input};
        p.fullscreen(source, std::move(u), std::move(r), {&output});
    };
    if (pass == PostProcessPass::HierarchicalDepth)
    {
        Image* input = &p.depth;
        for (auto& image : p.hzb)
        {
            UniformValues u;
            value(u, "uSourceTexel", Vec2{1.f / input->width, 1.f / input->height});
            blit(kHzbFragmentShaderSource, input, image, std::move(u));
            input = &image;
        }
    }
    else if (pass == PostProcessPass::Gtao)
    {
        if (!p.gtao || p.frame.renderMode != ViewportRenderMode::Lit)
            p.clearImage(p.gtaoImage, 1);
        else
        {
            UniformValues u;
            value(u, "uInverseResolution", Vec2{1.f / p.width, 1.f / p.height});
            value(u, "uInverseProjection", inverseProjection(p.frame.projection));
            Resources r;
            r["uDepth"].images = {&p.depth};
            p.fullscreen(kGtaoFragmentShaderSource, u, r, {&p.gtaoRaw});
            r["uSource"].images = {&p.gtaoRaw};
            p.fullscreen(kGtaoFilterFragmentShaderSource, u, r, {&p.gtaoImage});
        }
    }
    else if (pass == PostProcessPass::Bloom)
    {
        Image* input = &p.hdr;
        for (std::size_t i = 0; i < p.bloomImages.size(); ++i)
        {
            auto& image = p.bloomImages[i];
            if (!p.bloom || p.frame.renderMode != ViewportRenderMode::Lit)
                p.clearImage(image);
            else
            {
                UniformValues u;
                value(u, "uSourceTexel", Vec2{1.f / input->width, 1.f / input->height});
                value(u, "uExtract", int(i == 0));
                blit(kBloomFragmentShaderSource, input, image, std::move(u));
            }
            input = &image;
        }
    }
    else if (pass == PostProcessPass::TemporalResolve)
    {
        const int index = 1 - p.temporal;
        UniformValues u;
        value(u, "uInverseResolution", Vec2{1.f / p.width, 1.f / p.height});
        value(u, "uHistoryValid", int(p.history));
        value(u, "uTaaEnabled", int(p.taa && p.frame.renderMode == ViewportRenderMode::Lit));
        value(u, "uReprojectDepth", p.frame.previousViewProjection * inverseViewProjection(p.frame));
        value(u, "uDepthRange", Vec2{p.frame.clusteredLights.nearPlane, p.frame.clusteredLights.farPlane});
        Resources r;
        r["uScene"].images = {&p.hdr};
        r["uAmbient"].images = {&p.ambient};
        r["uVelocity"].images = {&p.velocity};
        r["uHistory"].images = {&p.temporalColor[p.temporal]};
        r["uHistoryDepth"].images = {&p.temporalDepth[p.temporal]};
        r["uDepth"].images = {&p.depth};
        r["uGtao"].images = {&p.gtaoImage};
        for (int i = 0; i < 5; ++i)
            r["uBloom" + std::to_string(i)].images = {&p.bloomImages[i]};
        p.fullscreen(kTaaFragmentShaderSource, std::move(u), std::move(r),
                     {&p.temporalColor[index], &p.temporalDepth[index]});
        p.temporal = index;
        p.history = true;
    }
    else if (pass == PostProcessPass::ToneMap)
    {
        UniformValues u;
        value(u, "uExposure", p.exposure);
        value(u, "uInverseResolution", Vec2{1.f / p.width, 1.f / p.height});
        value(u, "uFxaaEnabled", int(p.fxaa));
        value(u, "uViewportRenderMode", int(p.frame.renderMode));
        Resources r;
        r["uResolved"].images = {&p.temporalColor[p.temporal]};
        p.fullscreen(kFinalToneFragmentShaderSource, std::move(u), std::move(r), {&p.color});
    }
    else if (pass == PostProcessPass::Pixelate)
    {
        UniformValues u;
        value(u, "uResolution", Vec2{float(p.width), float(p.height)});
        value(u, "uBlockSize", p.blockSize);
        value(u, "uColorLevels", p.colorLevels);
        Resources r;
        r["uSceneColor"].images = {&p.color};
        p.fullscreen(kRetroFragmentShaderSource, std::move(u), std::move(r), {&p.retroColor});
    }
}
void SdlVulkanRenderDevice::drawParticles(const std::vector<Particle>& particles)
{
    auto& p = *m_impl;
    if (particles.empty() || !p.recording)
        return;
    struct Instance
    {
        Vec4 position, color, uv;
    };
    std::unordered_map<std::string, std::vector<Instance>> batches;
    for (const auto& particle : particles)
    {
        const float columns = float(std::max<int>(1, particle.flipbookColumns)),
                    rows = float(std::max<int>(1, particle.flipbookRows));
        const float col = std::fmod(float(particle.flipbookFrame), columns),
                    row = std::floor(float(particle.flipbookFrame) / columns);
        batches[particle.texturePath].push_back(
            {{particle.position.x, particle.position.y, particle.position.z, std::max(.001f, particle.size)},
             particle.color,
             {col / columns, row / rows, (col + 1) / columns, (row + 1) / rows}});
    }
    p.select({&p.hdr, &p.velocity, &p.ambient}, &p.depth);
    p.drawDepth = true;
    p.writeDepth = false;
    p.blend = true;
    p.cull = p.frontCull = false;
    for (auto& batch : batches)
    {
        UniformValues u;
        value(u, "uParticleView", p.frame.view);
        value(u, "uParticleProjection", p.frame.projection);
        value(u, "uHasParticleTexture", int(!batch.first.empty()));
        Resources r;
        r["uParticleTexture"].images = {p.uiTexture(batch.first)};
        auto& program = p.program(kParticleVertexShaderSource, kParticleFragmentShaderSource);
        const auto set = p.descriptors(program, u, r);
        const auto pipeline = p.pipeline(program, VertexKind::Particle);
        const auto slice = p.upload(batch.second.data(), batch.second.size() * sizeof(Instance), 16);
        p.beginRendering();
        vkCmdBindPipeline(p.context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(p.context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, program.layout, 0, 1, &set, 0,
                                nullptr);
        vkCmdBindVertexBuffers(p.context.command(), 0, 1, &slice.buffer, &slice.offset);
        vkCmdDraw(p.context.command(), 6, static_cast<std::uint32_t>(batch.second.size()), 0, 0);
        vkCmdEndRendering(p.context.command());
    }
}
void SdlVulkanRenderDevice::drawGpuParticles(const std::vector<GpuParticleEmitter>& emitters)
{
    auto& p = *m_impl;
    if (!p.recording)
        return;
    p.select({&p.hdr, &p.velocity, &p.ambient}, &p.depth);
    p.drawDepth = true;
    p.writeDepth = false;
    p.blend = true;
    p.cull = p.frontCull = false;
    for (const auto& emitter : emitters)
    {
        const auto& s = emitter.settings;
        if (!s.maximumParticles || s.emissionRate <= 0)
            continue;
        UniformValues u;
        value(u, "uParticleView", p.frame.view);
        value(u, "uParticleProjection", p.frame.projection);
        value(u, "uEmitterPosition", emitter.position);
        value(u, "uEmitterGravity", s.gravity);
        value(u, "uEmitterStartColor", s.startColor);
        value(u, "uEmitterEndColor", s.endColor);
        value(u, "uEmitterParams", Vec4{s.emissionRate, s.lifetime, s.speed, s.spread});
        value(u, "uEmitterFlipbook",
              Vec4{float(std::max<int>(1, s.flipbookColumns)), float(std::max<int>(1, s.flipbookRows)), s.startSize,
                   s.endSize});
        value(u, "uEmitterTime", emitter.elapsedTime);
        value(u, "uEmitterCapacity", float(s.maximumParticles));
        value(u, "uEmitterFps", s.flipbookFramesPerSecond * (s.flipbookLooping ? 1.f : -1.f));
        value(u, "uHasParticleTexture", int(!s.texturePath.empty()));
        Resources r;
        r["uParticleTexture"].images = {p.uiTexture(s.texturePath)};
        auto& program = p.program(kGpuParticleVertexShaderSource, kGpuParticleFragmentShaderSource);
        const auto set = p.descriptors(program, u, r);
        const auto pipeline = p.pipeline(program, VertexKind::None);
        p.beginRendering();
        vkCmdBindPipeline(p.context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(p.context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, program.layout, 0, 1, &set, 0,
                                nullptr);
        vkCmdDraw(p.context.command(), 6,
                  static_cast<std::uint32_t>(std::min<std::size_t>(s.maximumParticles, 1000000)), 0, 0);
        vkCmdEndRendering(p.context.command());
    }
}
void SdlVulkanRenderDevice::drawUi(const std::vector<UIDrawCommand>& commands)
{
    auto& p = *m_impl;
    if (commands.empty() || !p.recording)
        return;
    std::vector<std::shared_ptr<const TextLayoutResult>> layouts;
    for (const auto& command : commands)
    {
        auto layout = command.textLayout;
        if (!layout && !command.text.empty())
        {
            TextStyle style;
            style.size = std::max(1.f, command.fontSize);
            style.font = command.font;
            layout = std::make_shared<TextLayoutResult>(TextLayout::shape(command.text, command.rect.width, style,
                                                                          TextDirection::Auto,
                                                                          command.type != UIWidgetType::InputField));
        }
        if (layout)
            for (const auto& glyph : layout->glyphs)
                p.glyphAtlas.getGlyph(glyph.face, glyph.glyphId, glyph.style.size);
        layouts.push_back(std::move(layout));
    }
    const auto generation = p.glyphAtlas.getStats().generation;
    if (generation != p.fontGeneration)
    {
        for (auto& page : p.fontPages)
            p.context.destroy(page.image);
        p.fontPages.clear();
        p.fontGeneration = generation;
    }
    const auto& pages = p.glyphAtlas.getPages();
    p.fontPages.resize(pages.size());
    for (std::size_t i = 0; i < pages.size(); ++i)
    {
        auto& gpu = p.fontPages[i];
        const auto& page = pages[i];
        if (!gpu.image.handle)
            gpu.image = p.makeImage(page.width, page.height, VK_FORMAT_R8G8B8A8_UNORM);
        if (gpu.revision != page.revision)
        {
            if (!p.context.uploadImage(gpu.image, page.pixels.data(), page.pixels.size()))
                throw std::runtime_error(p.context.error);
            gpu.revision = page.revision;
        }
    }
    p.select({p.retro ? &p.retroColor : &p.color});
    p.drawDepth = p.writeDepth = p.cull = p.frontCull = false;
    p.blend = true;
    const auto quad = [&](const UIRect& rect, Vec4 tint, Image* image, const UIRect& clip, Vec4 uv = Vec4{0, 0, 1, 1})
    {
        if (rect.width <= 0 || rect.height <= 0)
            return;
        struct Vertex
        {
            Vec2 position, uv;
        };
        const Vertex vertices[] = {{{rect.x, rect.y}, {uv.x, uv.y}},
                                   {{rect.x + rect.width, rect.y}, {uv.z, uv.y}},
                                   {{rect.x + rect.width, rect.y + rect.height}, {uv.z, uv.w}},
                                   {{rect.x, rect.y}, {uv.x, uv.y}},
                                   {{rect.x + rect.width, rect.y + rect.height}, {uv.z, uv.w}},
                                   {{rect.x, rect.y + rect.height}, {uv.x, uv.w}}};
        UniformValues u;
        value(u, "uViewport", Vec2{float(p.width), float(p.height)});
        value(u, "uColor", tint);
        value(u, "uHasUiTexture", int(image != nullptr));
        Resources r;
        r["uUiTexture"].images = {image ? image : &p.white};
        auto& program = p.program(kUiVertexShaderSource, kUiFragmentShaderSource);
        const auto set = p.descriptors(program, u, r);
        const auto pipeline = p.pipeline(program, VertexKind::Ui);
        const auto slice = p.upload(vertices, sizeof(vertices), 16);
        const int x0 = std::clamp(int(std::floor(clip.x)), 0, p.width),
                  x1 = std::clamp(int(std::ceil(clip.x + clip.width)), 0, p.width);
        const int y0 = std::clamp(p.height - int(std::ceil(clip.y + clip.height)), 0, p.height),
                  y1 = std::clamp(p.height - int(std::floor(clip.y)), 0, p.height);
        p.beginRendering();
        VkRect2D scissor{{x0, y0}, {std::uint32_t(std::max(0, x1 - x0)), std::uint32_t(std::max(0, y1 - y0))}};
        vkCmdSetScissor(p.context.command(), 0, 1, &scissor);
        vkCmdBindPipeline(p.context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(p.context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, program.layout, 0, 1, &set, 0,
                                nullptr);
        vkCmdBindVertexBuffers(p.context.command(), 0, 1, &slice.buffer, &slice.offset);
        vkCmdDraw(p.context.command(), 6, 1, 0, 0);
        vkCmdEndRendering(p.context.command());
    };
    for (std::size_t i = 0; i < commands.size(); ++i)
    {
        const auto& command = commands[i];
        if (command.rect.width <= 0 || command.rect.height <= 0)
            continue;
        const auto clip = command.clipRect.width > 0 && command.clipRect.height > 0 ? command.clipRect : command.rect;
        if (command.type != UIWidgetType::Label)
            quad(command.rect, command.color,
                 command.type == UIWidgetType::Image ? p.uiTexture(command.imagePath) : nullptr, clip);
        for (const auto& rect : command.selectionRects)
            quad(rect, {.16f, .38f, .72f, .8f}, nullptr, clip);
        if (layouts[i] && command.type != UIWidgetType::Image && command.type != UIWidgetType::Panel &&
            command.type != UIWidgetType::ProgressBar)
        {
            const auto& layout = *layouts[i];
            const float x = command.rect.x + command.textOffset.x;
            const float y =
                command.rect.y + command.textOffset.y + std::max(0.f, (command.rect.height - layout.height) * .5f);
            for (const auto& shaped : layout.glyphs)
            {
                const auto glyph = p.glyphAtlas.getGlyph(shaped.face, shaped.glyphId, shaped.style.size);
                if (!glyph)
                {
                    quad({x + shaped.x, y + shaped.y - shaped.style.size * .7f, std::max(1.f, shaped.advance * .7f),
                          shaped.style.size * .7f},
                         {.65f, .18f, .18f, command.color.w}, nullptr, clip);
                    continue;
                }
                if (glyph->width <= 0 || glyph->height <= 0 || glyph->page >= p.fontPages.size())
                    continue;
                const auto& page = pages[glyph->page];
                const auto base = command.type == UIWidgetType::Label
                                      ? command.color
                                      : Vec4{command.textColor.x, command.textColor.y, command.textColor.z,
                                             command.textColor.w * command.color.w};
                const Vec4 tint{base.x * shaped.style.color.x, base.y * shaped.style.color.y,
                                base.z * shaped.style.color.z, base.w * shaped.style.color.w};
                quad({x + shaped.x + glyph->bearingX, y + shaped.y - glyph->bearingY, float(glyph->width),
                      float(glyph->height)},
                     tint, &p.fontPages[glyph->page].image, clip,
                     {float(glyph->x) / page.width, float(glyph->y) / page.height,
                      float(glyph->x + glyph->width) / page.width, float(glyph->y + glyph->height) / page.height});
            }
        }
        for (const auto& rect : command.compositionRects)
            quad(rect, {.8f, .85f, 1, 1}, nullptr, clip);
        quad(command.caretRect, {1, 1, 1, 1}, nullptr, clip);
    }
}
void SdlVulkanRenderDevice::endFrame()
{
    auto& p = *m_impl;
    if (!p.recording)
        return;
    p.sample(p.retro ? p.retroColor : p.color);
    p.drawDepth = p.writeDepth = p.blend = p.cull = p.frontCull = false;
    p.depthTarget = nullptr;
    if (p.presentScene)
    {
        auto& program = p.program(kToneVertexShaderSource, kWindowFragment);
        UniformValues u;
        value(u, "uSwapchainSrgb",
              int(p.context.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                  p.context.swapchainFormat == VK_FORMAT_R8G8B8A8_SRGB));
        Resources r;
        r["uSceneColor"].images = {p.retro ? &p.retroColor : &p.color};
        const auto set = p.descriptors(program, u, r);
        const auto pipeline = p.pipeline(program, VertexKind::None, false, p.context.swapchainFormat);
        p.context.beginSwapchainRendering(true);
        vkCmdBindPipeline(p.context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(p.context.command(), VK_PIPELINE_BIND_POINT_GRAPHICS, program.layout, 0, 1, &set, 0,
                                nullptr);
        vkCmdDraw(p.context.command(), 3, 1, 0, 0);
        p.context.endSwapchainRendering();
    }
    else
    {
        p.context.beginSwapchainRendering(true);
        p.context.endSwapchainRendering();
    }
}
void SdlVulkanRenderDevice::present()
{
    auto& p = *m_impl;
    if (p.recording)
    {
        if (p.timestamps)
        {
            vkCmdWriteTimestamp2(p.context.command(), VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, p.timestamps, 1);
            p.timestampPending = true;
        }
        p.context.present();
        p.recording = false;
    }
}
bool SdlVulkanRenderDevice::validateMaterial(const CompiledMaterial& m, std::vector<MaterialDiagnostic>& diagnostics)
{
    try
    {
        for (auto pass : {MaterialPass::Base, MaterialPass::Shadow, MaterialPass::GBuffer})
            for (auto geometry : {MaterialGeometry::Static, MaterialGeometry::Skinned, MaterialGeometry::Instanced})
            {
                auto source = MaterialCompiler{}.generatePass(m, pass, geometry, m_impl->templates);
                m_impl->program(source.vertex, source.fragment);
            }
        return true;
    }
    catch (const std::exception& e)
    {
        m_impl->error = e.what();
        diagnostics.push_back({MaterialDiagnosticSeverity::Error, {}, 0, -1, e.what()});
        return false;
    }
}
std::vector<Vec4> SdlVulkanRenderDevice::readHdrAttachment(std::size_t attachment)
{
    auto& p = *m_impl;
    Image& image = attachment == 1 ? p.velocity : attachment == 2 ? p.ambient : p.hdr;
    const auto bytes = p.context.readImage(image, attachment == 1 ? 4 : 8);
    if (bytes.empty())
        throw std::runtime_error(p.context.error);
    std::vector<Vec4> result(std::size_t(image.width) * image.height);
    const auto* values = reinterpret_cast<const std::uint16_t*>(bytes.data());
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        if (attachment == 1)
            result[i] = {halfFloat(values[i * 2]), halfFloat(values[i * 2 + 1]), 0, 1};
        else
            result[i] = {halfFloat(values[i * 4]), halfFloat(values[i * 4 + 1]), halfFloat(values[i * 4 + 2]),
                         halfFloat(values[i * 4 + 3])};
    }
    return result;
}
std::vector<std::uint8_t> SdlVulkanRenderDevice::readSceneColorPixels()
{
    auto& p = *m_impl;
    return p.context.readImage(p.retro ? p.retroColor : p.color, 4);
}
std::vector<float> SdlVulkanRenderDevice::readDepthPixels()
{
    auto& p = *m_impl;
    const auto bytes = p.context.readImage(p.depth, 4);
    std::vector<float> result(bytes.size() / 4);
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}
std::size_t SdlVulkanRenderDevice::getCachedProgramCountForTests() const
{
    return m_impl->programs.size();
}
float SdlVulkanRenderDevice::readShadowDepthForTests(int x, int y, std::size_t cascade)
{
    auto& p = *m_impl;
    auto& image = p.shadows.at(cascade);
    const auto bytes = p.context.readImage(image, 4);
    float result = 1;
    const auto offset =
        (std::size_t(std::clamp(y, 0, int(image.height) - 1)) * image.width + std::clamp(x, 0, int(image.width) - 1)) *
        4;
    if (bytes.size() >= offset + 4)
        std::memcpy(&result, bytes.data() + offset, 4);
    return result;
}
VulkanInterop SdlVulkanRenderDevice::getVulkanInterop() const
{
    const auto& p = *m_impl;
    return {p.context.instance,        p.context.physicalDevice,  p.context.device,       p.context.queue,
            p.context.queueFamily,     p.context.swapchainFormat, p.context.imageCount(), p.context.command(),
            p.context.swapchainView(), p.context.extent};
}
VkImageView SdlVulkanRenderDevice::getSceneImageView() const
{
    return m_impl->retro ? m_impl->retroColor.view : m_impl->color.view;
}
VkImageView SdlVulkanRenderDevice::getUiImageView(const std::string& path)
{
    if (!m_impl->ensure())
        return {};
    return m_impl->uiTexture(path)->view;
}
std::uint64_t SdlVulkanRenderDevice::getSceneGeneration() const
{
    return m_impl->generation;
}
std::uint32_t SdlVulkanRenderDevice::getValidationErrorCount() const
{
    return m_impl->context.validationErrors;
}
void* SdlVulkanRenderDevice::getNativeWindow() const
{
    return m_impl->window.get();
}
void SdlVulkanRenderDevice::setEventCallback(std::function<void(const void*)> cb)
{
    m_impl->window.eventCallback = std::move(cb);
}
void SdlVulkanRenderDevice::setCloseRequestCallback(std::function<void()> cb)
{
    m_impl->window.closeRequestCallback = std::move(cb);
}
void SdlVulkanRenderDevice::setTextInputEnabled(bool v)
{
    m_impl->window.setTextInputEnabled(v);
}
void SdlVulkanRenderDevice::setTextInputRect(const UIRect& rect)
{
    m_impl->window.setTextInputRect(rect, getViewportSize());
}
GlyphAtlasStats SdlVulkanRenderDevice::getGlyphAtlasStats() const
{
    return m_impl->glyphAtlas.getStats();
}
void SdlVulkanRenderDevice::setEscapeQuits(bool v)
{
    m_impl->window.escapeQuits = v;
}
void SdlVulkanRenderDevice::setMouseCapture(bool v)
{
    m_impl->window.setMouseCapture(v);
}
bool SdlVulkanRenderDevice::isMouseCaptured() const
{
    return m_impl->window.isMouseCaptured();
}
bool SdlVulkanRenderDevice::rumbleController(float a, float b, std::uint32_t ms)
{
    return m_impl->window.rumbleController(a, b, ms);
}
bool SdlVulkanRenderDevice::isControllerConnected() const
{
    return m_impl->window.isControllerConnected();
}
float SdlVulkanRenderDevice::getDpiScale() const
{
    return m_impl->window.dpiScale;
}
bool SdlVulkanRenderDevice::enableSceneRenderTarget(bool enabled, int w, int h)
{
    m_impl->presentScene = !enabled;
    return enabled ? resizeSceneRenderTarget(w, h) : true;
}
bool SdlVulkanRenderDevice::resizeSceneRenderTarget(int w, int h)
{
    if (w < 1 || h < 1)
        return false;
    m_impl->requestedWidth = w;
    m_impl->requestedHeight = h;
    return true;
}
int SdlVulkanRenderDevice::getSceneTargetWidth() const
{
    return m_impl->width;
}
int SdlVulkanRenderDevice::getSceneTargetHeight() const
{
    return m_impl->height;
}
std::uintptr_t SdlVulkanRenderDevice::getSceneColorTexture() const
{
    return reinterpret_cast<std::uintptr_t>(getSceneImageView());
}
std::uintptr_t SdlVulkanRenderDevice::getUiTexture(const std::string& path)
{
    return reinterpret_cast<std::uintptr_t>(getUiImageView(path));
}
void SdlVulkanRenderDevice::setExposure(float v)
{
    m_impl->exposure = std::clamp(v, .01f, 20.f);
}
float SdlVulkanRenderDevice::getExposure() const
{
    return m_impl->exposure;
}
void SdlVulkanRenderDevice::setFxaaEnabled(bool v)
{
    m_impl->fxaa = v;
}
bool SdlVulkanRenderDevice::isFxaaEnabled() const
{
    return m_impl->fxaa;
}
void SdlVulkanRenderDevice::setBloomEnabled(bool v)
{
    m_impl->bloom = v;
}
bool SdlVulkanRenderDevice::isBloomEnabled() const
{
    return m_impl->bloom;
}
void SdlVulkanRenderDevice::setGtaoEnabled(bool v)
{
    m_impl->gtao = v;
}
bool SdlVulkanRenderDevice::isGtaoEnabled() const
{
    return m_impl->gtao;
}
void SdlVulkanRenderDevice::setSsaoEnabled(bool v)
{
    setGtaoEnabled(v);
}
bool SdlVulkanRenderDevice::isSsaoEnabled() const
{
    return isGtaoEnabled();
}
void SdlVulkanRenderDevice::setTaaEnabled(bool v)
{
    m_impl->taa = v;
    m_impl->history = false;
}
bool SdlVulkanRenderDevice::isTaaEnabled() const
{
    return m_impl->taa;
}
bool SdlVulkanRenderDevice::usesTemporalJitter() const
{
    return m_impl->taa && !m_impl->retro;
}
void SdlVulkanRenderDevice::setRetroPostProcess(bool v, float size, float levels)
{
    m_impl->retro = v;
    m_impl->blockSize = std::clamp(size, 1.f, 16.f);
    m_impl->colorLevels = std::clamp(levels, 2.f, 256.f);
    m_impl->history = false;
}
bool SdlVulkanRenderDevice::isRetroPostProcessEnabled() const
{
    return m_impl->retro;
}
float SdlVulkanRenderDevice::getRetroBlockSize() const
{
    return m_impl->blockSize;
}
float SdlVulkanRenderDevice::getRetroColorLevels() const
{
    return m_impl->colorLevels;
}
float SdlVulkanRenderDevice::getLastGpuFrameMilliseconds() const
{
    return m_impl->gpuMs;
}
std::size_t SdlVulkanRenderDevice::getHzbLevelCount() const
{
    return m_impl->hzb.size();
}
bool SdlVulkanRenderDevice::isIndirectDrawSupported() const
{
    return m_impl->initialized;
}
bool SdlVulkanRenderDevice::isGpuDrivenCullingSupported() const
{
    return m_impl->initialized;
}
std::size_t SdlVulkanRenderDevice::getResidentGpuBytes() const
{
    return m_impl->residentBytes() + m_impl->glyphAtlas.getStats().byteSize;
}
void SdlVulkanRenderDevice::setGpuMemoryBudget(std::size_t n)
{
    m_impl->budget = std::max<std::size_t>(1024, n);
}
void SdlVulkanRenderDevice::trimGpuResources()
{
    m_impl->trimPending = true;
    m_impl->programTrimPending = true;
}
const std::string& SdlVulkanRenderDevice::GetLastShaderError() const
{
    return m_impl->error;
}
bool SdlVulkanRenderDevice::validateShader(const std::string& path)
{
    auto source = ShaderPipeline::preprocess(path);
    if (!source.success)
    {
        m_impl->error = source.error;
        return false;
    }
    return vk::validateStage(source.source,
                             path.ends_with(".vert")   ? VK_SHADER_STAGE_VERTEX_BIT
                             : path.ends_with(".comp") ? VK_SHADER_STAGE_COMPUTE_BIT
                                                       : VK_SHADER_STAGE_FRAGMENT_BIT,
                             m_impl->error);
}
bool SdlVulkanRenderDevice::reloadShaders()
{
    try
    {
        return m_impl->reloadTemplates();
    }
    catch (const std::exception& e)
    {
        m_impl->error = e.what();
        return false;
    }
}
} // namespace tinyengine
