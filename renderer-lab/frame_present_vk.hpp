#pragma once

#include "scene_movie_vk_shared.hpp"
#include <d3d9.h>
#include <array>
#include <algorithm>
#include <string>
#include <memory>
#include <limits>

namespace frame_present {

// PC backbuffers may use XRGB, which has no Vulkan image format. A GPU-local
// buffer carries the unchanged four bytes per pixel between the APIs. This also
// avoids importing a driver's private, tiled multisample render-target layout.
class PresenterVk {
    using Context=scene_movie::VkSharedContext;
    template<class T> using Com=movie_pass::Com<T>;
    struct Slot {
        Com<ID3D12CommandAllocator> allocator;
        Com<ID3D12GraphicsCommandList> list;
        Com<ID3D12Resource> bytes, resolved;
        movie_pass::VulkanBuffer imported{};
        movie_pass::VulkanImage scaled{};
        VkCommandBuffer cmd{};
        VkSemaphore acquired{};
        UINT64 finished=0;
    };
    struct Image { VkImage image{}; VkSemaphore rendered{}; };
    struct Handle { HANDLE h{}; ~Handle(){if(h) CloseHandle(h);} };

    Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;
    HWND window;
    Context shared;
    movie_pass::VulkanDevice& vk;
    Com<ID3D12Fence> fence;
    VkSemaphore timeline{};
    VkCommandPool pool{};
    VkSwapchainKHR chain{};
    VkFormat imageFormat{};
    VkExtent2D extent{},client{};
    D3D12_RESOURCE_DESC input{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    std::vector<Image> images;
    std::array<Slot,3> slots{};
    size_t next=0;
    UINT64 tick=0;
    bool fifo=false, fatal=false, surfaceLost=false, deviceLost=false;
    std::string failure;
    D3DPRESENT_PARAMETERS fullscreen{};
    HMONITOR monitor{};
    MONITORINFOEXW monitorInfo{};
    DEVMODEW previousMode{};
    bool ownsMode=false, acquiredExclusive=false, borderless=false;
    PFN_vkAcquireFullScreenExclusiveModeEXT takeExclusive{};
    PFN_vkReleaseFullScreenExclusiveModeEXT giveExclusive{};

    struct ExclusiveInfo {
        VkSurfaceFullScreenExclusiveWin32InfoEXT win{};
        VkSurfaceFullScreenExclusiveInfoEXT policy{};
        VkPhysicalDeviceSurfaceInfo2KHR query{};
        ExclusiveInfo(VkSurfaceKHR surface,HMONITOR display) {
            win.sType=VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT;
            win.hmonitor=display;
            policy.sType=VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT;
            policy.fullScreenExclusive=VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
            policy.pNext=&win;
            query.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
            query.surface=surface;
            query.pNext=&policy;
        }
    };
    bool MatchesMode(const DEVMODEW& mode) const {
        return mode.dmPelsWidth==fullscreen.BackBufferWidth && mode.dmPelsHeight==fullscreen.BackBufferHeight &&
               (!fullscreen.FullScreen_RefreshRateInHz || mode.dmDisplayFrequency==fullscreen.FullScreen_RefreshRateInHz);
    }
    bool WantsExclusive() const { return monitor && !borderless; }
    bool CanPresentBorderless() const {
        // A compositor flip may stand in for exclusive output only on the
        // requested display, at its requested mode, across its entire client area.
        MONITORINFO info{};info.cbSize=sizeof(info);
        DEVMODEW mode{};mode.dmSize=sizeof(mode);
        RECT clientRect{};POINT origin{};
        if(!monitor || MonitorFromWindow(window,MONITOR_DEFAULTTONULL)!=monitor ||
           !GetMonitorInfoW(monitor,&info) || !GetClientRect(window,&clientRect) || !ClientToScreen(window,&origin) ||
           !EnumDisplaySettingsExW(monitorInfo.szDevice,ENUM_CURRENT_SETTINGS,&mode,0) || !MatchesMode(mode))return false;
        const RECT& screen=info.rcMonitor;
        return origin.x==screen.left && origin.y==screen.top &&
               clientRect.right-clientRect.left==screen.right-screen.left && clientRect.bottom-clientRect.top==screen.bottom-screen.top;
    }
    void EnterDisplayMode() {
        DEVMODEW current{};current.dmSize=sizeof(current);
        if(!EnumDisplaySettingsExW(monitorInfo.szDevice,ENUM_CURRENT_SETTINGS,&current,0))
            throw std::runtime_error("Cannot read the target monitor mode");
        if(MatchesMode(current))return;
        DEVMODEW selected{},candidate{};candidate.dmSize=sizeof(candidate);
        for(DWORD i=0;EnumDisplaySettingsExW(monitorInfo.szDevice,i,&candidate,0);++i) {
            if(!MatchesMode(candidate) || candidate.dmBitsPerPel!=current.dmBitsPerPel ||
               candidate.dmDisplayOrientation!=current.dmDisplayOrientation || (candidate.dmDisplayFlags&DM_INTERLACED))continue;
            candidate.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL|DM_DISPLAYFREQUENCY;
            if(ChangeDisplaySettingsExW(monitorInfo.szDevice,&candidate,nullptr,CDS_TEST,nullptr)!=DISP_CHANGE_SUCCESSFUL)continue;
            // Prefer the current refresh for D3DPRESENT_RATE_DEFAULT; otherwise
            // the first supported progressive mode is valid for this request.
            if(!selected.dmSize)selected=candidate;
            if(fullscreen.FullScreen_RefreshRateInHz || candidate.dmDisplayFrequency==current.dmDisplayFrequency) {
                selected=candidate;
                break;
            }
        }
        if(selected.dmSize){
            if(ChangeDisplaySettingsExW(monitorInfo.szDevice,&selected,nullptr,CDS_FULLSCREEN,nullptr)!=DISP_CHANGE_SUCCESSFUL)
                throw std::runtime_error("Cannot enter the requested Vulkan display mode");
            if(!ownsMode)previousMode=current;
            ownsMode=true;
            current.dmSize=sizeof(current);
            if(!EnumDisplaySettingsExW(monitorInfo.szDevice,ENUM_CURRENT_SETTINGS,&current,0) || !MatchesMode(current))
                throw std::runtime_error("Driver changed the requested Vulkan display mode");
            return;
        }
        throw std::runtime_error("Requested exclusive monitor mode and refresh rate unavailable");
    }

    PFN_vkCreateSemaphore makeSemaphore{};
    PFN_vkCreateSwapchainKHR makeChain{};
    PFN_vkDestroySwapchainKHR freeChain{};
    PFN_vkGetSwapchainImagesKHR chainImages{};
    PFN_vkAcquireNextImageKHR acquire{};
    PFN_vkQueuePresentKHR present{};
    PFN_vkCmdBlitImage blit{};
    PFN_vkGetMemoryWin32HandlePropertiesKHR memoryProperties{};
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getCaps{};
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getFormats{};
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR getModes{};

    static VkFormat Channels(DXGI_FORMAT format) {
        switch(format) {
        case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return VK_FORMAT_R8G8B8A8_UNORM;
        default: return VK_FORMAT_UNDEFINED;
        }
    }
    static DXGI_FORMAT Typed(DXGI_FORMAT f) {
        if(f==DXGI_FORMAT_B8G8R8X8_TYPELESS)return DXGI_FORMAT_B8G8R8X8_UNORM;
        if(f==DXGI_FORMAT_B8G8R8A8_TYPELESS)return DXGI_FORMAT_B8G8R8A8_UNORM;
        if(f==DXGI_FORMAT_R8G8B8A8_TYPELESS)return DXGI_FORMAT_R8G8B8A8_UNORM;
        return f;
    }
    void NewSemaphore(VkSemaphore& target) {
        auto info=movie_pass::VkInfo<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        movie_pass::VkRequire(makeSemaphore(vk.device,&info,nullptr,&target),"Create frame semaphore");
    }
    void WaitFor(UINT64 value) {
        if(!value || fence->GetCompletedValue()>=value)return;
        // A null event requests a synchronous wait, avoiding per-frame event handles.
        movie_pass::Require(fence->SetEventOnCompletion(value,nullptr),"Wait for Vulkan frame completion");
    }
    void Drain() {
        if(tick && fence.Get() && SUCCEEDED(device->GetDeviceRemovedReason())) {
            const UINT64 done=++tick;
            if(SUCCEEDED(queue->Signal(fence.Get(),done)))WaitFor(done);
        }
        shared.WaitIdle();
    }
    void FreeFrames() noexcept {
        for(auto& s:slots){
            shared.DestroySemaphore(std::exchange(s.acquired,VK_NULL_HANDLE));
            if(s.scaled.image || s.scaled.memory)vk.Destroy(s.scaled);
            if(s.imported.buffer || s.imported.memory)vk.Destroy(s.imported);
            s.scaled={};s.imported={};s.bytes.Reset();s.resolved.Reset();s.finished=0;
        }
        for(auto& i:images)shared.DestroySemaphore(i.rendered);
        images.clear();
        if(acquiredExclusive){giveExclusive(vk.device,chain);acquiredExclusive=false;}
        if(chain)freeChain(vk.device,std::exchange(chain,VK_NULL_HANDLE),nullptr);
    }
    void RestoreDisplayMode() noexcept {
        if(ownsMode) {
            previousMode.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL|DM_DISPLAYFREQUENCY;
            ChangeDisplaySettingsExW(monitorInfo.szDevice,&previousMode,nullptr,0,nullptr);
            ownsMode=false;
        }
    }
    void Close() noexcept {
        try {Drain();} catch (...) {}
        FreeFrames();
        RestoreDisplayMode();
        if(pool)vk.DestroyCommandPool(vk.device,pool,nullptr);
        shared.DestroySemaphore(timeline);
    }
    VkPresentModeKHR Mode(bool vsync,const ExclusiveInfo& exclusive) const {
        if(vsync)return VK_PRESENT_MODE_FIFO_KHR;
        uint32_t count=0;
        auto surface=shared.surface.handle;
        auto query=WantsExclusive()?shared.instance.Proc<PFN_vkGetPhysicalDeviceSurfacePresentModes2EXT>("vkGetPhysicalDeviceSurfacePresentModes2EXT"):nullptr;
        const auto enumerate=[&](VkPresentModeKHR* list){return query?query(shared.physical,&exclusive.query,&count,list):
                                                                     getModes(shared.physical,surface,&count,list);};
        movie_pass::VkRequire(enumerate(nullptr),"Count Vulkan present modes");
        std::vector<VkPresentModeKHR> modes(count);
        movie_pass::VkRequire(enumerate(modes.data()),"Read Vulkan present modes");
        for(auto m:{VK_PRESENT_MODE_IMMEDIATE_KHR,VK_PRESENT_MODE_MAILBOX_KHR})
            if(std::find(modes.begin(),modes.end(),m)!=modes.end())return m;
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    VkSurfaceFormatKHR Format(VkFormat preferred,const ExclusiveInfo& exclusive) const {
        uint32_t count=0;
        auto surface=shared.surface.handle;
        std::vector<VkSurfaceFormatKHR> formats;
        if(WantsExclusive()){
            auto query=shared.instance.Proc<PFN_vkGetPhysicalDeviceSurfaceFormats2KHR>("vkGetPhysicalDeviceSurfaceFormats2KHR");
            movie_pass::VkRequire(query(shared.physical,&exclusive.query,&count,nullptr),"Count exclusive Vulkan surface formats");
            std::vector<VkSurfaceFormat2KHR> list(count,movie_pass::VkInfo<VkSurfaceFormat2KHR>(VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR));
            movie_pass::VkRequire(query(shared.physical,&exclusive.query,&count,list.data()),"Read exclusive Vulkan surface formats");
            for(const auto& entry:list)formats.push_back(entry.surfaceFormat);
        } else {
            movie_pass::VkRequire(getFormats(shared.physical,surface,&count,nullptr),"Count Vulkan surface formats");
            formats.resize(count);
            movie_pass::VkRequire(getFormats(shared.physical,surface,&count,formats.data()),"Read Vulkan surface formats");
        }
        for(auto f:{preferred,VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM})
            for(auto item:formats)if(item.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                (item.format==f || item.format==VK_FORMAT_UNDEFINED))return {f,item.colorSpace};
        throw std::runtime_error("No true-color Vulkan surface format");
    }
    void ShareBuffer(Slot& s,UINT64 bufferSize) {
        using namespace movie_pass;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        auto desc=Buffer12(bufferSize);
        Require(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,
            nullptr,__uuidof(ID3D12Resource),s.bytes.AsVoid()),"Create GPU-local frame transfer buffer");
        Handle handle;
        Require(device->CreateSharedHandle(s.bytes.Get(),nullptr,GENERIC_ALL,nullptr,&handle.h),"Share GPU frame transfer buffer");
        constexpr auto kind=VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
        auto external=VkInfo<VkExternalMemoryBufferCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO);
        external.handleTypes=kind;
        auto b=VkInfo<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        b.pNext=&external;b.size=bufferSize;b.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;b.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        VkRequire(vk.CreateBuffer(vk.device,&b,nullptr,&s.imported.buffer),"Create Vulkan frame transfer buffer");
        auto hp=VkInfo<VkMemoryWin32HandlePropertiesKHR>(VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR);
        VkRequire(memoryProperties(vk.device,kind,handle.h,&hp),"Query shared buffer memory type");
        VkMemoryRequirements needs{}; vk.GetBufferMemoryRequirements(vk.device,s.imported.buffer,&needs);
        uint32_t bits=hp.memoryTypeBits & needs.memoryTypeBits;
        if(!bits)throw std::runtime_error("No compatible imported buffer memory");
        auto dedicated=VkInfo<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
        dedicated.buffer=s.imported.buffer;
        auto import=VkInfo<VkImportMemoryWin32HandleInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR);
        import.handleType=kind;import.handle=handle.h;import.pNext=&dedicated;
        auto alloc=VkInfo<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        alloc.pNext=&import;alloc.allocationSize=needs.size;alloc.memoryTypeIndex=0;
        while(!(bits&(1u<<alloc.memoryTypeIndex)))++alloc.memoryTypeIndex;
        VkRequire(vk.AllocateMemory(vk.device,&alloc,nullptr,&s.imported.memory),"Import GPU frame memory");
        VkRequire(vk.BindBufferMemory(vk.device,s.imported.buffer,s.imported.memory,0),"Bind GPU frame memory");
    }
    void Configure(const D3D12_RESOURCE_DESC& in,VkExtent2D current,bool vsync) {
        using namespace movie_pass;
        Drain();FreeFrames();
        VkSurfaceCapabilitiesKHR caps{};
        ExclusiveInfo exclusive(shared.surface.handle,monitor);
        if(monitor)EnterDisplayMode();
        if(WantsExclusive()) {
            auto supported=VkInfo<VkSurfaceCapabilitiesFullScreenExclusiveEXT>(VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_FULL_SCREEN_EXCLUSIVE_EXT);
            auto caps2=VkInfo<VkSurfaceCapabilities2KHR>(VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR);
            caps2.pNext=&supported;
            auto query=shared.instance.Proc<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>("vkGetPhysicalDeviceSurfaceCapabilities2KHR");
            VkRequire(query(shared.physical,&exclusive.query,&caps2),"Query Vulkan exclusive display capabilities");
            if(supported.fullScreenExclusiveSupported)caps=caps2.surfaceCapabilities;
            else borderless=true;
        }
        if(!WantsExclusive()) {
            if(monitor && !CanPresentBorderless())throw std::runtime_error("Game window does not cover the requested fullscreen Vulkan monitor");
            VkRequire(getCaps(shared.physical,shared.surface.handle,&caps),"Vulkan frame surface capabilities");
        }
        // The game copies the scene, then an enabled Vulkan overlay can render
        // into the acquired image as a color attachment before the final flip.
        constexpr VkImageUsageFlags presentUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if((caps.supportedUsageFlags&presentUsage)!=presentUsage ||
           !(caps.supportedCompositeAlpha&VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
            throw std::runtime_error("Vulkan surface lacks opaque transfer presentation");
        const auto fmt=Format(Channels(in.Format),exclusive);
        auto ci=VkInfo<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
        ci.surface=shared.surface.handle;ci.minImageCount=std::max(2u,caps.minImageCount);
        if(caps.maxImageCount)ci.minImageCount=std::min(ci.minImageCount,caps.maxImageCount);
        extent=caps.currentExtent;
        if(extent.width==UINT32_MAX)extent={std::clamp(current.width,caps.minImageExtent.width,caps.maxImageExtent.width),
                                            std::clamp(current.height,caps.minImageExtent.height,caps.maxImageExtent.height)};
        if(!extent.width||!extent.height)throw VulkanError(VK_ERROR_OUT_OF_DATE_KHR,"Window minimized");
        if(monitor) {
            if(extent.width!=fullscreen.BackBufferWidth || extent.height!=fullscreen.BackBufferHeight)
                throw std::runtime_error("Exclusive Vulkan surface does not cover the requested display mode");
            if(WantsExclusive())ci.pNext=&exclusive.policy;
        }
        ci.imageExtent=extent;ci.imageFormat=fmt.format;ci.imageColorSpace=fmt.colorSpace;ci.imageArrayLayers=1;
        ci.imageUsage=presentUsage;ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;ci.preTransform=caps.currentTransform;
        ci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;ci.presentMode=Mode(vsync,exclusive);ci.clipped=VK_TRUE;
        const auto created=makeChain(vk.device,&ci,nullptr,&chain);
        if(created==VK_ERROR_INITIALIZATION_FAILED && WantsExclusive() && CanPresentBorderless())
            throw VulkanError(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT,"Driver declined exclusive Vulkan swapchain");
        VkRequire(created,"Create Vulkan full-frame swapchain");
        if(WantsExclusive()) {
            const auto result=takeExclusive(vk.device,chain);
            // Some drivers report INITIALIZATION_FAILED for an otherwise valid
            // chain when exclusive access is denied by another application.
            if(result==VK_ERROR_INITIALIZATION_FAILED && CanPresentBorderless())
                throw VulkanError(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT,"Driver declined exclusive Vulkan access");
            VkRequire(result,"Acquire exclusive Vulkan monitor");
            acquiredExclusive=true;
        }
        uint32_t count=0;VkRequire(chainImages(vk.device,chain,&count,nullptr),"Count frame images");
        std::vector<VkImage> found(count);VkRequire(chainImages(vk.device,chain,&count,found.data()),"Read frame images");
        images.resize(count);for(size_t i=0;i<count;++i){images[i].image=found[i];NewSemaphore(images[i].rendered);}
        input=in;imageFormat=fmt.format;client=current;fifo=vsync;
        auto desc=in;desc.Format=Typed(in.Format);desc.SampleDesc={1,0};desc.Flags=D3D12_RESOURCE_FLAG_NONE;
        desc.Alignment=0;desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        UINT64 bytes=0;device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&bytes);
        if(!bytes||bytes==UINT64_MAX)throw std::runtime_error("Unknown frame layout");
        bool scale=extent.width!=in.Width || extent.height!=in.Height || fmt.format!=Channels(in.Format);
        if(scale){
            auto props=shared.instance.Proc<PFN_vkGetPhysicalDeviceFormatProperties>("vkGetPhysicalDeviceFormatProperties");
            VkFormatProperties from{},to{};props(shared.physical,Channels(in.Format),&from);props(shared.physical,fmt.format,&to);
            if(!(from.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)||!(to.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))
                throw std::runtime_error("Vulkan output cannot scale this source");
            filter=from.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        }
        for(auto& s:slots){
            NewSemaphore(s.acquired);
            ShareBuffer(s,bytes);
            if(in.SampleDesc.Count>1)CreateResource12(device.Get(),desc,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,s.resolved);
            if(scale)vk.MakeImage(s.scaled,{uint32_t(in.Width),in.Height},Channels(in.Format),VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
        }
    }
    VkFilter filter=VK_FILTER_NEAREST;
    void CopyFrame(Slot& s, ID3D12Resource* source) {
        using namespace movie_pass;
        Require(s.allocator->Reset(),"Reset frame copy allocator");
        Require(s.list->Reset(s.allocator.Get(),nullptr),"Reset frame copy list");
        ID3D12Resource* read=source;
        if(s.resolved.Get()){
            Transition12(s.list.Get(),source,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            Transition12(s.list.Get(),s.resolved.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RESOLVE_DEST);
            s.list->ResolveSubresource(s.resolved.Get(),0,source,0,Typed(input.Format));
            Transition12(s.list.Get(),source,D3D12_RESOURCE_STATE_RESOLVE_SOURCE,D3D12_RESOURCE_STATE_COMMON);
            Transition12(s.list.Get(),s.resolved.Get(),D3D12_RESOURCE_STATE_RESOLVE_DEST,D3D12_RESOURCE_STATE_COPY_SOURCE);
            read=s.resolved.Get();
        } else Transition12(s.list.Get(),source,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{},src{};
        dst.pResource=s.bytes.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        src.pResource=read;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        Transition12(s.list.Get(),s.bytes.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        s.list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        Transition12(s.list.Get(),s.bytes.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        Transition12(s.list.Get(),read,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
        Require(s.list->Close(),"Close full-frame copy");
        ID3D12CommandList* lists[]{s.list.Get()};queue->ExecuteCommandLists(1,lists);
    }
    void ImageBarrier(VkCommandBuffer cmd,VkImage image,VkImageLayout before,VkImageLayout after,VkAccessFlags src,VkAccessFlags dst) {
        auto barrier=movie_pass::VkInfo<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        barrier.oldLayout=before;barrier.newLayout=after;barrier.image=image;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.srcAccessMask=src;barrier.dstAccessMask=dst;
        barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    }
    void TransferOwnership(VkCommandBuffer cmd,VkBuffer buffer,bool acquireBuffer){
        auto barrier=movie_pass::VkInfo<VkBufferMemoryBarrier>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
        barrier.buffer=buffer;barrier.size=VK_WHOLE_SIZE;
        barrier.srcQueueFamilyIndex=acquireBuffer?VK_QUEUE_FAMILY_EXTERNAL:vk.family;
        barrier.dstQueueFamilyIndex=acquireBuffer?vk.family:VK_QUEUE_FAMILY_EXTERNAL;
        barrier.srcAccessMask=acquireBuffer?0:VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask=acquireBuffer?VK_ACCESS_TRANSFER_READ_BIT:0;
        vk.CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,1,&barrier,0,nullptr);
    }
    void Record(Slot& s,VkImage dest) {
        using namespace movie_pass;
        auto cmd=s.cmd;VkRequire(vk.ResetCommandBuffer(cmd,0),"Reset Vulkan frame copy");
        auto begin=VkInfo<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkRequire(vk.BeginCommandBuffer(cmd,&begin),"Record Vulkan frame transfer");
        TransferOwnership(cmd,s.imported.buffer,true);
        ImageBarrier(cmd,dest,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy copy{};copy.bufferRowLength=footprint.Footprint.RowPitch/4;
        copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={uint32_t(input.Width),input.Height,1};
        if(s.scaled.image)ImageBarrier(cmd,s.scaled.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        vk.CmdCopyBufferToImage(cmd,s.imported.buffer,s.scaled.image?s.scaled.image:dest,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        TransferOwnership(cmd,s.imported.buffer,false);
        if(s.scaled.image){
            ImageBarrier(cmd,s.scaled.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            VkImageBlit box{};box.srcSubresource=box.dstSubresource=copy.imageSubresource;
            box.srcOffsets[1]={int32_t(input.Width),int32_t(input.Height),1};box.dstOffsets[1]={int32_t(extent.width),int32_t(extent.height),1};
            blit(cmd,s.scaled.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dest,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&box,filter);
        }
        ImageBarrier(cmd,dest,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_ACCESS_TRANSFER_WRITE_BIT,0);
        VkRequire(vk.EndCommandBuffer(cmd),"Finish Vulkan frame commands");
    }
    HRESULT Draw(ID3D12Resource* src,UINT sync) {
        using namespace movie_pass;
        RECT area{};
        if(!GetClientRect(window,&area) || IsIconic(window) || area.right<=0 || area.bottom<=0)return DXGI_STATUS_OCCLUDED;
        const auto in=src->GetDesc();
        if(in.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||in.DepthOrArraySize!=1||in.MipLevels!=1||!Channels(in.Format))return E_NOTIMPL;
        VkExtent2D size{uint32_t(area.right),uint32_t(area.bottom)};
        if(!chain || input.Width!=in.Width || input.Height!=in.Height || input.Format!=in.Format || input.SampleDesc.Count!=in.SampleDesc.Count ||
           input.SampleDesc.Quality!=in.SampleDesc.Quality || client.width!=size.width || client.height!=size.height || fifo!=bool(sync))
            Configure(in,size,bool(sync));
        Slot& s=slots[next++%slots.size()];
        WaitFor(s.finished);
        uint32_t index=0;
        auto ar=acquire(vk.device,chain,1000000000ull,s.acquired,VK_NULL_HANDLE,&index);
        if(ar==VK_TIMEOUT||ar==VK_NOT_READY)return DXGI_STATUS_OCCLUDED;
        if(ar!=VK_SUBOPTIMAL_KHR)VkRequire(ar,"Acquire Vulkan frame");
        auto& out=images.at(index);
        Record(s,out.image);
        CopyFrame(s,src);
        UINT64 ready=++tick,finished=++tick;
        Require(queue->Signal(fence.Get(),ready),"Finish D3D12 frame copy");
        const VkSemaphore waits[]{timeline,s.acquired},signals[]{timeline,out.rendered};
        const uint64_t waitValues[]{ready,0},signalValues[]{finished,0};
        const VkPipelineStageFlags stages[]{VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT};
        auto values=VkInfo<VkTimelineSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO);
        values.waitSemaphoreValueCount=values.signalSemaphoreValueCount=2;
        values.pWaitSemaphoreValues=waitValues;values.pSignalSemaphoreValues=signalValues;
        auto submit=VkInfo<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        submit.pNext=&values;submit.waitSemaphoreCount=2;submit.pWaitSemaphores=waits;submit.pWaitDstStageMask=stages;
        submit.signalSemaphoreCount=2;submit.pSignalSemaphores=signals;submit.commandBufferCount=1;submit.pCommandBuffers=&s.cmd;
        VkRequire(vk.QueueSubmit(vk.queue,1,&submit,VK_NULL_HANDLE),"Submit Vulkan full frame");
        s.finished=finished;
        Require(queue->Wait(fence.Get(),finished),"Return full frame to D3D12");
        auto view=VkInfo<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        view.waitSemaphoreCount=view.swapchainCount=1;view.pWaitSemaphores=&out.rendered;view.pSwapchains=&chain;view.pImageIndices=&index;
        auto result=present(vk.queue,&view);
        if(result!=VK_SUBOPTIMAL_KHR)VkRequire(result,"Present full frame in Vulkan");
        if(result==VK_SUBOPTIMAL_KHR || ar==VK_SUBOPTIMAL_KHR)client={};
        return S_OK;
    }
public:
    // Negotiated once when a new D3D9 device/reset requests exclusive output.
    // The common owner falls back to legacy presentation if the driver refuses it.
    void ConfigureFullscreen(const D3DPRESENT_PARAMETERS& requested) {
        if(requested.Windowed)return;
        if(chain || !requested.BackBufferWidth || !requested.BackBufferHeight)
            throw std::runtime_error("Invalid request for exclusive Vulkan frame presentation");
        borderless=!shared.exclusivePresentation;
        if(!borderless) {
            takeExclusive=shared.Proc<PFN_vkAcquireFullScreenExclusiveModeEXT>("vkAcquireFullScreenExclusiveModeEXT");
            giveExclusive=shared.Proc<PFN_vkReleaseFullScreenExclusiveModeEXT>("vkReleaseFullScreenExclusiveModeEXT");
        }
        monitor=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
        monitorInfo.cbSize=sizeof(monitorInfo);
        if(!monitor||!GetMonitorInfoW(monitor,&monitorInfo))
            throw std::runtime_error("Cannot identify requested Vulkan monitor");
        fullscreen=requested;
    }
    PresenterVk(ID3D12Device* game,ID3D12CommandQueue* direct,HWND hwnd)
        :device(game),queue(direct),window(hwnd),shared(game,hwnd),vk(shared.vk){
        using namespace movie_pass;
        try {
            makeSemaphore=shared.Proc<PFN_vkCreateSemaphore>("vkCreateSemaphore");
            makeChain=shared.Proc<PFN_vkCreateSwapchainKHR>("vkCreateSwapchainKHR");
            freeChain=shared.Proc<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR");
            chainImages=shared.Proc<PFN_vkGetSwapchainImagesKHR>("vkGetSwapchainImagesKHR");
            acquire=shared.Proc<PFN_vkAcquireNextImageKHR>("vkAcquireNextImageKHR");
            present=shared.Proc<PFN_vkQueuePresentKHR>("vkQueuePresentKHR");
            blit=shared.Proc<PFN_vkCmdBlitImage>("vkCmdBlitImage");
            memoryProperties=shared.Proc<PFN_vkGetMemoryWin32HandlePropertiesKHR>("vkGetMemoryWin32HandlePropertiesKHR");
            getCaps=shared.instance.Proc<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
            getFormats=shared.instance.Proc<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR");
            getModes=shared.instance.Proc<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>("vkGetPhysicalDeviceSurfacePresentModesKHR");
            Require(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,__uuidof(ID3D12Fence),fence.AsVoid()),"Create shared full-frame fence");
            timeline=shared.ImportFence(fence.Get());
            auto info=VkInfo<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
            info.queueFamilyIndex=vk.family;info.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            VkRequire(vk.CreateCommandPool(vk.device,&info,nullptr,&pool),"Create Vulkan frame command pool");
            auto alloc=VkInfo<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
            alloc.commandPool=pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;
            for(auto& s:slots){
                VkRequire(vk.AllocateCommandBuffers(vk.device,&alloc,&s.cmd),"Allocate Vulkan frame commands");
                Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),s.allocator.AsVoid()),"Create full-frame D3D12 allocator");
                Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,s.allocator.Get(),nullptr,__uuidof(ID3D12GraphicsCommandList),s.list.AsVoid()),"Create full-frame D3D12 list");
                Require(s.list->Close(),"Close initial full-frame list");
            }
        } catch(...){Close();throw;}
    }
    PresenterVk(const PresenterVk&)=delete;
    PresenterVk& operator=(const PresenterVk&)=delete;
    ~PresenterVk(){Close();}
    // The D3D9 scene is about to be reset. No scene resources may outlive
    // this point, but queues, the overlay's device and its VkInstance can.
    bool BeforeSceneReset() noexcept {
        if(deviceLost || FAILED(device->GetDeviceRemovedReason()))return false;
        try {
            if(chain) { Drain();FreeFrames(); }
            RestoreDisplayMode();
            monitor=nullptr;fullscreen={};borderless=false;client={};input={};
            if(surfaceLost) {
                auto& owned=shared.surface;
                if(owned.handle)owned.destroy(owned.instance,std::exchange(owned.handle,VK_NULL_HANDLE),nullptr);
                auto info=movie_pass::VkInfo<VkWin32SurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR);
                info.hwnd=window;info.hinstance=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window,GWLP_HINSTANCE));
                const auto make=shared.instance.Proc<PFN_vkCreateWin32SurfaceKHR>("vkCreateWin32SurfaceKHR");
                movie_pass::VkRequire(make(owned.instance,&info,nullptr,&owned.handle),"Recreate Vulkan window surface");
                surfaceLost=false;
            }
            fatal=false;
            failure.clear();
            return true;
        } catch(...) {return false;}
    }
    const char* LastError() const { return failure.c_str(); }
    const char* DisplayMode() const {
        if(!monitor)return "windowed";
        return acquiredExclusive?"exclusive":(borderless?"borderless":"pending");
    }
    HRESULT Present(ID3D12Resource* src,const D3DSURFACE_DESC&,UINT interval) {
        if(!src||fatal)return E_FAIL;
        try{
            try{return Draw(src,interval);}
            catch(const movie_pass::VulkanError& e){
                const auto code=e.Code();
                if(code==VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)acquiredExclusive=false;
                if(!WantsExclusive() || (code!=VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT &&
                   code!=VK_ERROR_NATIVE_WINDOW_IN_USE_KHR) || !CanPresentBorderless())throw;
                // The fullscreen monitor and its refresh are still correct. Reuse
                // the same scene resource and HWND with a normal Vulkan swapchain.
                borderless=true;
                client={};
                return Draw(src,interval);
            }
        }
        catch(const movie_pass::VulkanError& e){
            failure=e.what();
            if(e.Code()==VK_ERROR_OUT_OF_DATE_KHR){client={};return S_FALSE;}
            fatal=true;
            switch(e.Code()){
            case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
                acquiredExclusive=false;
                return DXGI_ERROR_ACCESS_LOST;
            case VK_ERROR_SURFACE_LOST_KHR:surfaceLost=true;return DXGI_ERROR_ACCESS_LOST;
            case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
            case VK_ERROR_DEVICE_LOST:deviceLost=true;return DXGI_ERROR_DEVICE_REMOVED;
            default:return E_FAIL;
            }
        }catch(const movie_pass::D3DError& e){failure=e.what();fatal=true;return e.Code();}
        catch(const std::exception& e){failure=e.what();fatal=true;return E_FAIL;}
    }
};
}


