#pragma once

#include "movie_pass.hpp"
#include <windows.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace movie_pass {

class VulkanError : public std::runtime_error {
public:
    VulkanError(VkResult code, const char* message) : std::runtime_error(message), code(code) {}
    VkResult Code() const { return code; }

private:
    VkResult code;
};

inline void VkRequire(VkResult result, const char* operation) {
    if (result == VK_SUCCESS) return;
    char error[128]{};
    std::snprintf(error, sizeof(error), "%s returned %d", operation, static_cast<int>(result));
    throw VulkanError(result, error);
}

template <typename Info>
Info VkInfo(VkStructureType type) {
    Info info{};
    info.sType = type;
    return info;
}

#define MOVIE_INSTANCE_FUNCTIONS(F) \
    F(DestroyInstance) F(EnumeratePhysicalDevices) F(GetPhysicalDeviceProperties) \
    F(GetPhysicalDeviceQueueFamilyProperties) F(GetPhysicalDeviceMemoryProperties) F(CreateDevice) F(GetDeviceProcAddr)

#define MOVIE_DEVICE_FUNCTIONS(F) \
    F(DestroyDevice) F(GetDeviceQueue) F(CreateBuffer) F(DestroyBuffer) F(GetBufferMemoryRequirements) \
    F(CreateImage) F(DestroyImage) F(GetImageMemoryRequirements) F(AllocateMemory) F(FreeMemory) \
    F(BindImageMemory) F(BindBufferMemory) F(MapMemory) F(UnmapMemory) F(CreateImageView) F(DestroyImageView) \
    F(CreateSampler) F(DestroySampler) F(CreateDescriptorSetLayout) F(DestroyDescriptorSetLayout) \
    F(CreateDescriptorPool) F(DestroyDescriptorPool) F(AllocateDescriptorSets) F(UpdateDescriptorSets) \
    F(CreatePipelineLayout) F(DestroyPipelineLayout) F(CreateRenderPass) F(DestroyRenderPass) \
    F(CreateFramebuffer) F(DestroyFramebuffer) F(CreateShaderModule) F(DestroyShaderModule) \
    F(CreateGraphicsPipelines) F(DestroyPipeline) F(CreateCommandPool) F(DestroyCommandPool) \
    F(AllocateCommandBuffers) F(BeginCommandBuffer) F(EndCommandBuffer) F(CmdPipelineBarrier) \
    F(CmdCopyBufferToImage) F(CmdBeginRenderPass) F(CmdBindPipeline) F(CmdBindDescriptorSets) \
    F(CmdDraw) F(CmdEndRenderPass) F(CmdCopyImageToBuffer) F(CreateFence) F(DestroyFence) \
    F(QueueSubmit) F(WaitForFences) F(ResetFences) F(ResetCommandBuffer) F(DeviceWaitIdle)

class VulkanInstance {
public:
    void Create(bool windowPresentation = false, uint32_t apiVersion = VK_API_VERSION_1_0,
                bool* surfaceCaps2 = nullptr) {
        loader = LoadLibraryExW(L"vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        get = loader ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader, "vkGetInstanceProcAddr")) : nullptr;
        if (!get) throw std::runtime_error("Missing system Vulkan loader");
        const auto create = reinterpret_cast<PFN_vkCreateInstance>(get(nullptr, "vkCreateInstance"));
        if (!create) throw std::runtime_error("Missing vkCreateInstance");
        auto app = VkInfo<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        app.pApplicationName = "Unlimited native movie render test";
        app.apiVersion = apiVersion;
        auto description = VkInfo<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        description.pApplicationInfo = &app;
        const char* win32Extensions[]{VK_KHR_SURFACE_EXTENSION_NAME, "VK_KHR_win32_surface",
                                      VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME};
        if (windowPresentation) {
            description.enabledExtensionCount = 2;
            description.ppEnabledExtensionNames = win32Extensions;
            if (surfaceCaps2) {
                const auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
                    get(nullptr,"vkEnumerateInstanceExtensionProperties"));
                uint32_t count = 0;
                *surfaceCaps2 = false;
                if (enumerate && enumerate(nullptr,&count,nullptr)==VK_SUCCESS) {
                    std::vector<VkExtensionProperties> available(count);
                    if (count && enumerate(nullptr,&count,available.data())==VK_SUCCESS)
                        for (const auto& extension : available)
                            if (!std::strcmp(extension.extensionName,win32Extensions[2])) *surfaceCaps2=true;
                }
                if (*surfaceCaps2) ++description.enabledExtensionCount;
            }
        }
        VkRequire(create(&description, nullptr, &instance), "vkCreateInstance");
#define LOAD_INSTANCE(name) name = reinterpret_cast<PFN_vk##name>(get(instance, "vk" #name)); \
    if (!name) throw std::runtime_error("Missing vk" #name);
        MOVIE_INSTANCE_FUNCTIONS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    }

    ~VulkanInstance() {
        if (DestroyInstance) DestroyInstance(instance, nullptr);
        if (loader) FreeLibrary(loader);
    }

    VkInstance instance = VK_NULL_HANDLE;
    template <typename T>
    T Proc(const char* name) const {
        const auto function = get(instance, name);
        if (!function) throw std::runtime_error(name);
        return reinterpret_cast<T>(function);
    }
#define DECLARE(name) PFN_vk##name name = nullptr;
    MOVIE_INSTANCE_FUNCTIONS(DECLARE)
#undef DECLARE
private:
    HMODULE loader = nullptr;
    PFN_vkGetInstanceProcAddr get = nullptr;
};

struct VulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct VulkanImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

class VulkanDevice {
public:
    void Create(VulkanInstance& instance, VkPhysicalDevice gpu, VkSurfaceKHR surface = VK_NULL_HANDLE,
                const std::vector<const char*>& extensions = {}, const void* deviceFeatures = nullptr) {
        std::uint32_t count = 0;
        instance.GetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
        std::vector<VkQueueFamilyProperties> properties(count);
        instance.GetPhysicalDeviceQueueFamilyProperties(gpu, &count, properties.data());
        const auto supportsSurface = surface ?
            instance.Proc<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>("vkGetPhysicalDeviceSurfaceSupportKHR") : nullptr;
        for (; family < count; ++family) {
            if (!(properties[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !properties[family].queueCount) continue;
            if (!supportsSurface) break;
            VkBool32 supported = VK_FALSE;
            VkRequire(supportsSurface(gpu, family, surface, &supported), "Check Vulkan presentation support");
            if (supported) break;
        }
        if (family == count) throw std::runtime_error("No Vulkan graphics queue");
        const float priority = 1;
        auto queueInfo = VkInfo<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        auto description = VkInfo<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
        description.queueCreateInfoCount = 1;
        description.pQueueCreateInfos = &queueInfo;
        auto enabled = extensions;
        if (surface) enabled.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        description.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
        description.ppEnabledExtensionNames = enabled.data();
        description.pNext = deviceFeatures;
        VkRequire(instance.CreateDevice(gpu, &description, nullptr, &device), "vkCreateDevice");
#define LOAD_DEVICE(name) name = reinterpret_cast<PFN_vk##name>(instance.GetDeviceProcAddr(device, "vk" #name)); \
    if (!name) throw std::runtime_error("Missing vk" #name);
        MOVIE_DEVICE_FUNCTIONS(LOAD_DEVICE)
#undef LOAD_DEVICE
        instance.GetPhysicalDeviceMemoryProperties(gpu, &memoryProperties);
        GetDeviceQueue(device, family, 0, &queue);
    }

    ~VulkanDevice() {
        if (DestroyDevice) DestroyDevice(device, nullptr);
    }

    void Allocate(VkMemoryRequirements required, VkMemoryPropertyFlags flags, VkDeviceMemory* memory) {
        for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
            const auto bits = memoryProperties.memoryTypes[index].propertyFlags;
            if (!(required.memoryTypeBits & (1u << index)) || (bits & flags) != flags) continue;
            auto desc = VkInfo<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            desc.allocationSize = required.size;
            desc.memoryTypeIndex = index;
            VkRequire(AllocateMemory(device, &desc, nullptr, memory), "vkAllocateMemory");
            return;
        }
        throw std::runtime_error("No suitable Vulkan memory type");
    }

    void MakeBuffer(VulkanBuffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage) {
        auto desc = VkInfo<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        desc.size = size;
        desc.usage = usage;
        desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkRequire(CreateBuffer(device, &desc, nullptr, &buffer.buffer), "vkCreateBuffer");
        VkMemoryRequirements requirements{};
        GetBufferMemoryRequirements(device, buffer.buffer, &requirements);
        Allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.memory);
        VkRequire(BindBufferMemory(device, buffer.buffer, buffer.memory, 0), "vkBindBufferMemory");
    }

    void MakeImage(VulkanImage& image, Extent size, VkFormat format, VkImageUsageFlags usage) {
        auto desc = VkInfo<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        desc.imageType = VK_IMAGE_TYPE_2D;
        desc.format = format;
        desc.extent = {size.width, size.height, 1};
        desc.mipLevels = desc.arrayLayers = 1;
        desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.tiling = VK_IMAGE_TILING_OPTIMAL;
        desc.usage = usage;
        desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkRequire(CreateImage(device, &desc, nullptr, &image.image), "vkCreateImage");
        VkMemoryRequirements requirements{};
        GetImageMemoryRequirements(device, image.image, &requirements);
        Allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &image.memory);
        VkRequire(BindImageMemory(device, image.image, image.memory, 0), "vkBindImageMemory");
        auto view = VkInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        view.image = image.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkRequire(CreateImageView(device, &view, nullptr, &image.view), "vkCreateImageView");
    }

    void Destroy(VulkanBuffer buffer) {
        DestroyBuffer(device, buffer.buffer, nullptr);
        FreeMemory(device, buffer.memory, nullptr);
    }

    void Destroy(VulkanImage image) {
        DestroyImageView(device, image.view, nullptr);
        DestroyImage(device, image.image, nullptr);
        FreeMemory(device, image.memory, nullptr);
    }

    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;
#define DECLARE(name) PFN_vk##name name = nullptr;
    MOVIE_DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
private:
    VkPhysicalDeviceMemoryProperties memoryProperties{};
};

#undef MOVIE_INSTANCE_FUNCTIONS
#undef MOVIE_DEVICE_FUNCTIONS

class VulkanMoviePass {
public:
    explicit VulkanMoviePass(VulkanDevice& backend) : vk(backend) {}
    ~VulkanMoviePass() {
        if (inFlight) vk.DeviceWaitIdle(vk.device);
        vk.DestroyFence(vk.device, fence, nullptr);
        vk.DestroyCommandPool(vk.device, commands, nullptr);
        vk.DestroyPipeline(vk.device, pipeline, nullptr);
        vk.DestroyShaderModule(vk.device, fragment, nullptr);
        vk.DestroyShaderModule(vk.device, vertex, nullptr);
        vk.DestroyFramebuffer(vk.device, framebuffer, nullptr);
        vk.DestroyRenderPass(vk.device, renderPass, nullptr);
        vk.DestroyPipelineLayout(vk.device, pipelineLayout, nullptr);
        vk.DestroyDescriptorPool(vk.device, descriptorPool, nullptr);
        vk.DestroyDescriptorSetLayout(vk.device, descriptorLayout, nullptr);
        vk.DestroySampler(vk.device, sampler, nullptr);
        if (borrowedColor) vk.DestroyImageView(vk.device, color.view, nullptr);
        else vk.Destroy(color);
        vk.Destroy(input);
        vk.Destroy(readback);
        vk.Destroy(upload);
    }

    Pixels Draw(Frame frame, Extent target, const std::vector<std::uint32_t>& vertexCode,
                const std::vector<std::uint32_t>& fragmentCode) {
        if (commands) throw std::logic_error("The Vulkan offscreen pass records a single draw");
        const auto downloadSize = OutputSize(frame, target);
        const auto uploadSize = frame.rowBytes * frame.size.height;
        vk.MakeBuffer(upload, uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        vk.MakeBuffer(readback, downloadSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        vk.MakeImage(input, frame.size, VK_FORMAT_B8G8R8A8_UNORM,
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        vk.MakeImage(color, target, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        void* mapped = nullptr;
        VkRequire(vk.MapMemory(vk.device, upload.memory, 0, uploadSize, 0, &mapped), "Map Vulkan upload");
        std::memcpy(mapped, frame.bgra, uploadSize);
        vk.UnmapMemory(vk.device, upload.memory);
        MakeDescriptors();
        MakeRenderPass(target, VK_FORMAT_R8G8B8A8_UNORM);
        MakeShader(vertexCode, &vertex);
        MakeShader(fragmentCode, &fragment);
        MakePipeline(Fit(frame.size, target));
        Record(frame.size, target, static_cast<std::uint32_t>(frame.rowBytes / 4), false, true);
        Submit(VK_NULL_HANDLE, VK_NULL_HANDLE);
        return Capture(target);
    }

    void DrawToSwapImage(Frame frame, Extent target, VkImage output, VkFormat format,
                        VkSemaphore acquired, VkSemaphore rendered, bool capture,
                         const std::vector<std::uint32_t>& vertexCode,
                         const std::vector<std::uint32_t>& fragmentCode) {
        const auto downloadSize = OutputSize(frame, target);
        const std::size_t rowSize = std::size_t(frame.size.width) * 4;
        const auto uploadSize = rowSize * frame.size.height;
        if (commands && (!borrowedColor || color.image != output || frame.size.width != configuredSource.width ||
                         frame.size.height != configuredSource.height || target.width != configuredTarget.width ||
                         target.height != configuredTarget.height || format != targetFormat)) {
            throw std::invalid_argument("Vulkan movie pass needs recreation after a swap-image or frame-size change");
        }
        Wait();
        if (!commands) {
            vk.MakeBuffer(upload, uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            vk.MakeImage(input, frame.size, VK_FORMAT_B8G8R8A8_UNORM,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            borrowedColor = true;
            color.image = output;
            auto view = VkInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            view.image = output;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkRequire(vk.CreateImageView(vk.device, &view, nullptr, &color.view), "Create Vulkan swap image view");
            MakeDescriptors();
            MakeRenderPass(target, format);
            MakeShader(vertexCode, &vertex);
            MakeShader(fragmentCode, &fragment);
            MakePipeline(Fit(frame.size, target));
            configuredSource = frame.size;
            configuredTarget = target;
            targetFormat = format;
        }
        if (capture && !readback.buffer) {
            vk.MakeBuffer(readback, downloadSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        }
        void* mapped = nullptr;
        VkRequire(vk.MapMemory(vk.device, upload.memory, 0, uploadSize, 0, &mapped), "Map Vulkan movie upload");
        for (unsigned row = 0; row < frame.size.height; ++row) {
            std::memcpy(static_cast<std::uint8_t*>(mapped) + row * rowSize, frame.bgra + row * frame.rowBytes, rowSize);
        }
        vk.UnmapMemory(vk.device, upload.memory);
        Record(frame.size, target, frame.size.width, true, capture);
        Submit(acquired, rendered);
    }

    void Wait() {
        if (!inFlight) return;
        VkRequire(vk.WaitForFences(vk.device, 1, &fence, VK_TRUE, 5000000000ull), "vkWaitForFences");
        inFlight = false;
    }

    Pixels Capture(Extent target) {
        Wait();
        if (!readback.buffer) throw std::logic_error("No Vulkan frame readback was requested");
        const auto downloadSize = std::size_t(target.width) * target.height * 4;
        Pixels result(downloadSize);
        void* mapped = nullptr;
        VkRequire(vk.MapMemory(vk.device, readback.memory, 0, downloadSize, 0, &mapped), "Map Vulkan readback");
        std::memcpy(result.data(), mapped, downloadSize);
        vk.UnmapMemory(vk.device, readback.memory);
        return result;
    }

private:
    void Submit(VkSemaphore acquired, VkSemaphore rendered) {
        if (fence) {
            VkRequire(vk.ResetFences(vk.device, 1, &fence), "Reset Vulkan movie fence");
        } else {
            auto fenceInfo = VkInfo<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
            VkRequire(vk.CreateFence(vk.device, &fenceInfo, nullptr, &fence), "vkCreateFence");
        }
        auto submission = VkInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submission.commandBufferCount = 1;
        submission.pCommandBuffers = &recording;
        constexpr VkPipelineStageFlags beforeColor = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (acquired) {
            submission.waitSemaphoreCount = submission.signalSemaphoreCount = 1;
            submission.pWaitSemaphores = &acquired;
            submission.pWaitDstStageMask = &beforeColor;
            submission.pSignalSemaphores = &rendered;
        }
        VkRequire(vk.QueueSubmit(vk.queue, 1, &submission, fence), "vkQueueSubmit");
        inFlight = true;
    }

    void MakeDescriptors() {
        auto samplerInfo = VkInfo<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
        samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VkRequire(vk.CreateSampler(vk.device, &samplerInfo, nullptr, &sampler), "vkCreateSampler");
        const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                   1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        auto setInfo = VkInfo<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        setInfo.bindingCount = 1;
        setInfo.pBindings = &binding;
        VkRequire(vk.CreateDescriptorSetLayout(vk.device, &setInfo, nullptr, &descriptorLayout), "vkCreateDescriptorSetLayout");
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        auto poolInfo = VkInfo<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        poolInfo.maxSets = poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        VkRequire(vk.CreateDescriptorPool(vk.device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");
        auto allocate = VkInfo<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        allocate.descriptorPool = descriptorPool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &descriptorLayout;
        VkRequire(vk.AllocateDescriptorSets(vk.device, &allocate, &descriptor), "vkAllocateDescriptorSets");
        const VkDescriptorImageInfo view{sampler, input.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        auto update = VkInfo<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        update.dstSet = descriptor;
        update.descriptorCount = 1;
        update.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        update.pImageInfo = &view;
        vk.UpdateDescriptorSets(vk.device, 1, &update, 0, nullptr);
        auto layout = VkInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &descriptorLayout;
        VkRequire(vk.CreatePipelineLayout(vk.device, &layout, nullptr, &pipelineLayout), "vkCreatePipelineLayout");
    }

    void MakeRenderPass(Extent target, VkFormat format) {
        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        const VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        auto pass = VkInfo<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
        pass.attachmentCount = pass.subpassCount = 1;
        pass.pAttachments = &attachment;
        pass.pSubpasses = &subpass;
        VkRequire(vk.CreateRenderPass(vk.device, &pass, nullptr, &renderPass), "vkCreateRenderPass");
        auto fb = VkInfo<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
        fb.renderPass = renderPass;
        fb.attachmentCount = fb.layers = 1;
        fb.pAttachments = &color.view;
        fb.width = target.width;
        fb.height = target.height;
        VkRequire(vk.CreateFramebuffer(vk.device, &fb, nullptr, &framebuffer), "vkCreateFramebuffer");
    }

    void MakeShader(const std::vector<std::uint32_t>& words, VkShaderModule* shader) {
        auto code = VkInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        code.codeSize = words.size() * sizeof(std::uint32_t);
        code.pCode = words.data();
        VkRequire(vk.CreateShaderModule(vk.device, &code, nullptr, shader), "vkCreateShaderModule");
    }

    void MakePipeline(Rect rect) {
        auto vs = VkInfo<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        vs.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vs.module = vertex;
        vs.pName = "main";
        auto ps = vs;
        ps.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        ps.module = fragment;
        const VkPipelineShaderStageCreateInfo stages[]{vs, ps};
        const auto vertices = VkInfo<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
        auto topology = VkInfo<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        topology.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        const VkViewport viewport{float(rect.left), float(rect.top), float(rect.size.width), float(rect.size.height), 0, 1};
        const VkRect2D scissor{{int(rect.left), int(rect.top)}, {rect.size.width, rect.size.height}};
        auto window = VkInfo<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        window.viewportCount = window.scissorCount = 1;
        window.pViewports = &viewport;
        window.pScissors = &scissor;
        auto raster = VkInfo<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1.0f;
        auto samples = VkInfo<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState write{};
        write.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        auto blend = VkInfo<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        blend.attachmentCount = 1;
        blend.pAttachments = &write;
        auto state = VkInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        state.stageCount = 2;
        state.pStages = stages;
        state.pVertexInputState = &vertices;
        state.pInputAssemblyState = &topology;
        state.pViewportState = &window;
        state.pRasterizationState = &raster;
        state.pMultisampleState = &samples;
        state.pColorBlendState = &blend;
        state.layout = pipelineLayout;
        state.renderPass = renderPass;
        VkRequire(vk.CreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &state, nullptr, &pipeline), "vkCreateGraphicsPipelines");
    }

    void Transition(VkImage image, VkImageLayout from, VkImageLayout to,
                    VkPipelineStageFlags before, VkPipelineStageFlags after,
                    VkAccessFlags writes, VkAccessFlags reads) {
        auto barrier = VkInfo<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.srcAccessMask = writes;
        barrier.dstAccessMask = reads;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk.CmdPipelineBarrier(recording, before, after, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void Record(Extent source, Extent target, std::uint32_t rowTexels, bool presentation, bool capture) {
        if (!commands) {
            auto pool = VkInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
            pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool.queueFamilyIndex = vk.family;
            VkRequire(vk.CreateCommandPool(vk.device, &pool, nullptr, &commands), "vkCreateCommandPool");
            auto allocate = VkInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
            allocate.commandPool = commands;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;
            VkRequire(vk.AllocateCommandBuffers(vk.device, &allocate, &recording), "vkAllocateCommandBuffers");
        } else {
            VkRequire(vk.ResetCommandBuffer(recording, 0), "Reset Vulkan movie commands");
        }
        auto begin = VkInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkRequire(vk.BeginCommandBuffer(recording, &begin), "vkBeginCommandBuffer");
        Transition(input.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy uploaded{};
        uploaded.bufferRowLength = rowTexels;
        uploaded.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        uploaded.imageExtent = {source.width, source.height, 1};
        vk.CmdCopyBufferToImage(recording, upload.buffer, input.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &uploaded);
        Transition(input.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        // Keep the swap image transition in the acquire semaphore's wait scope.
        Transition(color.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        VkClearValue black{};
        black.color.float32[3] = 1.0f;
        auto pass = VkInfo<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
        pass.renderPass = renderPass;
        pass.framebuffer = framebuffer;
        pass.renderArea.extent = {target.width, target.height};
        pass.clearValueCount = 1;
        pass.pClearValues = &black;
        vk.CmdBeginRenderPass(recording, &pass, VK_SUBPASS_CONTENTS_INLINE);
        vk.CmdBindPipeline(recording, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vk.CmdBindDescriptorSets(recording, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptor, 0, nullptr);
        vk.CmdDraw(recording, 3, 1, 0, 0);
        vk.CmdEndRenderPass(recording);
        if (capture) RecordCapture(target, uploaded.imageSubresource);
        if (presentation) {
            Transition(color.image, capture ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                       capture ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       capture ? VK_ACCESS_TRANSFER_READ_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);
        }
        VkRequire(vk.EndCommandBuffer(recording), "vkEndCommandBuffer");
    }

    void RecordCapture(Extent target, VkImageSubresourceLayers subresource) {
        Transition(color.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy download{};
        download.imageSubresource = subresource;
        download.imageExtent = {target.width, target.height, 1};
        vk.CmdCopyImageToBuffer(recording, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &download);
        auto sync = VkInfo<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        sync.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sync.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vk.CmdPipelineBarrier(recording, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                              0, 1, &sync, 0, nullptr, 0, nullptr);
    }

    VulkanDevice& vk;
    VulkanBuffer upload{}, readback{};
    VulkanImage input{}, color{};
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool commands = VK_NULL_HANDLE;
    VkCommandBuffer recording = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    bool inFlight = false;
    bool borrowedColor = false;
    Extent configuredSource{}, configuredTarget{};
    VkFormat targetFormat = VK_FORMAT_UNDEFINED;
};

} // namespace movie_pass
