#pragma once
#include "scene_movie_state.hpp"

namespace scene_movie {

// The game owns its final color buffer. Vulkan produces a shared movie layer
// at the same sample count and D3D12 applies the current FE blending to it.
class VkMovieComposite {
    struct Slot {
        Com<ID3D12Resource> image;
        Com<ID3D12DescriptorHeap> heap;
        Com<ID3D12PipelineState> pipeline;
        DXGI_FORMAT destination = DXGI_FORMAT_UNKNOWN;
        D3D12_RENDER_TARGET_BLEND_DESC blend{};
        UINT sampleMask{};
        UINT64 consumed{};
    };
public:
    explicit VkMovieComposite(ID3D12Device* device) : target(device) {
        Require(device->CreateRootSignature(0,CompositeRoot,sizeof(CompositeRoot),__uuidof(ID3D12RootSignature),
                                            root.AsVoid()),"Create Vulkan movie composition signature");
        for (auto* fence : {&written,&consumed})
            Require(device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,__uuidof(ID3D12Fence),fence->AsVoid()),"Create Vulkan movie timeline");
        const D3D12_DESCRIPTOR_HEAP_DESC desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,2,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,0};
        for (auto& slot : slots)
            Require(device->CreateDescriptorHeap(&desc,__uuidof(ID3D12DescriptorHeap),slot.heap.AsVoid()),"Create Vulkan movie layer descriptors");
    }
    ~VkMovieComposite() { target.Drain(); }
    ID3D12Device* Device() const { return target.Device(); }
    ID3D12Fence* Written() const { return written.Get(); }
    ID3D12Fence* Consumed() const { return consumed.Get(); }

    template<class Render>
    bool Draw(scene_hud::Interop* interop,IDirect3DSurface9* surface,const D3DSURFACE_DESC& desc,const State& state,Render render) {
        unsigned used{};
        bool drew=target.DrawSurface(interop,surface,desc,[&](unsigned index,ID3D12GraphicsCommandList* cmd,
                        DXGI_FORMAT format,DXGI_SAMPLE_DESC samples,D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
            used=index;
            auto& slot=slots[index];
            const auto old=slot.image.Get()?slot.image->GetDesc():D3D12_RESOURCE_DESC{};
            const bool resize=old.Width!=desc.Width || old.Height!=desc.Height || old.SampleDesc.Count!=samples.Count ||
                              old.SampleDesc.Quality!=samples.Quality;
            if (resize) {
                D3D12_RESOURCE_DESC image{};
                image.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                image.Width=desc.Width;image.Height=desc.Height;
                image.DepthOrArraySize=image.MipLevels=1;
                image.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                image.SampleDesc=samples;image.Format=DXGI_FORMAT_R16G16B16A16_UNORM;
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
                Require(Device()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&image,D3D12_RESOURCE_STATE_COMMON,nullptr,
                        __uuidof(ID3D12Resource),slot.image.AsVoid()),"Create shared Vulkan film target");
                auto handle=slot.heap->GetCPUDescriptorHandleForHeapStart();
                Device()->CreateShaderResourceView(slot.image.Get(),nullptr,handle);
                handle.ptr+=Device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                Device()->CreateShaderResourceView(slot.image.Get(),nullptr,handle);
            }
            if (resize || !slot.pipeline.Get() || format!=slot.destination || slot.sampleMask!=state.sampleMask ||
                std::memcmp(&slot.blend,&state.blend,sizeof(state.blend))) {
                D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
                p.pRootSignature=root.Get();p.VS={CompositeVertex,sizeof(CompositeVertex)};
                p.PS=samples.Count>1 ? D3D12_SHADER_BYTECODE{CompositeFilmMS,sizeof(CompositeFilmMS)}
                                     : D3D12_SHADER_BYTECODE{CompositeFilm,sizeof(CompositeFilm)};
                p.BlendState.RenderTarget[0]=state.blend;p.SampleMask=state.sampleMask;p.SampleDesc=samples;
                p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
                p.RasterizerState.DepthClipEnable=TRUE;p.RasterizerState.MultisampleEnable=samples.Count>1;
                p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
                p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=format;
                Require(Device()->CreateGraphicsPipelineState(&p,__uuidof(ID3D12PipelineState),slot.pipeline.AsVoid()),"Create Vulkan film compositor");
                slot.destination=format;slot.blend=state.blend;slot.sampleMask=state.sampleMask;
            }
            const UINT64 serial=++writes;
            render(index,slot.image.Get(),slot.consumed,serial,samples);
            Require(target.Queue()->Wait(written.Get(),serial),"Wait for Vulkan film layer");
            movie_pass::Transition12(cmd,slot.image.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->SetPipelineState(slot.pipeline.Get());cmd->SetGraphicsRootSignature(root.Get());
            ID3D12DescriptorHeap* heap=slot.heap.Get();
            cmd->SetDescriptorHeaps(1,&heap);cmd->SetGraphicsRootDescriptorTable(1,heap->GetGPUDescriptorHandleForHeapStart());
            const auto& v=state.viewport;
            const D3D12_VIEWPORT vp{float(v.X),float(v.Y),float(v.Width),float(v.Height),0,1};
            cmd->RSSetViewports(1,&vp);cmd->RSSetScissorRects(1,&state.scissor);cmd->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);cmd->DrawInstanced(3,1,0,0);
            movie_pass::Transition12(cmd,slot.image.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        });
        if (drew) {
            const UINT64 next=++reads;
            if (SUCCEEDED(target.Queue()->Signal(consumed.Get(),next))) slots[used].consumed=next;
            else { target.Drain();slots[used].consumed=0; }
        }
        return drew;
    }
private:
    scene_hud::NativeHudPass target;
    Com<ID3D12RootSignature> root;
    Com<ID3D12Fence> written,consumed;
    std::array<Slot,3> slots;
    UINT64 writes{},reads{};
};
} // namespace scene_movie
