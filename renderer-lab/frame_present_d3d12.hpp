#pragma once
#include "scene_hud_d3d12.hpp"
#include "frame_present_shaders.hpp"
#include <dxgi1_4.h>
#include <memory>

namespace frame_present {
using movie_pass::Com;
using movie_pass::Require;

inline DXGI_FORMAT SourceViewFormat(DXGI_FORMAT fmt) {
    switch (fmt) {
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: case DXGI_FORMAT_B8G8R8X8_UNORM: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

inline DXGI_FORMAT FlipFormat(DXGI_FORMAT source) {
    const auto fmt=SourceViewFormat(source);
    return fmt==DXGI_FORMAT_B8G8R8X8_UNORM?DXGI_FORMAT_B8G8R8A8_UNORM:fmt;
}

class Writer12 {
    struct Batch {
        Com<ID3D12CommandAllocator> allocator;
        Com<ID3D12GraphicsCommandList> list;
        Com<ID3D12DescriptorHeap> srv,rtv;
        Com<ID3D12Resource> temporary;
        UINT64 done{};
    };
public:
    Writer12(ID3D12Device* gpu,ID3D12CommandQueue* direct) : device(gpu),queue(direct) {
        Require(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),fence.AsVoid()),"Create frame writer fence");
        Require(device->CreateRootSignature(0,FrameRoot,sizeof(FrameRoot),__uuidof(ID3D12RootSignature),root.AsVoid()),"Create frame conversion layout");
        for(auto& b:batches) {
            Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),b.allocator.AsVoid()),"Create frame allocator");
            Require(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,b.allocator.Get(),nullptr,__uuidof(ID3D12GraphicsCommandList),b.list.AsVoid()),"Create frame list");
            Require(b.list->Close(),"Close new frame list");
            movie_pass::MakeHeap12(device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,b.srv);
            movie_pass::MakeHeap12(device.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_RTV,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,b.rtv);
        }
    }
    ~Writer12() {
        if(serial && fence->GetCompletedValue()<serial) fence->SetEventOnCompletion(serial,nullptr);
    }
    IUnknown* SwapDevice() const { return queue.Get(); }
    void Write(ID3D12Resource* source,const D3DSURFACE_DESC&,IDXGISwapChain3* chain,UINT index) {
        auto& batch=batches[serial%batches.size()];
        Com<ID3D12Resource> dest;
        Require(chain->GetBuffer(index,__uuidof(ID3D12Resource),dest.AsVoid()),"Get D3D12 presentation buffer");
        const auto from=source->GetDesc(),to=dest->GetDesc();
        const auto view=SourceViewFormat(from.Format);
        if(view==DXGI_FORMAT_UNKNOWN || FlipFormat(view)!=to.Format || from.Width!=to.Width || from.Height!=to.Height ||
           from.MipLevels!=1 || from.DepthOrArraySize!=1 || from.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D)
            throw std::runtime_error("Incompatible game swapchain image");
        if(batch.done && fence->GetCompletedValue()<batch.done)
            Require(fence->SetEventOnCompletion(batch.done,nullptr),"Recycle presentation allocator");
        Require(batch.allocator->Reset(),"Reset frame allocator");
        Require(batch.list->Reset(batch.allocator.Get(),nullptr),"Reset frame commands");
        auto* commands=batch.list.Get();
        try {
            if(view==to.Format) {
                if(from.SampleDesc.Count==1)
                    Transfer(commands,source,dest.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PRESENT,false,view);
                else Transfer(commands,source,dest.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PRESENT,true,view);
            } else {
                // DXGI flip does not accept BGRX. Preserve the game frame's RGB
                // by sampling the BGRX source directly and writing opaque BGRA.
                ID3D12Resource* readable=source;
                if (from.SampleDesc.Count>1 || (from.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) {
                    auto tmp=batch.temporary.Get()?batch.temporary->GetDesc():D3D12_RESOURCE_DESC{};
                    if(tmp.Width!=from.Width || tmp.Height!=from.Height || tmp.Format!=view) {
                        tmp=from;tmp.SampleDesc={1,0};tmp.Alignment=0;tmp.Flags=D3D12_RESOURCE_FLAG_NONE;tmp.Format=view;
                        movie_pass::CreateResource12(device.Get(),tmp,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,batch.temporary);
                    }
                    readable=batch.temporary.Get();
                    Transfer(commands,source,readable,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COMMON,from.SampleDesc.Count>1,view);
                }
                Blit(batch,readable,dest.Get(),view,to);
            }
            Require(commands->Close(),"Close frame commands");
        } catch (...) {commands->Close();throw;}
        ID3D12CommandList* submit[]{commands};
        queue->ExecuteCommandLists(1,submit);
        const UINT64 next=serial+1;
        Require(queue->Signal(fence.Get(),next),"Signal written frame");
        batch.done=serial=next;
    }
private:
    static void Transfer(ID3D12GraphicsCommandList* cmd,ID3D12Resource* from,ID3D12Resource* to,
                         D3D12_RESOURCE_STATES startFrom,D3D12_RESOURCE_STATES startTo,bool resolve,DXGI_FORMAT format) {
        const auto read=resolve?D3D12_RESOURCE_STATE_RESOLVE_SOURCE:D3D12_RESOURCE_STATE_COPY_SOURCE;
        const auto write=resolve?D3D12_RESOURCE_STATE_RESOLVE_DEST:D3D12_RESOURCE_STATE_COPY_DEST;
        movie_pass::Transition12(cmd,from,startFrom,read);movie_pass::Transition12(cmd,to,startTo,write);
        if (resolve) cmd->ResolveSubresource(to,0,from,0,format);
        else cmd->CopyResource(to,from);
        movie_pass::Transition12(cmd,to,write,startTo);movie_pass::Transition12(cmd,from,read,startFrom);
    }
    void Blit(Batch& batch,ID3D12Resource* source,ID3D12Resource* output,DXGI_FORMAT format,const D3D12_RESOURCE_DESC& size) {
        if(!pso.Get()) {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
            p.pRootSignature=root.Get();p.VS={FrameVertex,sizeof(FrameVertex)};p.PS={FramePixel,sizeof(FramePixel)};
            auto& blend=p.BlendState.RenderTarget[0];
            blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
            blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=15;
            p.SampleMask=UINT_MAX;p.SampleDesc.Count=1;p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
            p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
            p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
            p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.NumRenderTargets=1;p.RTVFormats[0]=size.Format;
            Require(device->CreateGraphicsPipelineState(&p,__uuidof(ID3D12PipelineState),pso.AsVoid()),"Create BGRX frame converter");
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC read{};
        read.Format=format;read.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        read.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;read.Texture2D.MipLevels=1;
        device->CreateShaderResourceView(source,&read,batch.srv->GetCPUDescriptorHandleForHeapStart());
        auto rtv=batch.rtv->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(output,nullptr,rtv);
        auto* cmd=batch.list.Get();
        movie_pass::Transition12(cmd,source,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        movie_pass::Transition12(cmd,output,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->SetGraphicsRootSignature(root.Get());cmd->SetPipelineState(pso.Get());
        ID3D12DescriptorHeap* heaps[]{batch.srv.Get()};
        cmd->SetDescriptorHeaps(1,heaps);cmd->SetGraphicsRootDescriptorTable(0,batch.srv->GetGPUDescriptorHandleForHeapStart());
        const D3D12_VIEWPORT vp{0,0,float(size.Width),float(size.Height),0,1};
        const RECT rect{0,0,LONG(size.Width),LONG(size.Height)};
        cmd->RSSetViewports(1,&vp);cmd->RSSetScissorRects(1,&rect);cmd->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);cmd->DrawInstanced(3,1,0,0);
        movie_pass::Transition12(cmd,output,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
        movie_pass::Transition12(cmd,source,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
    }
    Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;
    Com<ID3D12Fence> fence;
    Com<ID3D12RootSignature> root;
    Com<ID3D12PipelineState> pso;
    std::array<Batch,3> batches;
    UINT64 serial{};
};
} // namespace frame_present
