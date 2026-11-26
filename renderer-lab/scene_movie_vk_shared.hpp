#pragma once

#include "movie_d3d.hpp"
#include "movie_vulkan.hpp"
#include <vulkan/vulkan_win32.h>

namespace scene_movie {

// Shared handles always refer to committed resources on the same adapter.
// Closing an NT handle after import leaves each API owning its resource.
class VkSharedContext {
    struct Handle {
        HANDLE value{};
        ~Handle() { if (value) CloseHandle(value); }
    };
public:
    movie_pass::VulkanInstance instance;
    struct Win32Surface {
        VkInstance instance{};
        VkSurfaceKHR handle{};
        PFN_vkDestroySurfaceKHR destroy{};
        ~Win32Surface() { if (handle) destroy(instance,handle,nullptr); }
    } surface;
    movie_pass::VulkanDevice vk;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    bool samplerAnisotropy = false;
    bool exclusivePresentation = false;
    float maxAnisotropy = 1;

    explicit VkSharedContext(ID3D12Device* device, HWND window=nullptr) : owner(device) {
        using namespace movie_pass;
        bool surfaceCaps2=false;
        instance.Create(window!=nullptr,VK_API_VERSION_1_2,window?&surfaceCaps2:nullptr);
        if (window) {
            surface.instance=instance.instance;
            surface.destroy=instance.Proc<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR");
            auto info=VkInfo<VkWin32SurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
            info.hwnd=window;
            info.hinstance=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window,GWLP_HINSTANCE));
            VkRequire(instance.Proc<PFN_vkCreateWin32SurfaceKHR>("vkCreateWin32SurfaceKHR")(instance.instance,&info,nullptr,&surface.handle),"Create Win32 Vulkan frame surface");
        }
        const auto properties2 = instance.Proc<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2");
        const LUID luid = owner->GetAdapterLuid();
        uint32_t count = 0;
        VkRequire(instance.EnumeratePhysicalDevices(instance.instance,&count,nullptr),"Count Vulkan adapters");
        std::vector<VkPhysicalDevice> adapters(count);
        if (count) VkRequire(instance.EnumeratePhysicalDevices(instance.instance,&count,adapters.data()),"List Vulkan adapters");
        for (auto adapter : adapters) {
            auto id = VkInfo<VkPhysicalDeviceIDProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
            auto properties = VkInfo<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
            properties.pNext = &id;
            properties2(adapter,&properties);
            if (id.deviceLUIDValid && (id.deviceNodeMask & 1u) && !std::memcmp(id.deviceLUID,&luid,sizeof(luid)) &&
                properties.properties.apiVersion >= VK_API_VERSION_1_2) {
                physical = adapter;
                break;
            }
        }
        if (!physical) throw std::runtime_error("No Vulkan 1.2 adapter matches the D3D12 device");
        auto timeline = VkInfo<VkPhysicalDeviceTimelineSemaphoreFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
        auto features = VkInfo<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        features.pNext = &timeline;
        instance.Proc<PFN_vkGetPhysicalDeviceFeatures2>("vkGetPhysicalDeviceFeatures2")(physical,&features);
        if (!timeline.timelineSemaphore) throw std::runtime_error("Vulkan timeline semaphores unavailable");
        samplerAnisotropy = features.features.samplerAnisotropy == VK_TRUE;
        features.features = {};
        features.features.samplerAnisotropy = samplerAnisotropy;
        VkPhysicalDeviceProperties props{};
        instance.GetPhysicalDeviceProperties(physical,&props);
        maxAnisotropy=props.limits.maxSamplerAnisotropy;
        std::vector<const char*> extensions{VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
                                           VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};
        if (surfaceCaps2) {
            const auto enumerate=instance.Proc<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
            uint32_t available=0;
            if(enumerate(physical,nullptr,&available,nullptr)==VK_SUCCESS && available) {
                std::vector<VkExtensionProperties> properties(available);
                if(enumerate(physical,nullptr,&available,properties.data())==VK_SUCCESS)
                    for(const auto& extension:properties)
                        if(!std::strcmp(extension.extensionName,VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME)) {
                            exclusivePresentation=true;
                            extensions.push_back(VK_EXT_FULL_SCREEN_EXCLUSIVE_EXTENSION_NAME);
                            break;
                        }
            }
        }
        vk.Create(instance,physical,surface.handle,extensions,&features);
        memoryProperties = Proc<PFN_vkGetMemoryWin32HandlePropertiesKHR>("vkGetMemoryWin32HandlePropertiesKHR");
        importSemaphore = Proc<PFN_vkImportSemaphoreWin32HandleKHR>("vkImportSemaphoreWin32HandleKHR");
        createSemaphore = Proc<PFN_vkCreateSemaphore>("vkCreateSemaphore");
        destroySemaphore = Proc<PFN_vkDestroySemaphore>("vkDestroySemaphore");
    }

    template<class T> T Proc(const char* name) const {
        const auto p = instance.GetDeviceProcAddr(vk.device,name);
        if (!p) throw std::runtime_error(name);
        return reinterpret_cast<T>(p);
    }

    movie_pass::VulkanImage ImportImage(ID3D12Resource* resource, VkFormat format,
                         VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) const {
        using namespace movie_pass;
        const auto desc = resource->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.MipLevels != 1 || desc.DepthOrArraySize != 1)
            throw std::runtime_error("Expected a single D3D12 texture subresource");
        Handle handle;
        Require(owner->CreateSharedHandle(resource,nullptr,GENERIC_ALL,nullptr,&handle.value),"Export D3D12 movie texture");
        constexpr auto type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        auto external = VkInfo<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
        external.handleTypes = type;
        auto image = VkInfo<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        image.pNext = &external;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = format;
        image.extent = {static_cast<uint32_t>(desc.Width),desc.Height,1};
        image.mipLevels = image.arrayLayers = 1;
        image.samples = static_cast<VkSampleCountFlagBits>(desc.SampleDesc.Count);
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = usage;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VulkanImage result;
        try {
            VkRequire(vk.CreateImage(vk.device,&image,nullptr,&result.image),"Create Vulkan movie texture");
            auto imported = VkInfo<VkMemoryWin32HandlePropertiesKHR>(VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR);
            VkRequire(memoryProperties(vk.device,type,handle.value,&imported),"Query shared D3D12 texture memory");
            VkMemoryRequirements required{};
            vk.GetImageMemoryRequirements(vk.device,result.image,&required);
            const uint32_t bits = required.memoryTypeBits & imported.memoryTypeBits;
            if (!bits) throw std::runtime_error("Incompatible D3D12 texture memory type");
            uint32_t index = 0;
            while (!(bits & (1u << index))) ++index;
            auto dedicated = VkInfo<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
            dedicated.image = result.image;
            auto import = VkInfo<VkImportMemoryWin32HandleInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR);
            import.pNext = &dedicated;
            import.handleType = type;
            import.handle = handle.value;
            auto alloc = VkInfo<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            alloc.pNext = &import;
            alloc.allocationSize = required.size;
            alloc.memoryTypeIndex = index;
            VkRequire(vk.AllocateMemory(vk.device,&alloc,nullptr,&result.memory),"Import D3D12 texture into Vulkan");
            VkRequire(vk.BindImageMemory(vk.device,result.image,result.memory,0),"Bind shared D3D12 movie texture");
            auto view = VkInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            view.image = result.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            VkRequire(vk.CreateImageView(vk.device,&view,nullptr,&result.view),"Create Vulkan shared movie view");
            return result;
        } catch (...) {
            if (result.view) vk.DestroyImageView(vk.device,result.view,nullptr);
            if (result.image) vk.DestroyImage(vk.device,result.image,nullptr);
            if (result.memory) vk.FreeMemory(vk.device,result.memory,nullptr);
            throw;
        }
    }

    VkSemaphore ImportFence(ID3D12Fence* fence) const {
        using namespace movie_pass;
        Handle handle;
        Require(owner->CreateSharedHandle(fence,nullptr,GENERIC_ALL,nullptr,&handle.value),"Export D3D12 movie fence");
        auto timeline = VkInfo<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
        timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        auto info = VkInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        info.pNext = &timeline;
        VkSemaphore result = VK_NULL_HANDLE;
        VkRequire(createSemaphore(vk.device,&info,nullptr,&result),"Create shared Vulkan timeline");
        auto import = VkInfo<VkImportSemaphoreWin32HandleInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR);
        import.semaphore = result;
        import.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT;
        import.handle = handle.value;
        const auto status = importSemaphore(vk.device,&import);
        if (status != VK_SUCCESS) {
            DestroySemaphore(result);
            VkRequire(status,"Import D3D12 movie fence");
        }
        return result;
    }
    void DestroySemaphore(VkSemaphore semaphore) const {
        if (semaphore) destroySemaphore(vk.device,semaphore,nullptr);
    }
    void WaitIdle() const {
        movie_pass::VkRequire(vk.DeviceWaitIdle(vk.device),"Wait for outstanding Vulkan movie work");
    }
    void Submit(VkCommandBuffer cmd,VkSemaphore wait,UINT64 waitValue,VkSemaphore signal,UINT64 signalValue,
                VkFence complete = VK_NULL_HANDLE) const {
        using namespace movie_pass;
        auto timeline = VkInfo<VkTimelineSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO);
        timeline.waitSemaphoreValueCount = wait ? 1 : 0;
        timeline.pWaitSemaphoreValues = &waitValue;
        timeline.signalSemaphoreValueCount = signal ? 1 : 0;
        timeline.pSignalSemaphoreValues = &signalValue;
        auto submit = VkInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.pNext = &timeline;
        submit.commandBufferCount = cmd ? 1 : 0;
        submit.pCommandBuffers = &cmd;
        const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        submit.waitSemaphoreCount = timeline.waitSemaphoreValueCount;
        submit.pWaitSemaphores = &wait;
        submit.pWaitDstStageMask = &stages;
        submit.signalSemaphoreCount = timeline.signalSemaphoreValueCount;
        submit.pSignalSemaphores = &signal;
        VkRequire(vk.QueueSubmit(vk.queue,1,&submit,complete),"Submit shared Vulkan movie layer");
    }
private:
    movie_pass::Com<ID3D12Device> owner;
    PFN_vkGetMemoryWin32HandlePropertiesKHR memoryProperties{};
    PFN_vkImportSemaphoreWin32HandleKHR importSemaphore{};
    PFN_vkCreateSemaphore createSemaphore{};
    PFN_vkDestroySemaphore destroySemaphore{};
};
}
