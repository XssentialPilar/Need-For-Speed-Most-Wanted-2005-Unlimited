#pragma once

#include "scene_hud_d3d12.hpp"
#include <d3d11_4.h>
#include <cmath>

namespace interop_hud {
using movie_pass::Com;
using movie_pass::Require;

class SharedHandle {
public:
    HANDLE value = nullptr;
    ~SharedHandle() { if (value) CloseHandle(value); }
};

inline D3D12_BOX InstrumentBounds(const void* vertices, UINT count, const D3DVIEWPORT9& viewport) {
    const auto* vertex = static_cast<const BYTE*>(vertices);
    float left = float(viewport.X + viewport.Width), top = float(viewport.Y + viewport.Height);
    float right = float(viewport.X), bottom = float(viewport.Y);
    for (UINT i=0; i<count; ++i,vertex+=20) {
        float xy[2];
        std::memcpy(xy,vertex,sizeof(xy));
        if (!std::isfinite(xy[0]) || !std::isfinite(xy[1])) return {};
        left=std::min(left,xy[0]); top=std::min(top,xy[1]);
        right=std::max(right,xy[0]); bottom=std::max(bottom,xy[1]);
    }
    const auto x = [&](float v){return UINT(std::clamp(v,float(viewport.X),float(viewport.X+viewport.Width)));};
    const auto y = [&](float v){return UINT(std::clamp(v,float(viewport.Y),float(viewport.Y+viewport.Height)));};
    return {x(std::floor(left)-1),y(std::floor(top)-1),0,x(std::ceil(right)+2),y(std::ceil(bottom)+2),1};
}

struct CommandPair {
    Com<ID3D12CommandAllocator> allocator;
    Com<ID3D12GraphicsCommandList> before, after;
    Com<ID3D12Resource> surface;
    Com<ID3D11RenderTargetView> view;
    D3D12_RESOURCE_DESC desc{};
    UINT64 inUse=0;
};

class Native11 {
public:
    explicit Native11(ID3D12Device* owner) : device12(owner) {
        static const HMODULE dxgi=LoadLibraryExW(L"dxgi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        static const HMODULE d3d11=LoadLibraryExW(L"d3d11.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        const auto makeFactory=dxgi ? reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(dxgi,"CreateDXGIFactory1")) : nullptr;
        const auto makeDevice=d3d11 ? reinterpret_cast<decltype(&D3D11CreateDevice)>(GetProcAddress(d3d11,"D3D11CreateDevice")) : nullptr;
        if (!makeFactory || !makeDevice) throw std::runtime_error("System D3D11 unavailable");
        Com<IDXGIFactory4> factory;
        Require(makeFactory(__uuidof(IDXGIFactory4),factory.AsVoid()),"Create D3D11 adapter factory");
        Com<IDXGIAdapter> gpu;
        Require(factory->EnumAdapterByLuid(device12->GetAdapterLuid(),__uuidof(IDXGIAdapter),gpu.AsVoid()),"Match HUD GPU");
        Com<ID3D11Device> device;
        Com<ID3D11DeviceContext> context;
        Require(makeDevice(gpu.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,
                          device.Put(),nullptr,context.Put()),"Create independent D3D11 device");
        Require(device->QueryInterface(__uuidof(ID3D11Device5),device11.AsVoid()),"Get D3D11.4 device");
        Require(context->QueryInterface(__uuidof(ID3D11DeviceContext4),context11.AsVoid()),"Get D3D11.4 context");
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        Require(device12->CreateCommandQueue(&queueDesc,__uuidof(ID3D12CommandQueue),queue.AsVoid()),"Create HUD transfer queue");
        Require(device12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,__uuidof(ID3D12Fence),fence12.AsVoid()),"Create HUD shared fence");
        SharedHandle handle;
        Require(device12->CreateSharedHandle(fence12.Get(),nullptr,GENERIC_ALL,nullptr,&handle.value),"Export HUD fence");
        Require(device11->OpenSharedFence(handle.value,__uuidof(ID3D11Fence),fence11.AsVoid()),"Import HUD fence");
        MakePipeline();
        for(auto& slot:slots) {
            Require(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),slot.allocator.AsVoid()),"Create copy allocator");
            for(auto* command : {&slot.before,&slot.after}) {
                Require(device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,
                                          __uuidof(ID3D12GraphicsCommandList),command->AsVoid()),"Create HUD copy list");
                Require((*command)->Close(),"Close HUD copy list");
            }
        }
    }
    ~Native11() {
        if(submitted && fence12->GetCompletedValue()<submitted && SUCCEEDED(device12->GetDeviceRemovedReason()))
            fence12->SetEventOnCompletion(submitted,nullptr);
    }
    ID3D12Device* Owner() const { return device12.Get(); }
    bool Draw(scene_hud::Interop* interop,IDirect3DSurface9* target,const D3DSURFACE_DESC& legacy,
              const D3DVIEWPORT9& viewport,const void* data,UINT count) {
        auto bounds=InstrumentBounds(data,count,viewport);
        if(bounds.left>=bounds.right || bounds.top>=bounds.bottom) return false;
        const UINT64 done=fence12->GetCompletedValue();
        if(done==UINT64_MAX) return false;
        auto slot=std::find_if(slots.begin(),slots.end(),[&](const auto& s){return s.inUse<=done;});
        if(slot==slots.end()) return false;
        scene_hud::TargetLease lease(interop,target,queue.Get());
        const auto source=lease.resource->GetDesc();
        const auto format=scene_hud::ColorFormat(legacy.Format,source.Format);
        if(format==DXGI_FORMAT_UNKNOWN || source.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
           source.DepthOrArraySize!=1 || source.MipLevels!=1 || source.Width!=legacy.Width ||
           source.Height!=legacy.Height || bounds.right>source.Width || bounds.bottom>source.Height) return false;
        const bool msaa=source.SampleDesc.Count>1;
        if(msaa) bounds={0,0,0,UINT(source.Width),source.Height,1};
        auto desc=source;
        desc.Width=bounds.right-bounds.left; desc.Height=bounds.bottom-bounds.top;
        desc.Format=format; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.Alignment=0; desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
        EnsureSurface(*slot,desc);
        Require(slot->allocator->Reset(),"Reset HUD copy allocator");
        RecordCopy(slot->before.Get(),slot->allocator.Get(),lease.resource.Get(),slot->surface.Get(),bounds,true,msaa);
        RecordCopy(slot->after.Get(),slot->allocator.Get(),lease.resource.Get(),slot->surface.Get(),bounds,false,msaa);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(context11->Map(vertexData.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped),"Upload HUD vertices");
        std::memcpy(mapped.pData,data,size_t(count)*20);
        context11->Unmap(vertexData.Get(),0);
        const scene_hud::HudViewport constants{float(bounds.left),float(bounds.top),float(desc.Width),float(desc.Height)};
        context11->UpdateSubresource(viewData.Get(),0,nullptr,&constants,0,0);
        const D3D11_VIEWPORT area{0,0,float(desc.Width),float(desc.Height),0,1};
        context11->RSSetViewports(1,&area);
        // Full-copy MSAA includes pixels outside the game viewport. Clip the
        // programmable draw to the same rectangle as the retail rasterizer.
        const D3D11_RECT scissor{LONG(viewport.X)-LONG(bounds.left), LONG(viewport.Y)-LONG(bounds.top),
                 LONG(viewport.X+viewport.Width)-LONG(bounds.left), LONG(viewport.Y+viewport.Height)-LONG(bounds.top)};
        context11->RSSetScissorRects(1,&scissor);
        auto* view=slot->view.Get();
        context11->OMSetRenderTargets(1,&view,nullptr);

        const auto pre=submitted+1, draw=pre+1, post=draw+1;
        ID3D12CommandList* list=slot->before.Get();
        queue->ExecuteCommandLists(1,&list);
        Require(queue->Signal(fence12.Get(),pre),"Signal game color copy");
        submitted=slot->inUse=pre;
        try {
            Require(context11->Wait(fence11.Get(),pre),"Order D3D11 after game copy");
            context11->Draw(count,0);
            context11->OMSetRenderTargets(0,nullptr,nullptr);
            Require(context11->Signal(fence11.Get(),draw),"Signal native D3D11 drawing");
            context11->Flush();
            submitted=slot->inUse=draw;
            Require(queue->Wait(fence12.Get(),draw),"Order game copy after HUD");
            list=slot->after.Get();
            queue->ExecuteCommandLists(1,&list);
            Require(queue->Signal(fence12.Get(),post),"Signal combined HUD");
            submitted=slot->inUse=post;
        } catch (...) {
            lease.Return(submitted,fence12.Get());
            throw;
        }
        Require(lease.Return(post,fence12.Get()),"Return synchronized game color");
        return true;
    }
private:
    void EnsureSurface(CommandPair& slot,const D3D12_RESOURCE_DESC& desc) {
        if(slot.view.Get() && desc.Width==slot.desc.Width && desc.Height==slot.desc.Height && desc.Format==slot.desc.Format &&
           desc.SampleDesc.Count==slot.desc.SampleDesc.Count && desc.SampleDesc.Quality==slot.desc.SampleDesc.Quality) return;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type=D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask=heap.VisibleNodeMask=1;
        Com<ID3D12Resource> shared;
        Require(device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,
                        __uuidof(ID3D12Resource),shared.AsVoid()),"Create shared HUD color");
        SharedHandle handle;
        Require(device12->CreateSharedHandle(shared.Get(),nullptr,GENERIC_ALL,nullptr,&handle.value),"Export shared HUD color");
        Com<ID3D11Texture2D> texture;
        Require(device11->OpenSharedResource1(handle.value,__uuidof(ID3D11Texture2D),texture.AsVoid()),"Import shared HUD color");
        Com<ID3D11RenderTargetView> view;
        Require(device11->CreateRenderTargetView(texture.Get(),nullptr,view.Put()),"Create shared HUD RTV");
        slot.view.Swap(view); slot.surface.Swap(shared); slot.desc=desc;
    }
    static void RecordCopy(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* allocator,
                           ID3D12Resource* game,ID3D12Resource* share,const D3D12_BOX& rect,bool before,bool msaa) {
        Require(list->Reset(allocator,nullptr),"Reset HUD area copy");
        auto* from=before?game:share; auto* to=before?share:game;
        movie_pass::Transition12(list,from,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);
        movie_pass::Transition12(list,to,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        const D3D12_TEXTURE_COPY_LOCATION src{from,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}};
        const D3D12_TEXTURE_COPY_LOCATION dst{to,D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{}};
        list->CopyTextureRegion(&dst,before?0:rect.left,before?0:rect.top,0,&src,(before&&!msaa)?&rect:nullptr);
        movie_pass::Transition12(list,to,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON);
        movie_pass::Transition12(list,from,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON);
        Require(list->Close(),"Close HUD area copy");
    }
    void MakePipeline() {
        using namespace scene_hud;
        Require(device11->CreateVertexShader(HudVertex,sizeof(HudVertex),nullptr,vs.Put()),"Create native HUD VS");
        Require(device11->CreatePixelShader(HudPixel,sizeof(HudPixel),nullptr,ps.Put()),"Create native HUD PS");
        const D3D11_INPUT_ELEMENT_DESC inputs[]{
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
        Require(device11->CreateInputLayout(inputs,2,HudVertex,sizeof(HudVertex),layout.Put()),"Create native HUD input");
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth=3072*20; desc.Usage=D3D11_USAGE_DYNAMIC; desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        Require(device11->CreateBuffer(&desc,nullptr,vertexData.Put()),"Create HUD vertex buffer");
        desc.ByteWidth=16;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        Require(device11->CreateBuffer(&desc,nullptr,viewData.Put()),"Create HUD viewport buffer");
        D3D11_BLEND_DESC equation{};
        auto& rt=equation.RenderTarget[0];rt.BlendEnable=TRUE;
        rt.SrcBlend=rt.SrcBlendAlpha=D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend=rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp=rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        Require(device11->CreateBlendState(&equation,blend.Put()),"Create HUD blend state");
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        raster.ScissorEnable=TRUE;raster.MultisampleEnable=TRUE;
        Require(device11->CreateRasterizerState(&raster,rasterizer.Put()),"Create HUD rasterizer");
        auto* vb=vertexData.Get(); auto* cb=viewData.Get();
        const UINT stride=20, offset=0;
        context11->IASetInputLayout(layout.Get()); context11->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context11->VSSetShader(vs.Get(),nullptr,0);context11->PSSetShader(ps.Get(),nullptr,0);
        context11->VSSetConstantBuffers(0,1,&cb);context11->RSSetState(rasterizer.Get());
        context11->OMSetBlendState(blend.Get(),nullptr,UINT_MAX);
    }
    Com<ID3D12Device> device12;
    Com<ID3D12CommandQueue> queue;
    Com<ID3D12Fence> fence12;
    Com<ID3D11Device5> device11;
    Com<ID3D11DeviceContext4> context11;
    Com<ID3D11Fence> fence11;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11RasterizerState> rasterizer;
    Com<ID3D11BlendState> blend;
    Com<ID3D11Buffer> vertexData,viewData;
    std::array<CommandPair,3> slots;
    UINT64 submitted=0;
};

class GameHud11 {
public:
    bool Draw(IDirect3DDevice9* legacy,const void* vertices,size_t count,size_t stride) noexcept {
        if (!legacy || !vertices || !count || count>3072 || count%3 || stride!=20) return false;
        try {
            Com<scene_hud::Interop> interop;
            Require(legacy->QueryInterface(scene_hud::InteropId,interop.AsVoid()),"Get mixed game device");
            Com<ID3D12Device> owner;
            Require(interop->GetD3D12Device(__uuidof(ID3D12Device),owner.AsVoid()),"Get game GPU");
            if(!pass || pass->Owner()!=owner.Get()) pass=std::make_unique<Native11>(owner.Get());
            Com<IDirect3DSurface9> target;
            D3DVIEWPORT9 viewport{};
            D3DSURFACE_DESC desc{};
            Require(legacy->GetRenderTarget(0,target.Put()),"Get game color");
            Require(target->GetDesc(&desc),"Get target description");
            Require(legacy->GetViewport(&viewport),"Get game viewport");
            if(!pass->Draw(interop.Get(),target.Get(),desc,viewport,vertices,UINT(count))) return false;
            ++draws;
            return true;
        } catch(const movie_pass::D3DError& error) {
            lastError=error.Code();
        } catch(...) {lastError=E_FAIL;}
        return false;
    }
    UINT64 DrawCount() const {return draws;}
    HRESULT LastError() const {return lastError;}
    void Reset() {pass.reset();}
private:
    std::unique_ptr<Native11> pass;
    UINT64 draws=0;
    HRESULT lastError=S_OK;
};
} // namespace interop_hud
