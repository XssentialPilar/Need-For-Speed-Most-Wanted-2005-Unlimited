#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include "movie_vulkan.hpp"
#include "movie_presentation.hpp"

#include <algorithm>
#include <memory>

namespace movie_pass {

class VulkanMoviePresenter {
public:
    VulkanMoviePresenter(HWND window, VulkanInstance& instance, VkPhysicalDevice physical,
                         const std::vector<std::uint32_t>& vertex, const std::vector<std::uint32_t>& pixel)
        : window(window), instance(instance), physical(physical), vertex(vertex), pixel(pixel) {
        if (!window || !physical || vertex.empty() || pixel.empty()) {
            throw std::invalid_argument("Missing Vulkan presentation input");
        }
    }

    VulkanMoviePresenter(const VulkanMoviePresenter&) = delete;
    VulkanMoviePresenter& operator=(const VulkanMoviePresenter&) = delete;
    ~VulkanMoviePresenter() { ResetDevice(); }

    MoviePresentation Submit(Frame frame, bool vsync, Pixels* capture = nullptr) {
        const auto client = ClientExtent(window);
        if (!client.width || !client.height) return MoviePresentation::Suspended;
        OutputSize(frame, client);
        try {
            if (!device) Initialize();
            if (!swapChain || vsync != previousVsync || lastClient.width != client.width || lastClient.height != client.height) {
                RecreateSwapChain(client, vsync);
            }
            if (capture && !captureSupported) throw std::runtime_error("Vulkan swap chain does not support pixel readback");
            // The last render fence covers consumption of the acquire semaphore.
            // Render-finished semaphores stay bound to the individual images.
            if (lastPass) lastPass->Wait();
            std::uint32_t imageIndex = 0;
            const auto acquireStatus = acquire(device->device, swapChain, 1000000000ull,
                                               available, VK_NULL_HANDLE, &imageIndex);
            if (acquireStatus == VK_TIMEOUT || acquireStatus == VK_NOT_READY) return MoviePresentation::Occluded;
            const bool suboptimal = acquireStatus == VK_SUBOPTIMAL_KHR;
            if (!suboptimal) VkRequire(acquireStatus, "Acquire Vulkan movie swap image");
            auto& current = images.at(imageIndex);
            if (!current.pass || frame.size.width != current.source.width || frame.size.height != current.source.height) {
                current.pass = std::make_unique<VulkanMoviePass>(*device);
                current.source = frame.size;
            }
            current.pass->DrawToSwapImage(frame, extent, current.image, format, available, current.finished,
                                          capture != nullptr, vertex, pixel);
            lastPass = current.pass.get();
            if (capture) {
                *capture = lastPass->Capture(extent);
                if (format == VK_FORMAT_B8G8R8A8_UNORM) {
                    for (std::size_t i = 0; i < capture->size(); i += 4) std::swap((*capture)[i], (*capture)[i + 2]);
                }
            }
            auto view = VkInfo<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
            view.waitSemaphoreCount = view.swapchainCount = 1;
            view.pWaitSemaphores = &current.finished;
            view.pSwapchains = &swapChain;
            view.pImageIndices = &imageIndex;
            const auto status = present(device->queue, &view);
            if (status != VK_SUBOPTIMAL_KHR) VkRequire(status, "Present Vulkan movie swap image");
            if (suboptimal || status == VK_SUBOPTIMAL_KHR) lastClient = {};
            return MoviePresentation::Presented;
        } catch (const VulkanError& error) {
            if (error.Code() == VK_ERROR_OUT_OF_DATE_KHR) {
                DestroySwapChain();
                return MoviePresentation::SurfaceChanged;
            }
            ResetDevice();
            if (error.Code() == VK_ERROR_SURFACE_LOST_KHR) return MoviePresentation::SurfaceChanged;
            if (error.Code() == VK_ERROR_DEVICE_LOST) return MoviePresentation::DeviceLost;
            throw;
        } catch (...) {
            ResetDevice();
            throw;
        }
    }

    Extent BackBufferExtent() const { return extent; }

    void ResetDevice() {
        if (device) {
            DestroySwapChain();
            device.reset();
        }
        if (surface) destroySurface(instance.instance, std::exchange(surface, VK_NULL_HANDLE), nullptr);
    }

private:
    struct SwapImage {
        VkImage image = VK_NULL_HANDLE;
        VkSemaphore finished = VK_NULL_HANDLE;
        std::unique_ptr<VulkanMoviePass> pass;
        Extent source{};
    };

    template<typename T>
    T DeviceProc(const char* name) const {
        const auto function = instance.GetDeviceProcAddr(device->device, name);
        if (!function) throw std::runtime_error(name);
        return reinterpret_cast<T>(function);
    }

    void Initialize() {
        const auto createSurface = instance.Proc<PFN_vkCreateWin32SurfaceKHR>("vkCreateWin32SurfaceKHR");
        destroySurface = instance.Proc<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR");
        const auto capsProc = instance.Proc<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        auto surfaceDesc = VkInfo<VkWin32SurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
        surfaceDesc.hinstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
        surfaceDesc.hwnd = window;
        VkRequire(createSurface(instance.instance, &surfaceDesc, nullptr, &surface), "Create Vulkan Win32 surface");
        surfaceCaps = capsProc;
        device = std::make_unique<VulkanDevice>();
        device->Create(instance, physical, surface);
        createSwapChain = DeviceProc<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR");
        destroySwapChain = DeviceProc<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR");
        getImages = DeviceProc<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");
        createSemaphore = DeviceProc<PFN_vkCreateSemaphore>("vkCreateSemaphore");
        destroySemaphore = DeviceProc<PFN_vkDestroySemaphore>("vkDestroySemaphore");
        acquire = DeviceProc<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR");
        present = DeviceProc<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");
    }

    VkSurfaceFormatKHR FindFormat() const {
        const auto formats = instance.Proc<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR");
        std::uint32_t count = 0;
        VkRequire(formats(physical, surface, &count, nullptr), "Count Vulkan surface formats");
        std::vector<VkSurfaceFormatKHR> choices(count);
        VkRequire(formats(physical, surface, &count, choices.data()), "Read Vulkan surface formats");
        for (const auto desired : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM}) {
            for (const auto candidate : choices) {
                if (candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                    (candidate.format == desired || candidate.format == VK_FORMAT_UNDEFINED)) {
                    return {desired, candidate.colorSpace};
                }
            }
        }
        throw std::runtime_error("No compatible Vulkan swap chain color format");
    }

    VkPresentModeKHR PickPresentMode(bool vsync) const {
        if (vsync) return VK_PRESENT_MODE_FIFO_KHR;
        const auto modes = instance.Proc<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>("vkGetPhysicalDeviceSurfacePresentModesKHR");
        std::uint32_t count = 0;
        VkRequire(modes(physical, surface, &count, nullptr), "Count Vulkan present modes");
        std::vector<VkPresentModeKHR> choices(count);
        VkRequire(modes(physical, surface, &count, choices.data()), "Read Vulkan present modes");
        for (const auto mode : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
            if (std::find(choices.begin(), choices.end(), mode) != choices.end()) return mode;
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    void RecreateSwapChain(Extent client, bool vsync) {
        DestroySwapChain();
        VkSurfaceCapabilitiesKHR limits{};
        VkRequire(surfaceCaps(physical, surface, &limits), "Read Vulkan surface caps");
        const auto surfaceFormat = FindFormat();
        const auto presentMode = PickPresentMode(vsync);
        if (!(limits.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
            throw std::runtime_error("Vulkan swap chain cannot be rendered to");
        }
        auto size = limits.currentExtent;
        if (size.width == std::numeric_limits<std::uint32_t>::max()) {
            size.width = std::clamp(client.width, limits.minImageExtent.width, limits.maxImageExtent.width);
            size.height = std::clamp(client.height, limits.minImageExtent.height, limits.maxImageExtent.height);
        }
        if (!size.width || !size.height) VkRequire(VK_ERROR_OUT_OF_DATE_KHR, "Vulkan surface has no usable extent");
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (const auto mode : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
            if (!(limits.supportedCompositeAlpha & mode)) continue;
            alpha = mode;
            break;
        }
        const auto availableImages = limits.maxImageCount ? limits.maxImageCount : std::numeric_limits<std::uint32_t>::max();
        auto desc = VkInfo<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
        desc.surface = surface;
        desc.minImageCount = std::min(availableImages, std::max(2u, limits.minImageCount));
        desc.imageFormat = surfaceFormat.format;
        desc.imageColorSpace = surfaceFormat.colorSpace;
        desc.imageExtent = size;
        desc.imageArrayLayers = 1;
        desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (limits.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        desc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        desc.preTransform = limits.currentTransform;
        desc.compositeAlpha = alpha;
        desc.presentMode = presentMode;
        desc.clipped = VK_TRUE;
        VkRequire(createSwapChain(device->device, &desc, nullptr, &swapChain), "Create Vulkan movie swap chain");
        std::uint32_t count = 0;
        VkRequire(getImages(device->device, swapChain, &count, nullptr), "Count Vulkan swap images");
        std::vector<VkImage> backBuffers(count);
        VkRequire(getImages(device->device, swapChain, &count, backBuffers.data()), "Read Vulkan swap images");
        images.resize(count);
        const auto semaphore = VkInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        VkRequire(createSemaphore(device->device, &semaphore, nullptr, &available), "Create Vulkan acquire semaphore");
        for (std::size_t i = 0; i < images.size(); ++i) {
            images[i].image = backBuffers[i];
            VkRequire(createSemaphore(device->device, &semaphore, nullptr, &images[i].finished), "Create Vulkan present semaphore");
        }
        captureSupported = (desc.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
        extent = {size.width, size.height};
        lastClient = client;
        format = surfaceFormat.format;
        previousVsync = vsync;
    }

    void DestroySwapChain() {
        if (swapChain) {
            device->DeviceWaitIdle(device->device);
            for (auto& image : images) {
                image.pass.reset();
                destroySemaphore(device->device, image.finished, nullptr);
            }
            images.clear();
            destroySemaphore(device->device, std::exchange(available, VK_NULL_HANDLE), nullptr);
            destroySwapChain(device->device, std::exchange(swapChain, VK_NULL_HANDLE), nullptr);
        }
        lastPass = nullptr;
        extent = lastClient = {};
    }

    HWND window;
    VulkanInstance& instance;
    VkPhysicalDevice physical;
    std::vector<std::uint32_t> vertex, pixel;
    std::unique_ptr<VulkanDevice> device;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    VkSemaphore available = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    PFN_vkDestroySurfaceKHR destroySurface = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR surfaceCaps = nullptr;
    PFN_vkCreateSwapchainKHR createSwapChain = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapChain = nullptr;
    PFN_vkGetSwapchainImagesKHR getImages = nullptr;
    PFN_vkCreateSemaphore createSemaphore = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
    PFN_vkAcquireNextImageKHR acquire = nullptr;
    PFN_vkQueuePresentKHR present = nullptr;
    std::vector<SwapImage> images;
    VulkanMoviePass* lastPass = nullptr;
    Extent extent{}, lastClient{};
    bool previousVsync = false, captureSupported = false;
};

} // namespace movie_pass
