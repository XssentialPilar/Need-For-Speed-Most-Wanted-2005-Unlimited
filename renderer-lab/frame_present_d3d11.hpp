#pragma once
#include "frame_present_shaders.hpp"
#include "scene_hud_d3d12.hpp"
#include "shared_device11.hpp"

namespace frame_present {

// Transfer the completed image on the GPU to an independent D3D11 device on
// the same adapter. No mapping or CPU copies occur on the presentation path.
class Writer11 {
    struct Handle {
        HANDLE value{};
        ~Handle() { if (value) CloseHandle(value); }
    };
    template<class T> using Com = movie_pass::Com<T>;
    struct Slot {
        Com<ID3D12CommandAllocator> allocator;
        Com<ID3D12GraphicsCommandList> list;
        UINT64 recorded = 0;
    };
public:
    Writer11(ID3D12Device* game, ID3D12CommandQueue* direct) : owner(game), queue(direct) {
        using movie_pass::Require;
        movie_pass::CreatePeer11(game, device, context);
        Handle shared;
        Require(game->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), fence12.AsVoid()), "Create frame fence");
        Require(game->CreateSharedHandle(fence12.Get(), nullptr, GENERIC_ALL, nullptr, &shared.value), "Export frame fence");
        Require(device->OpenSharedFence(shared.value, __uuidof(ID3D11Fence), fence11.AsVoid()), "Open frame fence");
        event.value = CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if (!event.value) throw std::runtime_error("Create frame completion event");
        for (auto& slot : slots) {
            Require(game->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                 slot.allocator.AsVoid()), "Create frame copy allocator");
            Require(game->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,
                                            __uuidof(ID3D12GraphicsCommandList),slot.list.AsVoid()), "Create frame copy list");
            Require(slot.list->Close(), "Close initial frame list");
        }
        Require(device->CreateVertexShader(FrameVertex,sizeof(FrameVertex),nullptr,vs.Put()), "Create frame vertex shader");
        Require(device->CreatePixelShader(FramePixel,sizeof(FramePixel),nullptr,ps.Put()), "Create frame pixel shader");
        D3D11_SAMPLER_DESC sample{};
        sample.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sample.MaxAnisotropy = 1;
        sample.ComparisonFunc = D3D11_COMPARISON_NEVER;
        Require(device->CreateSamplerState(&sample,sampler.Put()), "Create frame sampler");
    }
    ~Writer11() {
        try { WaitFor(serial); } catch (const std::exception&) {}
    }
    IUnknown* SwapDevice() const { return device.Get(); }

    void Write(ID3D12Resource* src, const D3DSURFACE_DESC& legacy, IDXGISwapChain3* chain, UINT index) {
        using movie_pass::Require;
        const auto desc = src->GetDesc();
        const auto format = scene_hud::ColorFormat(legacy.Format,desc.Format);
        if (format == DXGI_FORMAT_UNKNOWN || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
            desc.DepthOrArraySize != 1 || desc.MipLevels != 1 || !desc.Width || desc.Width > UINT_MAX ||
            !desc.Height || !desc.SampleDesc.Count) throw std::runtime_error("Unsupported frame surface");
        PrepareShared(static_cast<UINT>(desc.Width),desc.Height,format);
        Com<ID3D11Texture2D> output;
        Com<ID3D11RenderTargetView> outView;
        Require(chain->GetBuffer(index,__uuidof(ID3D11Texture2D),output.AsVoid()), "Get D3D11 swap image");
        Require(device->CreateRenderTargetView(output.Get(),nullptr,outView.Put()), "Get D3D11 swap view");
        D3D11_TEXTURE2D_DESC target{};
        output->GetDesc(&target);
        Slot& slot = slots[nextSlot];
        WaitFor(slot.recorded);
        Require(slot.allocator->Reset(), "Reset frame allocator");
        Require(slot.list->Reset(slot.allocator.Get(),nullptr), "Reset frame copy");
        const bool msaa = desc.SampleDesc.Count > 1;
        const auto read = msaa ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE;
        const auto write = msaa ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_COPY_DEST;
        auto* cmd = slot.list.Get();
        movie_pass::Transition12(cmd,src,D3D12_RESOURCE_STATE_COMMON,read);
        movie_pass::Transition12(cmd,sharedImage.Get(),D3D12_RESOURCE_STATE_COMMON,write);
        if (msaa) cmd->ResolveSubresource(sharedImage.Get(),0,src,0,format);
        else cmd->CopyResource(sharedImage.Get(),src);
        movie_pass::Transition12(cmd,src,read,D3D12_RESOURCE_STATE_COMMON);
        movie_pass::Transition12(cmd,sharedImage.Get(),write,D3D12_RESOURCE_STATE_COMMON);
        Require(cmd->Close(), "Close frame copy");
        ID3D12CommandList* lists[]{cmd};
        queue->ExecuteCommandLists(1,lists);
        const UINT64 uploaded = serial + 1;
        Require(queue->Signal(fence12.Get(),uploaded), "Publish scene frame");
        slot.recorded = serial = uploaded;
        nextSlot = (nextSlot + 1) % slots.size();
        Require(context->Wait(fence11.Get(),uploaded), "Wait for scene frame");
        const D3D11_VIEWPORT vp{0,0,float(target.Width),float(target.Height),0,1};
        auto* color = outView.Get();
        auto* input = sharedView.Get();
        auto* linear = sampler.Get();
        context->RSSetViewports(1,&vp);
        context->OMSetRenderTargets(1,&color,nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0);
        context->PSSetShader(ps.Get(),nullptr,0);
        context->PSSetSamplers(0,1,&linear);
        context->PSSetShaderResources(0,1,&input);
        context->Draw(3,0);
        input = nullptr;
        context->PSSetShaderResources(0,1,&input);
        context->OMSetRenderTargets(0,nullptr,nullptr);
        const UINT64 painted = uploaded + 1;
        Require(context->Signal(fence11.Get(),painted), "Signal D3D11 frame read");
        context->Flush();
        serial = painted;
        Require(queue->Wait(fence12.Get(),painted), "Wait for shared frame reuse");
    }
private:
    void WaitFor(UINT64 value) {
        if (!value) return;
        const auto completed = fence12->GetCompletedValue();
        if (completed == UINT64_MAX) throw std::runtime_error("Frame device removed");
        if (completed >= value) return;
        movie_pass::Require(fence12->SetEventOnCompletion(value,event.value), "Wait for frame completion");
        if (WaitForSingleObject(event.value,5000) != WAIT_OBJECT_0) throw std::runtime_error("Frame completion timed out");
    }
    void PrepareShared(UINT width, UINT height, DXGI_FORMAT format) {
        if (sharedView.Get() && width == sourceWidth && height == sourceHeight && format == sourceFormat) return;
        using movie_pass::Require;
        WaitFor(serial);
        D3D12_RESOURCE_DESC tex{};
        tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        tex.Width = width; tex.Height = height; tex.Format = format;
        tex.MipLevels = tex.DepthOrArraySize = tex.SampleDesc.Count = 1;
        tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        Com<ID3D12Resource> next;
        Require(owner->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&tex,D3D12_RESOURCE_STATE_COMMON,nullptr,
                                               __uuidof(ID3D12Resource),next.AsVoid()), "Allocate shared scene frame");
        Handle share;
        Require(owner->CreateSharedHandle(next.Get(),nullptr,GENERIC_ALL,nullptr,&share.value), "Share scene frame");
        Com<ID3D11Texture2D> viewTexture;
        Require(device->OpenSharedResource1(share.value,__uuidof(ID3D11Texture2D),viewTexture.AsVoid()), "Open scene frame");
        Com<ID3D11ShaderResourceView> view;
        Require(device->CreateShaderResourceView(viewTexture.Get(),nullptr,view.Put()), "Create scene frame view");
        next.Swap(sharedImage); view.Swap(sharedView);
        sourceWidth=width; sourceHeight=height; sourceFormat=format;
    }
    Com<ID3D12Device> owner;
    Com<ID3D12CommandQueue> queue;
    Com<ID3D11Device5> device;
    Com<ID3D11DeviceContext4> context;
    Com<ID3D12Fence> fence12;
    Com<ID3D11Fence> fence11;
    Handle event;
    std::array<Slot,3> slots{};
    size_t nextSlot{};
    UINT64 serial{};
    Com<ID3D12Resource> sharedImage;
    Com<ID3D11ShaderResourceView> sharedView;
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11SamplerState> sampler;
    DXGI_FORMAT sourceFormat = DXGI_FORMAT_UNKNOWN;
    UINT sourceWidth{}, sourceHeight{};
};
} // namespace frame_present
