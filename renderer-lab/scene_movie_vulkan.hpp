#pragma once
#include "scene_movie_vk_composite.hpp"
#include "scene_movie_vk_shared.hpp"

namespace scene_movie {
#include "scene_movie_vulkan_shaders.inc"

class GpuVk {
    using Image = movie_pass::VulkanImage;
    using Buffer = movie_pass::VulkanBuffer;
    using VkDeviceRef = movie_pass::VulkanDevice;
    template<class T> static T Info(VkStructureType type) { return movie_pass::VkInfo<T>(type); }
    static constexpr VkImageSubresourceRange colorRange{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};

    struct Slot {
        Com<ID3D12Resource> shared;
        Image image{};
        VkFramebuffer fb{};
        VkRenderPass pass{};
        VkPipeline pipe{};
        VkSampler sampler{};
        VkSampleCountFlagBits samples{};
        D3D12_CULL_MODE cull{};
        D3D12_SAMPLER_DESC filter{};
        VkDescriptorSet set{};
        VkCommandBuffer cmd{};
        VkFence completed{};
        Buffer vertices{},uniform{},upload{};
        size_t capacity{};
        bool submitted{},used{};
    };
public:
    explicit GpuVk(ID3D12Device* device) : target(device),ctx(device),vk(ctx.vk) {
        try { Initialize(); } catch (...) { Destroy(); throw; }
    }
    ~GpuVk() { Destroy(); }
    ID3D12Device* Device() const { return target.Device(); }
    UINT64 uploads{};
    bool Draw(scene_hud::Interop* interop,IDirect3DSurface9* surface,const D3DSURFACE_DESC& desc,const State& s,
              const Vertex* quad,const Decoded& movie) {
        return target.Draw(interop,surface,desc,s,[&](unsigned index,ID3D12Resource* resource,UINT64 wait,UINT64 signal,
                                                     DXGI_SAMPLE_DESC samples) {
            auto& slot=slots[index];
            if (slot.submitted) {
                movie_pass::VkRequire(vk.WaitForFences(vk.device,1,&slot.completed,VK_TRUE,UINT64_MAX),"Wait for previous Vulkan film draw");
                slot.submitted=false;
            }
            PrepareTarget(slot,resource,desc.Width,desc.Height,static_cast<VkSampleCountFlagBits>(samples.Count),s);
            PrepareSource(s,movie);
            PrepareDescriptors(slot,s);
            const Vertex list[]{quad[0],quad[1],quad[2],quad[0],quad[2],quad[3]};
            Write(slot.vertices,list,sizeof(list));Write(slot.uniform,s.constants,sizeof(s.constants));
            movie_pass::VkRequire(vk.ResetCommandBuffer(slot.cmd,0),"Reset Vulkan movie commands");
            auto begin=Info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
            begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            movie_pass::VkRequire(vk.BeginCommandBuffer(slot.cmd,&begin),"Begin Vulkan movie commands");
            const bool updating=stamp!=movie.stamp;
            if (updating) Upload(slot,movie);
            Barrier(slot.cmd,slot.image.image,slot.used?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    0,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_QUEUE_FAMILY_EXTERNAL,vk.family);
            VkClearValue clear{};
            auto draw=Info<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
            draw.renderPass=slot.pass;draw.framebuffer=slot.fb;draw.renderArea.extent={desc.Width,desc.Height};
            draw.clearValueCount=1;draw.pClearValues=&clear;
            vk.CmdBeginRenderPass(slot.cmd,&draw,VK_SUBPASS_CONTENTS_INLINE);
            vk.CmdBindPipeline(slot.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,slot.pipe);
            vk.CmdBindDescriptorSets(slot.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&slot.set,0,nullptr);
            const VkDeviceSize offset=0;
            bindVertices(slot.cmd,0,1,&slot.vertices.buffer,&offset);
            const auto& v=s.viewport;
            const VkViewport vp{float(v.X),float(v.Y),float(v.Width),float(v.Height),v.MinZ,v.MaxZ};
            const LONG left=std::clamp<LONG>(s.scissor.left,0,desc.Width);
            const LONG top=std::clamp<LONG>(s.scissor.top,0,desc.Height);
            const LONG right=std::clamp<LONG>(s.scissor.right,left,desc.Width);
            const LONG bottom=std::clamp<LONG>(s.scissor.bottom,top,desc.Height);
            const VkRect2D rect{{left,top},{uint32_t(right-left),uint32_t(bottom-top)}};
            setViewport(slot.cmd,0,1,&vp);setScissor(slot.cmd,0,1,&rect);
            vk.CmdDraw(slot.cmd,6,1,0,0);
            vk.CmdEndRenderPass(slot.cmd);
            Barrier(slot.cmd,slot.image.image,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0,vk.family,VK_QUEUE_FAMILY_EXTERNAL);
            movie_pass::VkRequire(vk.EndCommandBuffer(slot.cmd),"End Vulkan movie commands");
            movie_pass::VkRequire(vk.ResetFences(vk.device,1,&slot.completed),"Reset Vulkan movie completion");
            ctx.Submit(slot.cmd,wait?consumed:VK_NULL_HANDLE,wait,written,signal,slot.completed);
            slot.used=slot.submitted=true;
            if (updating) { stamp=movie.stamp;++uploads; }
        });
    }
private:
    void Initialize() {
        using namespace movie_pass;
        consumed=ctx.ImportFence(target.Consumed());written=ctx.ImportFence(target.Written());
        setViewport=ctx.Proc<PFN_vkCmdSetViewport>("vkCmdSetViewport");
        setScissor=ctx.Proc<PFN_vkCmdSetScissor>("vkCmdSetScissor");
        bindVertices=ctx.Proc<PFN_vkCmdBindVertexBuffers>("vkCmdBindVertexBuffers");
        const VkDescriptorSetLayoutBinding bindings[]{
            {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {2,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        auto bind=Info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        bind.bindingCount=3;bind.pBindings=bindings;
        VkRequire(vk.CreateDescriptorSetLayout(vk.device,&bind,nullptr,&setLayout),"Create Vulkan film descriptor layout");
        auto pl=Info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pl.setLayoutCount=1;pl.pSetLayouts=&setLayout;
        VkRequire(vk.CreatePipelineLayout(vk.device,&pl,nullptr,&layout),"Create Vulkan film pipeline layout");
        const uint32_t count=static_cast<uint32_t>(slots.size());
        const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,count},{VK_DESCRIPTOR_TYPE_SAMPLER,count}};
        auto pool=Info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        pool.maxSets=count;pool.poolSizeCount=3;pool.pPoolSizes=sizes;
        VkRequire(vk.CreateDescriptorPool(vk.device,&pool,nullptr,&descriptors),"Create Vulkan film descriptors");
        auto cp=Info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;cp.queueFamilyIndex=vk.family;
        VkRequire(vk.CreateCommandPool(vk.device,&cp,nullptr,&commands),"Create Vulkan movie command pool");
        auto shader=Info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        shader.codeSize=sizeof(MovieVertexSpv);shader.pCode=MovieVertexSpv;
        VkRequire(vk.CreateShaderModule(vk.device,&shader,nullptr,&vs),"Create Vulkan movie vertex shader");
        shader.codeSize=sizeof(VulkanFilmSpv);shader.pCode=VulkanFilmSpv;
        VkRequire(vk.CreateShaderModule(vk.device,&shader,nullptr,&ps),"Create Vulkan movie fragment shader");
        for (auto& slot : slots) {
            auto ci=Info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
            ci.commandPool=commands;ci.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ci.commandBufferCount=1;
            VkRequire(vk.AllocateCommandBuffers(vk.device,&ci,&slot.cmd),"Allocate Vulkan film commands");
            auto di=Info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
            di.descriptorPool=descriptors;di.descriptorSetCount=1;di.pSetLayouts=&setLayout;
            VkRequire(vk.AllocateDescriptorSets(vk.device,&di,&slot.set),"Allocate Vulkan film descriptors");
            auto fi=Info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
            VkRequire(vk.CreateFence(vk.device,&fi,nullptr,&slot.completed),"Create Vulkan film completion fence");
            vk.MakeBuffer(slot.vertices,sizeof(Vertex)*6,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            vk.MakeBuffer(slot.uniform,112,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        }
    }
    void Destroy() noexcept {
        vk.DeviceWaitIdle(vk.device);
        for (auto& s : slots) {
            vk.DestroyFramebuffer(vk.device,s.fb,nullptr);vk.Destroy(s.image);
            vk.DestroyPipeline(vk.device,s.pipe,nullptr);vk.DestroyRenderPass(vk.device,s.pass,nullptr);
            vk.DestroySampler(vk.device,s.sampler,nullptr);vk.DestroyFence(vk.device,s.completed,nullptr);
            vk.Destroy(s.vertices);vk.Destroy(s.uniform);vk.Destroy(s.upload);
        }
        vk.Destroy(input);vk.DestroyShaderModule(vk.device,ps,nullptr);vk.DestroyShaderModule(vk.device,vs,nullptr);
        vk.DestroyDescriptorPool(vk.device,descriptors,nullptr);vk.DestroyCommandPool(vk.device,commands,nullptr);
        vk.DestroyPipelineLayout(vk.device,layout,nullptr);vk.DestroyDescriptorSetLayout(vk.device,setLayout,nullptr);
        ctx.DestroySemaphore(consumed);ctx.DestroySemaphore(written);
    }
    void PrepareTarget(Slot& s,ID3D12Resource* resource,UINT w,UINT h,VkSampleCountFlagBits samples,const State& state) {
        using namespace movie_pass;
        if (s.shared.Get()!=resource || !s.fb) {
            vk.DestroyFramebuffer(vk.device,s.fb,nullptr);s.fb={};
            vk.Destroy(s.image);s.image={};s.shared.Reset();s.used=false;
            if (s.samples!=samples || !s.pass) {
                vk.DestroyPipeline(vk.device,s.pipe,nullptr);s.pipe={};
                vk.DestroyRenderPass(vk.device,s.pass,nullptr);s.pass={};
                VkAttachmentDescription attachment{};
                attachment.format=VK_FORMAT_R16G16B16A16_UNORM;attachment.samples=samples;
                attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
                attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                const VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
                sub.colorAttachmentCount=1;sub.pColorAttachments=&color;
                auto pass=Info<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
                pass.attachmentCount=1;pass.pAttachments=&attachment;pass.subpassCount=1;pass.pSubpasses=&sub;
                VkRequire(vk.CreateRenderPass(vk.device,&pass,nullptr,&s.pass),"Create Vulkan movie render pass");
                s.samples=samples;
            }
            s.image=ctx.ImportImage(resource,VK_FORMAT_R16G16B16A16_UNORM);
            auto info=Info<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
            info.renderPass=s.pass;info.attachmentCount=1;info.pAttachments=&s.image.view;info.width=w;info.height=h;info.layers=1;
            VkRequire(vk.CreateFramebuffer(vk.device,&info,nullptr,&s.fb),"Create Vulkan film framebuffer");
            Com<ID3D12Resource> retained(resource);s.shared.Swap(retained);
        }
        if (!s.pipe || s.cull!=state.cull) {
            vk.DestroyPipeline(vk.device,s.pipe,nullptr);s.pipe={};
            BuildPipeline(s,state.cull);
            s.cull=state.cull;
        }
    }
    void BuildPipeline(Slot& s,D3D12_CULL_MODE cull) {
        auto stage=Info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        VkPipelineShaderStageCreateInfo stages[]{stage,stage};
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vs;stages[0].pName="MovieVertex";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=ps;stages[1].pName="VulkanFilm";
        const VkVertexInputBindingDescription binding{0,sizeof(Vertex),VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attributes[]{{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R8G8B8A8_UNORM,12},{2,0,VK_FORMAT_R32G32_SFLOAT,16}};
        auto inputState=Info<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
        inputState.vertexBindingDescriptionCount=1;inputState.pVertexBindingDescriptions=&binding;
        inputState.vertexAttributeDescriptionCount=3;inputState.pVertexAttributeDescriptions=attributes;
        auto assembly=Info<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        auto vp=Info<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        vp.viewportCount=vp.scissorCount=1;
        auto raster=Info<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        raster.polygonMode=VK_POLYGON_MODE_FILL;raster.lineWidth=1;raster.frontFace=VK_FRONT_FACE_CLOCKWISE;
        raster.cullMode=cull==D3D12_CULL_MODE_NONE?VK_CULL_MODE_NONE:cull==D3D12_CULL_MODE_FRONT?VK_CULL_MODE_FRONT_BIT:VK_CULL_MODE_BACK_BIT;
        auto ms=Info<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        ms.rasterizationSamples=s.samples;
        VkPipelineColorBlendAttachmentState color{};color.colorWriteMask=15;
        auto cb=Info<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        cb.attachmentCount=1;cb.pAttachments=&color;
        const VkDynamicState ds[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
        auto dynamic=Info<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        dynamic.dynamicStateCount=2;dynamic.pDynamicStates=ds;
        auto p=Info<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        p.stageCount=2;p.pStages=stages;p.pVertexInputState=&inputState;p.pInputAssemblyState=&assembly;
        p.pViewportState=&vp;p.pRasterizationState=&raster;p.pMultisampleState=&ms;p.pColorBlendState=&cb;
        p.pDynamicState=&dynamic;p.layout=layout;p.renderPass=s.pass;
        movie_pass::VkRequire(vk.CreateGraphicsPipelines(vk.device,VK_NULL_HANDLE,1,&p,nullptr,&s.pipe),"Create Vulkan movie material");
    }
    void PrepareSource(const State& s,const Decoded& movie) {
        if (input.view && width==movie.width && height==movie.height && srgb==s.srgbSample && opaque==s.forceOpaque) return;
        if (input.image) { ctx.WaitIdle();vk.Destroy(input);input={}; }
        width=movie.width;height=movie.height;srgb=s.srgbSample;opaque=s.forceOpaque;stamp=0;
        const auto format=srgb?VK_FORMAT_B8G8R8A8_SRGB:VK_FORMAT_B8G8R8A8_UNORM;
        vk.MakeImage(input,{width,height},format,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        if (opaque) {
            vk.DestroyImageView(vk.device,input.view,nullptr);input.view={};
            auto view=Info<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            view.image=input.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;
            view.components.a=VK_COMPONENT_SWIZZLE_ONE;view.subresourceRange=colorRange;
            movie_pass::VkRequire(vk.CreateImageView(vk.device,&view,nullptr,&input.view),"Create opaque Vulkan film view");
        }
    }
    static VkSamplerAddressMode Address(D3D12_TEXTURE_ADDRESS_MODE mode) {
        switch(mode) {
        case D3D12_TEXTURE_ADDRESS_MODE_WRAP: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case D3D12_TEXTURE_ADDRESS_MODE_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case D3D12_TEXTURE_ADDRESS_MODE_CLAMP: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case D3D12_TEXTURE_ADDRESS_MODE_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        default: throw std::runtime_error("Unsupported Vulkan film sampler address mode");
        }
    }
    void PrepareDescriptors(Slot& slot,const State& state) {
        const auto& f=state.sampler;
        if (!slot.sampler || std::memcmp(&slot.filter,&f,sizeof(f))) {
            auto s=Info<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
            s.addressModeU=Address(f.AddressU);s.addressModeV=Address(f.AddressV);s.addressModeW=Address(f.AddressW);
            s.minFilter=(f.Filter&16)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;s.magFilter=(f.Filter&4)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
            s.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;s.compareOp=VK_COMPARE_OP_ALWAYS;
            s.anisotropyEnable=f.Filter==D3D12_FILTER_ANISOTROPIC;
            if (s.anisotropyEnable && !ctx.samplerAnisotropy) throw std::runtime_error("Vulkan film anisotropy unavailable");
            s.maxAnisotropy=std::min(ctx.maxAnisotropy,float(f.MaxAnisotropy));
            if (s.addressModeU==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER || s.addressModeV==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER) {
                const float* b=f.BorderColor;
                if (b[0]==0 && b[1]==0 && b[2]==0 && b[3]==0) s.borderColor=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
                else if (b[0]==0 && b[1]==0 && b[2]==0 && b[3]==1) s.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
                else if (b[0]==1 && b[1]==1 && b[2]==1 && b[3]==1) s.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
                else throw std::runtime_error("Unsupported Vulkan movie sampler border color");
            }
            VkSampler next{};
            movie_pass::VkRequire(vk.CreateSampler(vk.device,&s,nullptr,&next),"Create Vulkan movie sampler");
            vk.DestroySampler(vk.device,slot.sampler,nullptr);slot.sampler=next;slot.filter=f;
        }
        const VkDescriptorBufferInfo buffer{slot.uniform.buffer,0,112};
        const VkDescriptorImageInfo texture{VK_NULL_HANDLE,input.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo filter{slot.sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
        auto descriptor=Info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        descriptor.dstSet=slot.set;descriptor.descriptorCount=1;
        VkWriteDescriptorSet writes[]{descriptor,descriptor,descriptor};
        writes[0].dstBinding=0;writes[0].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[0].pBufferInfo=&buffer;
        writes[1].dstBinding=1;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[1].pImageInfo=&texture;
        writes[2].dstBinding=2;writes[2].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;writes[2].pImageInfo=&filter;
        vk.UpdateDescriptorSets(vk.device,3,writes,0,nullptr);
    }
    void Write(const Buffer& buffer,const void* data,size_t bytes) {
        void* mapped=nullptr;
        movie_pass::VkRequire(vk.MapMemory(vk.device,buffer.memory,0,bytes,0,&mapped),"Map Vulkan movie data");
        std::memcpy(mapped,data,bytes);vk.UnmapMemory(vk.device,buffer.memory);
    }
    void Barrier(VkCommandBuffer cmd,VkImage image,VkImageLayout from,VkImageLayout to,VkPipelineStageFlags before,
                 VkPipelineStageFlags after,VkAccessFlags read,VkAccessFlags write,uint32_t src=VK_QUEUE_FAMILY_IGNORED,
                 uint32_t dst=VK_QUEUE_FAMILY_IGNORED) {
        auto b=Info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        b.oldLayout=from;b.newLayout=to;b.srcQueueFamilyIndex=src;b.dstQueueFamilyIndex=dst;b.image=image;
        b.srcAccessMask=read;b.dstAccessMask=write;b.subresourceRange=colorRange;
        vk.CmdPipelineBarrier(cmd,before,after,0,0,nullptr,0,nullptr,1,&b);
    }
    void Upload(Slot& slot,const Decoded& movie) {
        const size_t bytes=movie.bgra.size();
        if (slot.capacity<bytes) {
            vk.Destroy(slot.upload);slot.upload={};slot.capacity=0;
            vk.MakeBuffer(slot.upload,bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);slot.capacity=bytes;
        }
        Write(slot.upload,movie.bgra.data(),bytes);
        Barrier(slot.cmd,input.image,stamp?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                stamp?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
                stamp?VK_ACCESS_SHADER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={width,height,1};
        vk.CmdCopyBufferToImage(slot.cmd,slot.upload.buffer,input.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
        Barrier(slot.cmd,input.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    }
    VkMovieComposite target;
    VkSharedContext ctx;
    VkDeviceRef& vk;
    VkSemaphore consumed{},written{};
    VkCommandPool commands{};
    VkDescriptorSetLayout setLayout{};
    VkPipelineLayout layout{};
    VkDescriptorPool descriptors{};
    VkShaderModule vs{},ps{};
    PFN_vkCmdSetViewport setViewport{};
    PFN_vkCmdSetScissor setScissor{};
    PFN_vkCmdBindVertexBuffers bindVertices{};
    std::array<Slot,3> slots{};
    Image input{};
    UINT width{},height{};
    UINT64 stamp{};
    bool opaque{},srgb{};
};
} // namespace scene_movie
