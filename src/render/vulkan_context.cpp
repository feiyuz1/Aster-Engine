#include "vulkan_context.h"
#include <SDL.h>
#include <SDL_vulkan.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace tinyengine::vk
{
namespace
{
constexpr VkDeviceSize ringSize = 64ull * 1024 * 1024;
constexpr VkBufferUsageFlags uploadUsage =
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
    VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
    VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
constexpr std::uint64_t forever = std::numeric_limits<std::uint64_t>::max();
VKAPI_ATTR VkBool32 VKAPI_CALL debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    auto& context = *static_cast<Context*>(user);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        std::atomic_ref<std::uint32_t>(context.validationErrors).fetch_add(1, std::memory_order_relaxed);
    std::cerr << "[Vulkan validation] " << (data && data->pMessage ? data->pMessage : "unknown message") << '\n';
    return VK_FALSE;
}
bool hasExtension(const std::vector<VkExtensionProperties>& properties, const char* name)
{
    return std::any_of(properties.begin(), properties.end(),
                       [name](const auto& p) { return std::strcmp(name, p.extensionName) == 0; });
}
VkImageSubresourceRange imageRange(VkImageAspectFlags aspect, std::uint32_t mipLevels = 1)
{
    return {aspect, 0, mipLevels, 0, 1};
}
} // namespace

struct Context::Impl
{
    struct Frame
    {
        struct UploadPage
        {
            Buffer buffer;
            VkDeviceSize offset = 0;
        };
        VkCommandPool pool{};
        VkCommandBuffer command{};
        VkSemaphore acquire{};
        VkFence fence{};
        Buffer ring;
        VkDeviceSize offset = 0;
        std::vector<UploadPage> overflowPages;
        std::vector<VkDescriptorPool> descriptorPools;
        std::size_t descriptorPoolIndex = 0;
    };
    struct State
    {
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkAccessFlags2 access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    };
    SDL_Window* window = nullptr;
    VkSurfaceKHR surface{};
    VkDebugUtilsMessengerEXT messenger{};
    VkSwapchainKHR swapchain{};
    VkQueue presentQueue{};
    std::uint32_t presentFamily = 0;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    // Presentation completion is tied to reacquiring an image, not a frame fence.
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkImageLayout> layouts;
    std::array<Frame, 2> frames;
    std::uint32_t frameIndex = 0, imageIndex = 0;
    VkFence lastFrameFence{};
    VkPhysicalDeviceProperties properties{};
    VkDeviceSize maxBufferSize = 0;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkCommandPool transferPool{};
    VkCommandBuffer transferCommand{};
    VkFence transferFence{};
    bool active = false, transferActive = false, rendering = false;
    bool acquirePending = false, rebuild = false, fatal = false;
    std::unordered_map<VkImage, State> states;
};

Context::Context() = default;
Context::~Context()
{
    shutdown();
}

bool Context::check(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS)
        return true;
    error = std::string(operation) + " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")";
    if (impl_ && result == VK_ERROR_DEVICE_LOST)
        impl_->fatal = true;
    return false;
}

bool Context::initialize(SDL_Window* window, bool validation)
{
    shutdown();
    error.clear();
    validationErrors = 0;
    impl_ = std::make_unique<Impl>();
    impl_->window = window;
    if (!window)
    {
        error = "Vulkan initialization requires an SDL window";
        return false;
    }
    auto fail = [this]()
    {
        shutdown();
        return false;
    };
    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerateVersion)
        enumerateVersion(&loaderVersion);
    if (loaderVersion < VK_API_VERSION_1_3)
    {
        error = "Vulkan loader does not support Vulkan 1.3";
        return fail();
    }
    unsigned extensionCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr))
    {
        error = SDL_GetError();
        return fail();
    }
    std::vector<const char*> extensions(extensionCount);
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, extensions.data()))
    {
        error = SDL_GetError();
        return fail();
    }
    std::vector<const char*> layers;
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debugMessage;
    debug.pUserData = this;
    VkValidationFeatureEnableEXT syncValidation = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    validationFeatures.enabledValidationFeatureCount = 1;
    validationFeatures.pEnabledValidationFeatures = &syncValidation;
    if (validation)
    {
        std::uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> available(count);
        vkEnumerateInstanceLayerProperties(&count, available.data());
        if (std::none_of(available.begin(), available.end(),
                         [](const auto& layer)
                         { return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0; }))
        {
            error = "VK_LAYER_KHRONOS_validation was requested but is unavailable";
            return fail();
        }
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        vkEnumerateInstanceExtensionProperties(layers.front(), &count, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(count);
        vkEnumerateInstanceExtensionProperties(layers.front(), &count, availableExtensions.data());
        if (!hasExtension(availableExtensions, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME))
        {
            error = "VK_EXT_validation_features is required for synchronization validation";
            return fail();
        }
        extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        debug.pNext = &validationFeatures;
    }
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Aster Engine";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 27, 0);
    app.pEngineName = "TinyEngine";
    app.engineVersion = app.applicationVersion;
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    create.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    create.ppEnabledExtensionNames = extensions.data();
    create.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    create.ppEnabledLayerNames = layers.data();
    create.pNext = validation ? &debug : nullptr;
    if (!check(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance"))
        return fail();
    if (validation)
    {
        debug.pNext = nullptr;
        auto createDebug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!createDebug ||
            !check(createDebug(instance, &debug, nullptr, &impl_->messenger), "vkCreateDebugUtilsMessengerEXT"))
        {
            if (error.empty())
                error = "Vulkan debug messenger entry point unavailable";
            return fail();
        }
    }
    if (!SDL_Vulkan_CreateSurface(window, instance, &impl_->surface))
    {
        error = SDL_GetError();
        return fail();
    }
    std::uint32_t count = 0;
    if (!check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices"))
        return fail();
    std::vector<VkPhysicalDevice> devices(count);
    if (!check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices"))
        return fail();
    int bestScore = -1;
    for (auto candidate : devices)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(candidate, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3)
            continue;
        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (!features13.dynamicRendering || !features13.synchronization2 || !features13.shaderDemoteToHelperInvocation)
            continue;
        std::uint32_t extensionTotal = 0;
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionTotal, nullptr);
        std::vector<VkExtensionProperties> deviceExtensions(extensionTotal);
        vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionTotal, deviceExtensions.data());
        if (!hasExtension(deviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            continue;
        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        std::uint32_t graphics = UINT32_MAX, present = UINT32_MAX;
        for (std::uint32_t i = 0; i < familyCount; ++i)
        {
            VkBool32 supported = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, impl_->surface, &supported);
            const bool graphicsCompute = (families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                                         (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
            if (supported)
                present = i;
            if (graphicsCompute)
                graphics = i;
            if (graphicsCompute && supported)
            {
                graphics = present = i;
                break;
            }
        }
        if (graphics == UINT32_MAX || present == UINT32_MAX)
            continue;
        const int score =
            (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 0) + (graphics == present ? 10 : 0);
        if (score > bestScore)
        {
            bestScore = score;
            physicalDevice = candidate;
            queueFamily = graphics;
            impl_->presentFamily = present;
            impl_->properties = properties;
        }
    }
    if (!physicalDevice)
    {
        error = "No Vulkan 1.3 device with dynamic rendering, synchronization2, shaderDemoteToHelperInvocation, "
                "graphics/compute and presentation";
        return fail();
    }
    VkPhysicalDeviceMaintenance4Properties maintenance4{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_PROPERTIES};
    VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties2.pNext = &maintenance4;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
    impl_->maxBufferSize = maintenance4.maxBufferSize;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &impl_->memoryProperties);
    float priority = 1.0f;
    std::array<VkDeviceQueueCreateInfo, 2> queues{};
    queues[0] = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queues[0].queueFamilyIndex = queueFamily;
    queues[0].queueCount = 1;
    queues[0].pQueuePriorities = &priority;
    queues[1] = queues[0];
    queues[1].queueFamilyIndex = impl_->presentFamily;
    VkPhysicalDeviceVulkan13Features supported13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features supported12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features supported11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    supported13.pNext = &supported12;
    supported12.pNext = &supported11;
    VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    supported.pNext = &supported13;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);
    VkPhysicalDeviceVulkan13Features enabled13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    enabled13.dynamicRendering = VK_TRUE;
    enabled13.synchronization2 = VK_TRUE;
    enabled13.shaderDemoteToHelperInvocation = VK_TRUE;
    enabled13.maintenance4 = supported13.maintenance4;
    VkPhysicalDeviceVulkan12Features enabled12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    enabled12.separateDepthStencilLayouts = supported12.separateDepthStencilLayouts;
    enabled12.drawIndirectCount = supported12.drawIndirectCount;
    enabled12.samplerFilterMinmax = supported12.samplerFilterMinmax;
    VkPhysicalDeviceVulkan11Features enabled11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    enabled11.shaderDrawParameters = supported11.shaderDrawParameters;
    enabled13.pNext = &enabled12;
    enabled12.pNext = &enabled11;
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // The pinned ImGui backend resolves the KHR entry points even on Vulkan 1.3.
    std::uint32_t deviceExtensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, nullptr);
    std::vector<VkExtensionProperties> availableDeviceExtensions(deviceExtensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount,
                                         availableDeviceExtensions.data());
    if (hasExtension(availableDeviceExtensions, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
        deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    VkDeviceCreateInfo deviceCreate{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceCreate.pNext = &enabled13;
    deviceCreate.queueCreateInfoCount = queueFamily == impl_->presentFamily ? 1 : 2;
    deviceCreate.pQueueCreateInfos = queues.data();
    deviceCreate.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
    deviceCreate.ppEnabledExtensionNames = deviceExtensions.data();
    deviceCreate.pEnabledFeatures = &supported.features;
    if (!check(vkCreateDevice(physicalDevice, &deviceCreate, nullptr, &device), "vkCreateDevice"))
        return fail();
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    vkGetDeviceQueue(device, impl_->presentFamily, 0, &impl_->presentQueue);
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = queueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (auto& frame : impl_->frames)
    {
        if (!check(vkCreateCommandPool(device, &poolInfo, nullptr, &frame.pool), "vkCreateCommandPool"))
            return fail();
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = frame.pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        if (!check(vkAllocateCommandBuffers(device, &allocate, &frame.command), "vkAllocateCommandBuffers") ||
            !check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.acquire), "vkCreateSemaphore") ||
            !check(vkCreateFence(device, &fenceInfo, nullptr, &frame.fence), "vkCreateFence"))
            return fail();
        frame.ring = createBuffer(ringSize, uploadUsage,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (!frame.ring.handle)
            return fail();
    }
    if (!check(vkCreateCommandPool(device, &poolInfo, nullptr, &impl_->transferPool), "vkCreateCommandPool"))
        return fail();
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = impl_->transferPool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    if (!check(vkAllocateCommandBuffers(device, &allocate, &impl_->transferCommand), "vkAllocateCommandBuffers") ||
        !check(vkCreateFence(device, &fenceInfo, nullptr, &impl_->transferFence), "vkCreateFence"))
        return fail();
    int width = 0, height = 0;
    SDL_Vulkan_GetDrawableSize(window, &width, &height);
    if (width > 0 && height > 0 && !recreateSwapchain())
        return fail();
    return true;
}

std::uint32_t Context::memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const
{
    for (std::uint32_t i = 0; i < impl_->memoryProperties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (impl_->memoryProperties.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    return UINT32_MAX;
}

Buffer Context::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
    Buffer buffer;
    if (!device || !size)
    {
        error = "Cannot create a zero-sized buffer or use an uninitialized device";
        return buffer;
    }
    if (size > impl_->maxBufferSize)
    {
        error = "Buffer size exceeds the Vulkan device limit";
        return {};
    }
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size = size;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!check(vkCreateBuffer(device, &create, nullptr, &buffer.handle), "vkCreateBuffer"))
        return {};
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType(requirements.memoryTypeBits, properties);
    if (allocate.memoryTypeIndex == UINT32_MAX)
    {
        error = "No compatible Vulkan buffer memory type";
        destroy(buffer);
        return {};
    }
    if (!check(vkAllocateMemory(device, &allocate, nullptr, &buffer.memory), "vkAllocateMemory") ||
        !check(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0), "vkBindBufferMemory"))
    {
        destroy(buffer);
        return {};
    }
    if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
        !check(vkMapMemory(device, buffer.memory, 0, size, 0, &buffer.mapped), "vkMapMemory"))
    {
        destroy(buffer);
        return {};
    }
    buffer.size = size;
    return buffer;
}

void Context::destroy(Buffer& buffer)
{
    if (device)
    {
        if (buffer.mapped)
            vkUnmapMemory(device, buffer.memory);
        if (buffer.handle)
            vkDestroyBuffer(device, buffer.handle, nullptr);
        if (buffer.memory)
            vkFreeMemory(device, buffer.memory, nullptr);
    }
    buffer = {};
}

Image Context::createImage(std::uint32_t width, std::uint32_t height, VkFormat format, VkImageUsageFlags usage,
                           VkImageAspectFlags aspect, std::uint32_t mipLevels)
{
    Image image;
    if (!device || !width || !height)
    {
        error = "Cannot create a zero-sized image or use an uninitialized device";
        return image;
    }
    VkImageFormatProperties properties;
    if (!check(vkGetPhysicalDeviceImageFormatProperties(physicalDevice, format, VK_IMAGE_TYPE_2D,
                                                        VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties),
               "vkGetPhysicalDeviceImageFormatProperties"))
        return {};
    if (width > properties.maxExtent.width || height > properties.maxExtent.height)
    {
        error = "Image extent exceeds Vulkan format limits";
        return {};
    }
    if (!mipLevels || mipLevels > properties.maxMipLevels ||
        mipLevels > static_cast<std::uint32_t>(std::bit_width(std::max(width, height))))
    {
        error = "Image mip count must fit its dimensions and Vulkan format limits";
        return {};
    }
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    create.imageType = VK_IMAGE_TYPE_2D;
    create.format = format;
    create.extent = {width, height, 1};
    create.mipLevels = mipLevels;
    create.arrayLayers = 1;
    create.samples = VK_SAMPLE_COUNT_1_BIT;
    create.tiling = VK_IMAGE_TILING_OPTIMAL;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!check(vkCreateImage(device, &create, nullptr, &image.handle), "vkCreateImage"))
        return {};
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, image.handle, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocate.memoryTypeIndex == UINT32_MAX)
    {
        error = "No compatible device-local image memory";
        destroy(image);
        return {};
    }
    if (!check(vkAllocateMemory(device, &allocate, nullptr, &image.memory), "vkAllocateMemory") ||
        !check(vkBindImageMemory(device, image.handle, image.memory, 0), "vkBindImageMemory"))
    {
        destroy(image);
        return {};
    }
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.handle;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = imageRange(aspect, mipLevels);
    if (!check(vkCreateImageView(device, &view, nullptr, &image.view), "vkCreateImageView"))
    {
        destroy(image);
        return {};
    }
    image.format = format;
    image.width = width;
    image.height = height;
    image.mipLevels = mipLevels;
    image.aspect = aspect;
    image.usage = usage;
    impl_->states.emplace(image.handle, Impl::State{});
    return image;
}

void Context::destroy(Image& image)
{
    if (device)
    {
        if (image.view)
            vkDestroyImageView(device, image.view, nullptr);
        if (image.handle)
            vkDestroyImage(device, image.handle, nullptr);
        if (image.memory)
            vkFreeMemory(device, image.memory, nullptr);
        if (impl_)
            impl_->states.erase(image.handle);
    }
    image = {};
}

bool Context::recreateSwapchain()
{
    int width = 0, height = 0;
    SDL_Vulkan_GetDrawableSize(impl_->window, &width, &height);
    if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_MINIMIZED))
        return false;
    if (!check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle(swapchain resize)"))
        return false;
    VkSurfaceCapabilitiesKHR capabilities;
    if (!check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, impl_->surface, &capabilities),
               "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return false;
    std::uint32_t count = 0;
    if (!check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, impl_->surface, &count, nullptr),
               "vkGetPhysicalDeviceSurfaceFormatsKHR"))
        return false;
    std::vector<VkSurfaceFormatKHR> formats(count);
    if (!check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, impl_->surface, &count, formats.data()),
               "vkGetPhysicalDeviceSurfaceFormatsKHR"))
        return false;
    if (formats.empty())
    {
        error = "Vulkan surface exposes no formats";
        return false;
    }
    VkSurfaceFormatKHR chosen = formats.front();
    for (auto preferred : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM})
    {
        auto found =
            std::find_if(formats.begin(), formats.end(),
                         [preferred](const auto& value) {
                             return value.format == preferred && value.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                         });
        if (found != formats.end())
        {
            chosen = *found;
            break;
        }
    }
    if (chosen.format == VK_FORMAT_UNDEFINED)
        chosen.format = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D nextExtent = capabilities.currentExtent;
    if (nextExtent.width == UINT32_MAX)
        nextExtent = {std::clamp(static_cast<std::uint32_t>(width), capabilities.minImageExtent.width,
                                 capabilities.maxImageExtent.width),
                      std::clamp(static_cast<std::uint32_t>(height), capabilities.minImageExtent.height,
                                 capabilities.maxImageExtent.height)};
    if (!nextExtent.width || !nextExtent.height)
        return false;
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
    {
        error = "Swapchain images do not support color attachments";
        return false;
    }
    VkSwapchainCreateInfoKHR create{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    create.surface = impl_->surface;
    create.minImageCount = std::max(2u, capabilities.minImageCount + 1);
    if (capabilities.maxImageCount)
        create.minImageCount = std::min(create.minImageCount, capabilities.maxImageCount);
    create.imageFormat = chosen.format;
    create.imageColorSpace = chosen.colorSpace;
    create.imageExtent = nextExtent;
    create.imageArrayLayers = 1;
    create.imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        (capabilities.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    const std::array<std::uint32_t, 2> families{queueFamily, impl_->presentFamily};
    create.imageSharingMode = families[0] == families[1] ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    if (create.imageSharingMode == VK_SHARING_MODE_CONCURRENT)
    {
        create.queueFamilyIndexCount = 2;
        create.pQueueFamilyIndices = families.data();
    }
    create.preTransform = capabilities.currentTransform;
    create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(capabilities.supportedCompositeAlpha & create.compositeAlpha))
        for (auto alpha : {VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                           VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (capabilities.supportedCompositeAlpha & alpha)
            {
                create.compositeAlpha = alpha;
                break;
            }
    create.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    create.clipped = VK_TRUE;
    create.oldSwapchain = impl_->swapchain;
    VkSwapchainKHR replacement{};
    if (!check(vkCreateSwapchainKHR(device, &create, nullptr, &replacement), "vkCreateSwapchainKHR"))
    {
        impl_->fatal = true;
        return false;
    }
    // Passing oldSwapchain retires it. Any partial replacement failure is terminal
    // rather than allowing a later acquire against incomplete/retired resources.
    impl_->fatal = true;
    for (auto view : impl_->views)
        vkDestroyImageView(device, view, nullptr);
    for (auto semaphore : impl_->renderFinished)
        vkDestroySemaphore(device, semaphore, nullptr);
    if (impl_->swapchain)
        vkDestroySwapchainKHR(device, impl_->swapchain, nullptr);
    impl_->views.clear();
    impl_->renderFinished.clear();
    impl_->images.clear();
    impl_->layouts.clear();
    impl_->swapchain = replacement;
    extent = nextExtent;
    swapchainFormat = chosen.format;
    if (!check(vkGetSwapchainImagesKHR(device, replacement, &count, nullptr), "vkGetSwapchainImagesKHR"))
        return false;
    impl_->images.resize(count);
    if (!check(vkGetSwapchainImagesKHR(device, replacement, &count, impl_->images.data()), "vkGetSwapchainImagesKHR"))
        return false;
    impl_->views.resize(count);
    impl_->renderFinished.resize(count);
    impl_->layouts.assign(count, VK_IMAGE_LAYOUT_UNDEFINED);
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (std::uint32_t i = 0; i < count; ++i)
    {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = impl_->images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = chosen.format;
        view.subresourceRange = imageRange(VK_IMAGE_ASPECT_COLOR_BIT);
        if (!check(vkCreateImageView(device, &view, nullptr, &impl_->views[i]), "vkCreateImageView(swapchain)") ||
            !check(vkCreateSemaphore(device, &semaphore, nullptr, &impl_->renderFinished[i]),
                   "vkCreateSemaphore(present)"))
        {
            impl_->fatal = true;
            return false;
        }
    }
    impl_->rebuild = false;
    impl_->fatal = false;
    return true;
}

bool Context::beginFrame()
{
    if (!device || !impl_ || impl_->fatal)
        return false;
    if (impl_->active)
        return true;
    int width = 0, height = 0;
    SDL_Vulkan_GetDrawableSize(impl_->window, &width, &height);
    if (width <= 0 || height <= 0 || (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_MINIMIZED))
        return false;
    if (!impl_->swapchain || impl_->rebuild || extent.width != static_cast<std::uint32_t>(width) ||
        extent.height != static_cast<std::uint32_t>(height))
        if (!recreateSwapchain())
            return false;
    auto& frame = impl_->frames[impl_->frameIndex];
    // Serializing shared scene images is intentional; rings/pools still use two
    // contexts, and presentation semaphores belong to images independently.
    if (impl_->lastFrameFence &&
        !check(vkWaitForFences(device, 1, &impl_->lastFrameFence, VK_TRUE, forever), "vkWaitForFences(shared scene)"))
        return false;
    if (!check(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, forever), "vkWaitForFences(frame)"))
        return false;
    const auto acquire =
        vkAcquireNextImageKHR(device, impl_->swapchain, forever, frame.acquire, VK_NULL_HANDLE, &impl_->imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        impl_->rebuild = true;
        return false;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
    {
        check(acquire, "vkAcquireNextImageKHR");
        impl_->fatal = true;
        return false;
    }
    impl_->rebuild |= acquire == VK_SUBOPTIMAL_KHR;
    impl_->acquirePending = true;
    // The fence stays signaled until a real submission, including readback chunks.
    if (!check(vkResetCommandPool(device, frame.pool, 0), "vkResetCommandPool"))
    {
        impl_->fatal = true;
        return false;
    }
    for (auto pool : frame.descriptorPools)
        if (!check(vkResetDescriptorPool(device, pool, 0), "vkResetDescriptorPool"))
        {
            impl_->fatal = true;
            return false;
        }
    frame.offset = 0;
    for (auto& page : frame.overflowPages)
        page.offset = 0;
    frame.descriptorPoolIndex = 0;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer"))
    {
        impl_->fatal = true;
        return false;
    }
    impl_->active = true;
    transitionSwapchain(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    return true;
}

VkCommandBuffer Context::command() const
{
    if (!impl_)
        return VK_NULL_HANDLE;
    if (impl_->active)
        return impl_->frames[impl_->frameIndex].command;
    return impl_->transferActive ? impl_->transferCommand : VK_NULL_HANDLE;
}
VkImageView Context::swapchainView() const
{
    return impl_ && impl_->active ? impl_->views[impl_->imageIndex] : VK_NULL_HANDLE;
}
VkImage Context::swapchainImage() const
{
    return impl_ && impl_->active ? impl_->images[impl_->imageIndex] : VK_NULL_HANDLE;
}
std::uint32_t Context::imageCount() const
{
    return impl_ ? static_cast<std::uint32_t>(impl_->images.size()) : 0;
}
bool Context::isFatal() const
{
    return impl_ && impl_->fatal;
}

void Context::transition(Image& image, VkImageLayout layout, VkPipelineStageFlags2 stage, VkAccessFlags2 access)
{
    if (!command() || !image.handle)
    {
        error = "Image transition requires an image and active command recording";
        return;
    }
    const auto previous = impl_->states.find(image.handle);
    const auto source = previous != impl_->states.end() ? previous->second : Impl::State{};
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    // ALL_COMMANDS/MEMORY_* also covers uses recorded directly by the renderer
    // after transition; its final write is not observable through this wrapper.
    barrier.srcStageMask =
        image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = image.layout == VK_IMAGE_LAYOUT_UNDEFINED
                                ? 0
                                : (source.access | VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    barrier.dstStageMask = stage;
    barrier.dstAccessMask = access;
    barrier.oldLayout = image.layout;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.handle;
    barrier.subresourceRange = imageRange(image.aspect, image.mipLevels);
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command(), &dependency);
    image.layout = layout;
    impl_->states[image.handle] = {stage, access};
}

void Context::transitionSwapchain(VkImageLayout layout)
{
    auto& previous = impl_->layouts[impl_->imageIndex];
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask =
        previous == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask =
        previous == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ? VK_PIPELINE_STAGE_2_NONE
                                                                     : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccessMask = layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                ? 0
                                : VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = previous;
    barrier.newLayout = layout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = impl_->images[impl_->imageIndex];
    barrier.subresourceRange = imageRange(VK_IMAGE_ASPECT_COLOR_BIT);
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command(), &dependency);
    previous = layout;
}

Slice Context::upload(const void* data, VkDeviceSize size, VkDeviceSize alignment)
{
    if (!impl_ || impl_->fatal || (!impl_->active && !impl_->transferActive) || !data || !size)
    {
        error = "Ring upload requires data and active recording";
        return {};
    }
    if (size > impl_->maxBufferSize || size > std::numeric_limits<std::size_t>::max())
    {
        error = "Vulkan upload size exceeds the device or host address limit";
        return {};
    }
    auto& frame = impl_->frames[impl_->frameIndex];
    const auto& limits = impl_->properties.limits;
    alignment = std::max(alignment, VkDeviceSize(1));
    const auto deviceAlignment =
        std::max({VkDeviceSize(1), limits.minUniformBufferOffsetAlignment, limits.minStorageBufferOffsetAlignment,
                  limits.minTexelBufferOffsetAlignment});
    const auto factor = alignment / std::gcd(alignment, deviceAlignment);
    if (factor > std::numeric_limits<VkDeviceSize>::max() / deviceAlignment)
    {
        error = "Vulkan upload alignment overflow";
        return {};
    }
    alignment = factor * deviceAlignment;
    auto allocate = [&](Buffer& buffer, VkDeviceSize& cursor) -> Slice
    {
        if (cursor > buffer.size)
            return {};
        const auto remainder = cursor % alignment;
        const auto padding = remainder ? alignment - remainder : 0;
        if (padding > buffer.size - cursor)
            return {};
        const auto offset = cursor + padding;
        if (size > buffer.size - offset)
            return {};
        std::memcpy(static_cast<std::uint8_t*>(buffer.mapped) + offset, data, static_cast<std::size_t>(size));
        cursor = offset + size;
        return {buffer.handle, offset, size};
    };
    if (auto slice = allocate(frame.ring, frame.offset); slice.buffer)
        return slice;
    for (auto& page : frame.overflowPages)
        if (auto slice = allocate(page.buffer, page.offset); slice.buffer)
            return slice;
    // Overflow is allocated in large reusable pages, never one buffer per draw.
    auto buffer = createBuffer(std::max(ringSize, size), uploadUsage,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!buffer.handle)
        return {};
    frame.overflowPages.push_back({buffer, 0});
    auto& page = frame.overflowPages.back();
    return allocate(page.buffer, page.offset);
}

VkDescriptorSet Context::allocateDescriptor(VkDescriptorSetLayout layout)
{
    if (!impl_ || !impl_->active || !layout)
    {
        error = "Descriptor allocation requires a layout and active frame";
        return VK_NULL_HANDLE;
    }
    auto& frame = impl_->frames[impl_->frameIndex];
    for (;;)
    {
        if (frame.descriptorPoolIndex == frame.descriptorPools.size())
        {
            const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 2048},
                                                  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192},
                                                  {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8192},
                                                  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2048},
                                                  {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 2048},
                                                  {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 2048},
                                                  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192},
                                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8192},
                                                  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2048},
                                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 2048},
                                                  {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 2048}};
            VkDescriptorPoolCreateInfo create{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            create.maxSets = 4096;
            create.poolSizeCount = static_cast<std::uint32_t>(std::size(sizes));
            create.pPoolSizes = sizes;
            VkDescriptorPool pool{};
            if (!check(vkCreateDescriptorPool(device, &create, nullptr, &pool), "vkCreateDescriptorPool"))
                return VK_NULL_HANDLE;
            frame.descriptorPools.push_back(pool);
        }
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = frame.descriptorPools[frame.descriptorPoolIndex];
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &layout;
        VkDescriptorSet set{};
        const auto result = vkAllocateDescriptorSets(device, &allocate, &set);
        if (result == VK_SUCCESS)
            return set;
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
        {
            check(result, "vkAllocateDescriptorSets");
            return VK_NULL_HANDLE;
        }
        ++frame.descriptorPoolIndex;
        // No individual set may exceed one pool's advertised capacities.
        if (frame.descriptorPoolIndex > 64)
        {
            error = "Vulkan descriptor pools exhausted; a set may exceed per-pool capacity";
            return VK_NULL_HANDLE;
        }
    }
}

bool Context::beginTransfer()
{
    if (!device || !impl_ || impl_->fatal || impl_->active || impl_->transferActive)
        return false;
    if (impl_->lastFrameFence &&
        !check(vkWaitForFences(device, 1, &impl_->lastFrameFence, VK_TRUE, forever), "vkWaitForFences(transfer scene)"))
        return false;
    auto& frame = impl_->frames[impl_->frameIndex];
    if (!check(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, forever), "vkWaitForFences(transfer ring)") ||
        !check(vkWaitForFences(device, 1, &impl_->transferFence, VK_TRUE, forever), "vkWaitForFences(transfer)") ||
        !check(vkResetCommandPool(device, impl_->transferPool, 0), "vkResetCommandPool(transfer)"))
        return false;
    frame.offset = 0;
    for (auto& page : frame.overflowPages)
        page.offset = 0;
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!check(vkBeginCommandBuffer(impl_->transferCommand, &begin), "vkBeginCommandBuffer(transfer)"))
        return false;
    impl_->transferActive = true;
    return true;
}

bool Context::finishTransfer()
{
    if (!impl_->transferActive)
        return false;
    impl_->transferActive = false;
    if (!check(vkEndCommandBuffer(impl_->transferCommand), "vkEndCommandBuffer(transfer)") ||
        !check(vkResetFences(device, 1, &impl_->transferFence), "vkResetFences(transfer)"))
    {
        impl_->fatal = true;
        return false;
    }
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = impl_->transferCommand;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    if (!check(vkQueueSubmit2(queue, 1, &submit, impl_->transferFence), "vkQueueSubmit2(transfer)"))
    {
        impl_->fatal = true;
        return false;
    }
    if (!check(vkWaitForFences(device, 1, &impl_->transferFence, VK_TRUE, forever),
               "vkWaitForFences(transfer completion)"))
    {
        impl_->fatal = true;
        return false;
    }
    return true;
}

bool Context::uploadImage(Image& image, const void* data, VkDeviceSize size, std::uint32_t mipLevel)
{
    if (!device || !impl_ || impl_->fatal || !image.handle || !data || !size)
    {
        error = "Image upload requires an initialized context, image and pixel data";
        return false;
    }
    if (mipLevel >= image.mipLevels)
    {
        error = "Image upload mip level is out of range";
        return false;
    }
    if (impl_->rendering)
    {
        error = "Image upload requires ending dynamic rendering first";
        return false;
    }
    const bool standalone = !impl_->active;
    if (standalone && !beginTransfer())
        return false;
    auto slice = upload(data, size, 256);
    if (!slice.buffer)
    {
        if (standalone)
        {
            // No GPU commands reference staging yet; abandon this empty scope
            // without resetting a fence that has no corresponding submission.
            impl_->transferActive = false;
            if (!check(vkResetCommandPool(device, impl_->transferPool, 0), "vkResetCommandPool(abort upload)"))
                impl_->fatal = true;
        }
        return false;
    }
    transition(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy{};
    copy.bufferOffset = slice.offset;
    copy.imageSubresource = {image.aspect, mipLevel, 0, 1};
    copy.imageExtent = {std::max(1u, image.width >> mipLevel), std::max(1u, image.height >> mipLevel), 1};
    vkCmdCopyBufferToImage(command(), slice.buffer, image.handle, image.layout, 1, &copy);
    const auto target = (image.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                                                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    transition(image, target, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    return !standalone || finishTransfer();
}

bool Context::generateMipmaps(Image& image)
{
    if (!device || !impl_ || impl_->fatal || !image.handle || image.layout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        error = "Mip generation requires an initialized base image";
        return false;
    }
    if (impl_->rendering)
    {
        error = "Mip generation requires ending dynamic rendering first";
        return false;
    }
    if (image.mipLevels == 1)
        return true;
    constexpr VkImageUsageFlags requiredUsage =
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (image.aspect != VK_IMAGE_ASPECT_COLOR_BIT || (image.usage & requiredUsage) != requiredUsage)
    {
        error = "Mip generation requires a sampled color image with transfer source and destination usage";
        return false;
    }
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, image.format, &properties);
    constexpr VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & requiredFeatures) != requiredFeatures)
    {
        error = "Image format does not support linear filtered mip blits; upload explicit mip levels instead";
        return false;
    }
    const bool standalone = !impl_->active;
    if (standalone && !beginTransfer())
        return false;
    transition(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT);
    for (std::uint32_t level = 1; level < image.mipLevels; ++level)
    {
        // Temporarily make only the previous level a source. Restoring it to
        // DST after each blit keeps the image's aggregate layout consistent for
        // the final whole-image transition and all later upload/readback calls.
        VkImageMemoryBarrier2 source{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        source.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        source.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        source.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        source.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        source.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        source.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        source.srcQueueFamilyIndex = source.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        source.image = image.handle;
        source.subresourceRange = {image.aspect, level - 1, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &source;
        vkCmdPipelineBarrier2(command(), &dependency);
        VkImageBlit blit{};
        blit.srcSubresource = {image.aspect, level - 1, 0, 1};
        blit.srcOffsets[1] = {static_cast<std::int32_t>(std::max(1u, image.width >> (level - 1))),
                              static_cast<std::int32_t>(std::max(1u, image.height >> (level - 1))), 1};
        blit.dstSubresource = {image.aspect, level, 0, 1};
        blit.dstOffsets[1] = {static_cast<std::int32_t>(std::max(1u, image.width >> level)),
                              static_cast<std::int32_t>(std::max(1u, image.height >> level)), 1};
        vkCmdBlitImage(command(), image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.handle,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        source.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        source.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        source.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        source.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        source.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        source.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdPipelineBarrier2(command(), &dependency);
    }
    transition(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    return !standalone || finishTransfer();
}

bool Context::submitChunk(bool resume)
{
    auto& frame = impl_->frames[impl_->frameIndex];
    if (!check(vkEndCommandBuffer(frame.command), "vkEndCommandBuffer(readback)") ||
        !check(vkResetFences(device, 1, &frame.fence), "vkResetFences(readback)"))
        return false;
    VkSemaphoreSubmitInfo acquire{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    acquire.semaphore = frame.acquire;
    acquire.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = frame.command;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = impl_->acquirePending ? 1 : 0;
    submit.pWaitSemaphoreInfos = &acquire;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    if (!check(vkQueueSubmit2(queue, 1, &submit, frame.fence), "vkQueueSubmit2(readback)"))
    {
        impl_->fatal = true;
        return false;
    }
    impl_->acquirePending = false;
    impl_->lastFrameFence = frame.fence;
    if (!check(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, forever), "vkWaitForFences(readback)"))
        return false;
    if (resume)
    {
        if (!check(vkResetCommandBuffer(frame.command, 0), "vkResetCommandBuffer(readback)"))
            return false;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!check(vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer(readback resume)"))
            return false;
    }
    return true;
}

std::vector<std::uint8_t> Context::readImage(Image& image, std::uint32_t bytesPerPixel, std::uint32_t mipLevel)
{
    if (!device || !impl_ || impl_->fatal || !image.handle || !bytesPerPixel ||
        image.layout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        error = "Image readback requires initialized image contents and a nonzero pixel size";
        return {};
    }
    if (mipLevel >= image.mipLevels)
    {
        error = "Image readback mip level is out of range";
        return {};
    }
    if (impl_->rendering)
    {
        error = "Image readback requires ending dynamic rendering first";
        return {};
    }
    const auto mipWidth = std::max(1u, image.width >> mipLevel);
    const auto mipHeight = std::max(1u, image.height >> mipLevel);
    const VkDeviceSize pixelCount = VkDeviceSize(mipWidth) * mipHeight;
    if (pixelCount > std::numeric_limits<VkDeviceSize>::max() / bytesPerPixel)
    {
        error = "Image readback size overflow";
        return {};
    }
    const VkDeviceSize size = pixelCount * bytesPerPixel;
    if (size > std::numeric_limits<std::size_t>::max())
    {
        error = "Image readback size overflow";
        return {};
    }
    auto buffer = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!buffer.handle)
        return {};
    const bool standalone = !impl_->active;
    if (standalone && !beginTransfer())
    {
        destroy(buffer);
        return {};
    }
    const auto previousLayout = image.layout;
    const auto previousState = impl_->states[image.handle];
    transition(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
               VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {image.aspect, mipLevel, 0, 1};
    copy.imageExtent = {mipWidth, mipHeight, 1};
    vkCmdCopyImageToBuffer(command(), image.handle, image.layout, buffer.handle, 1, &copy);
    transition(image, previousLayout, previousState.stage, previousState.access);
    VkBufferMemoryBarrier2 hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    hostBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    hostBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    hostBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = buffer.handle;
    hostBarrier.size = VK_WHOLE_SIZE;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.bufferMemoryBarrierCount = 1;
    dependency.pBufferMemoryBarriers = &hostBarrier;
    vkCmdPipelineBarrier2(command(), &dependency);
    const bool complete = standalone ? finishTransfer() : submitChunk(true);
    std::vector<std::uint8_t> output;
    if (complete)
    {
        output.resize(static_cast<std::size_t>(size));
        std::memcpy(output.data(), buffer.mapped, output.size());
    }
    else
        impl_->fatal = true;
    // On failed submission no work references the buffer; device loss or a
    // failed wait is terminal and shutdown performs the conservative idle wait.
    if (!complete)
        vkDeviceWaitIdle(device);
    destroy(buffer);
    return output;
}

void Context::beginSwapchainRendering(bool clear)
{
    if (!impl_ || !impl_->active || impl_->rendering)
    {
        error = "Swapchain rendering requires an active frame and no open rendering scope";
        return;
    }
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = swapchainView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0, 0, 0, 1}};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(command(), &rendering);
    VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(command(), 0, 1, &viewport);
    vkCmdSetScissor(command(), 0, 1, &scissor);
    impl_->rendering = true;
}

void Context::endSwapchainRendering()
{
    if (impl_ && impl_->active && impl_->rendering)
    {
        vkCmdEndRendering(command());
        impl_->rendering = false;
        transitionSwapchain(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
}

void Context::present()
{
    if (!impl_ || !impl_->active || impl_->fatal)
        return;
    endSwapchainRendering();
    transitionSwapchain(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    auto& frame = impl_->frames[impl_->frameIndex];
    if (!check(vkEndCommandBuffer(frame.command), "vkEndCommandBuffer(present)") ||
        !check(vkResetFences(device, 1, &frame.fence), "vkResetFences(present)"))
    {
        impl_->fatal = true;
        impl_->active = false;
        return;
    }
    VkSemaphoreSubmitInfo acquire{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    acquire.semaphore = frame.acquire;
    acquire.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSemaphoreSubmitInfo finished{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    finished.semaphore = impl_->renderFinished[impl_->imageIndex];
    finished.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = frame.command;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = impl_->acquirePending ? 1 : 0;
    submit.pWaitSemaphoreInfos = &acquire;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &finished;
    impl_->active = false;
    if (!check(vkQueueSubmit2(queue, 1, &submit, frame.fence), "vkQueueSubmit2(present)"))
    {
        impl_->fatal = true;
        return;
    }
    impl_->acquirePending = false;
    impl_->lastFrameFence = frame.fence;
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &finished.semaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &impl_->swapchain;
    presentInfo.pImageIndices = &impl_->imageIndex;
    const auto result = vkQueuePresentKHR(impl_->presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        impl_->rebuild = true;
    else if (!check(result, "vkQueuePresentKHR"))
        impl_->fatal = true;
    impl_->frameIndex = (impl_->frameIndex + 1) % static_cast<std::uint32_t>(impl_->frames.size());
}

void Context::waitIdle()
{
    if (device)
        check(vkDeviceWaitIdle(device), "vkDeviceWaitIdle");
}

void Context::shutdown()
{
    if (!impl_)
        return;
    if (device)
    {
        vkDeviceWaitIdle(device);
        for (auto& frame : impl_->frames)
        {
            for (auto pool : frame.descriptorPools)
                vkDestroyDescriptorPool(device, pool, nullptr);
            for (auto& page : frame.overflowPages)
                destroy(page.buffer);
            destroy(frame.ring);
            if (frame.acquire)
                vkDestroySemaphore(device, frame.acquire, nullptr);
            if (frame.fence)
                vkDestroyFence(device, frame.fence, nullptr);
            if (frame.pool)
                vkDestroyCommandPool(device, frame.pool, nullptr);
        }
        for (auto view : impl_->views)
            if (view)
                vkDestroyImageView(device, view, nullptr);
        for (auto semaphore : impl_->renderFinished)
            if (semaphore)
                vkDestroySemaphore(device, semaphore, nullptr);
        if (impl_->swapchain)
            vkDestroySwapchainKHR(device, impl_->swapchain, nullptr);
        if (impl_->transferFence)
            vkDestroyFence(device, impl_->transferFence, nullptr);
        if (impl_->transferPool)
            vkDestroyCommandPool(device, impl_->transferPool, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    if (instance)
    {
        if (impl_->surface)
            vkDestroySurfaceKHR(instance, impl_->surface, nullptr);
        if (impl_->messenger)
        {
            auto destroyDebug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyDebug)
                destroyDebug(instance, impl_->messenger, nullptr);
        }
        vkDestroyInstance(instance, nullptr);
    }
    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;
    instance = VK_NULL_HANDLE;
    queueFamily = 0;
    extent = {};
    swapchainFormat = VK_FORMAT_UNDEFINED;
    impl_.reset();
}
} // namespace tinyengine::vk
