#pragma once

#include "movie_d3d.hpp"
#include <d3d9.h>
#include <array>
#include <memory>

namespace scene_hud {

using movie_pass::Com;
using movie_pass::Require;

#include "scene_hud_shaders.inc"

// The public d3d9on12 COM ABI. LLVM-MinGW currently omits the SDK header.
inline constexpr GUID InteropId{0xe7fda234, 0xb589, 0x4049, {0x94,0x0d,0x88,0x78,0x97,0x75,0x31,0xc8}};
struct Interop : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetD3D12Device(REFIID, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnwrapUnderlyingResource(IDirect3DResource9*, ID3D12CommandQueue*,
                                                               REFIID, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReturnUnderlyingResource(IDirect3DResource9*, UINT, UINT64*, ID3D12Fence**) = 0;
};

struct FactoryOverride {
    BOOL enabled;
    IUnknown* device;
    IUnknown* queues[2];
    UINT queueCount, nodeMask;
};

// Opt-in only. Normal driver selection stays with the caller.
inline IDirect3D9* CreateInteropFactory(HMODULE systemD3D9, UINT sdkVersion) {
    using Create = IDirect3D9* (WINAPI*)(UINT, FactoryOverride*, UINT);
    const auto create = systemD3D9 ? reinterpret_cast<Create>(
        GetProcAddress(systemD3D9, "Direct3DCreate9On12")) : nullptr;
    FactoryOverride options{};
    options.enabled = TRUE;
    return create ? create(sdkVersion, &options, 1) : nullptr;
}

inline bool SupportsDevice(IDirect3DDevice9* legacy) {
    Com<Interop> interop;
    return legacy && SUCCEEDED(legacy->QueryInterface(InteropId, interop.AsVoid()));
}

struct HudViewport {
    float x, y, width, height;
};

class TargetLease {
public:
    TargetLease(Interop* interop, IDirect3DResource9* legacy, ID3D12CommandQueue* queue)
        : interop(interop), legacy(legacy) {
        Require(interop->UnwrapUnderlyingResource(legacy, queue, __uuidof(ID3D12Resource), resource.AsVoid()),
                "Acquire HUD render target");
    }
    ~TargetLease() {
        if (checkedOut) interop->ReturnUnderlyingResource(legacy, 0, nullptr, nullptr);
    }
    HRESULT Return(UINT64 serial, ID3D12Fence* fence) {
        // Once commands have been submitted, a later failure cannot be fixed
        // by returning this surface without the corresponding synchronization.
        checkedOut = false;
        return interop->ReturnUnderlyingResource(legacy, 1, &serial, &fence);
    }
    HRESULT RetryCompleted() { return interop->ReturnUnderlyingResource(legacy, 0, nullptr, nullptr); }
    Com<ID3D12Resource> resource;
private:
    Interop* interop;
    IDirect3DResource9* legacy;
    bool checkedOut = true;
};

inline DXGI_FORMAT ColorFormat(D3DFORMAT legacy, DXGI_FORMAT actual) {
    switch (actual) {
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM: return DXGI_FORMAT_B8G8R8X8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_UNORM: return actual;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return legacy == D3DFMT_A16B16G16R16F ? actual : DXGI_FORMAT_UNKNOWN;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

class NativeHudPass {
    static constexpr UINT VertexStride = 20;
    static constexpr UINT MaxVertices = 3072;
    struct Frame {
        Com<ID3D12CommandAllocator> allocator;
        Com<ID3D12GraphicsCommandList> commands;
        Com<ID3D12Resource> upload;
        Com<ID3D12PipelineState> pipeline;
        Com<ID3D12DescriptorHeap> rtv;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        DXGI_SAMPLE_DESC samples{};
        UINT64 serial = 0;
    };

public:
    explicit NativeHudPass(ID3D12Device* owner) : device(owner) {
        const D3D12_COMMAND_QUEUE_DESC desc{D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
        Require(device->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), queue.AsVoid()), "Create native HUD queue");
        Require(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), fence.AsVoid()), "Create HUD fence");
        Require(device->CreateRootSignature(0, HudRoot, sizeof(HudRoot), __uuidof(ID3D12RootSignature),
                                            root.AsVoid()), "Create native HUD root signature");
        for (auto& frame : frames) {
            Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                   frame.allocator.AsVoid()), "Create HUD command allocator");
            Require(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frame.allocator.Get(), nullptr,
                        __uuidof(ID3D12GraphicsCommandList), frame.commands.AsVoid()), "Create HUD command list");
            Require(frame.commands->Close(), "Close initial HUD list");
            movie_pass::CreateResource12(device.Get(), movie_pass::Buffer12(MaxVertices * VertexStride),
                D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, frame.upload);
            movie_pass::MakeHeap12(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                  D3D12_DESCRIPTOR_HEAP_FLAG_NONE, frame.rtv);
        }
    }
    ~NativeHudPass() { Drain(); }

    ID3D12Device* Device() const { return device.Get(); }
    ID3D12CommandQueue* Queue() const { return queue.Get(); }
    void Drain() noexcept {
        if (serial && fence->GetCompletedValue() < serial && SUCCEEDED(device->GetDeviceRemovedReason())) {
            fence->SetEventOnCompletion(serial, nullptr);
        }
    }

    bool Draw(Interop* interop, IDirect3DSurface9* surface, const D3DSURFACE_DESC& legacy,
              const HudViewport& viewport, const void* vertices, UINT count) {
        return DrawSurface(interop, surface, legacy, [&](unsigned slot, ID3D12GraphicsCommandList* commands,
                                 DXGI_FORMAT format, DXGI_SAMPLE_DESC samples, D3D12_CPU_DESCRIPTOR_HANDLE handle) {
            auto& frame = frames[slot];
            if (!frame.pipeline.Get() || frame.format != format || frame.samples.Count != samples.Count ||
                frame.samples.Quality != samples.Quality) {
                MakePipeline(format, samples, frame.pipeline);
                frame.format = format;
                frame.samples = samples;
            }
            const auto vb = UploadGeometry(slot, vertices, count * VertexStride, VertexStride);
            const D3D12_VIEWPORT vp{viewport.x, viewport.y, viewport.width, viewport.height, 0, 1};
            const D3D12_RECT rect{LONG(vp.TopLeftX), LONG(vp.TopLeftY),
                                  LONG(vp.TopLeftX + vp.Width), LONG(vp.TopLeftY + vp.Height)};
            commands->SetPipelineState(frame.pipeline.Get());
            commands->SetGraphicsRootSignature(root.Get());
            commands->SetGraphicsRoot32BitConstants(0, 4, &viewport, 0);
            commands->IASetVertexBuffers(0, 1, &vb);
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commands->RSSetViewports(1, &vp);
            commands->RSSetScissorRects(1, &rect);
            commands->OMSetRenderTargets(1, &handle, FALSE, nullptr);
            commands->DrawInstanced(count, 1, 0, 0);
        });
    }

    D3D12_VERTEX_BUFFER_VIEW UploadGeometry(unsigned slot, const void* vertices, UINT bytes, UINT stride) {
        if (slot >= frames.size() || bytes > MaxVertices * VertexStride) throw std::invalid_argument("Invalid vertex upload");
        auto* upload = frames[slot].upload.Get();
        D3D12_RANGE nothing{};
        void* mapped = nullptr;
        Require(upload->Map(0, &nothing, &mapped), "Map game vertices");
        std::memcpy(mapped, vertices, bytes);
        const D3D12_RANGE written{0, bytes};
        upload->Unmap(0, &written);
        return {upload->GetGPUVirtualAddress(), bytes, stride};
    }

    template<class Record>
    bool DrawSurface(Interop* interop, IDirect3DSurface9* surface, const D3DSURFACE_DESC& legacy, Record record,
                     TargetLease* depth = nullptr, bool* executed = nullptr) {
        const auto completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX) return false;
        unsigned slot = 0;
        while (slot < frames.size() && frames[slot].serial > completed) ++slot;
        if (slot == frames.size()) return false;
        auto& frame = frames[slot];
        TargetLease target(interop, surface, queue.Get());
        const auto desc = target.resource->GetDesc();
        const auto format = ColorFormat(legacy.Format, desc.Format);
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize != 1 ||
            desc.MipLevels != 1 || desc.Width != legacy.Width || desc.Height != legacy.Height ||
            !(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) || format == DXGI_FORMAT_UNKNOWN) return false;
        D3D12_RENDER_TARGET_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = desc.SampleDesc.Count > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
        const auto handle = frame.rtv->GetCPUDescriptorHandleForHeapStart();
        device->CreateRenderTargetView(target.resource.Get(), &view, handle);
        Require(frame.allocator->Reset(), "Reset native game allocator");
        Require(frame.commands->Reset(frame.allocator.Get(), nullptr), "Reset native game commands");
        auto* commands = frame.commands.Get();
        try {
            movie_pass::Transition12(commands, target.resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_STATE_RENDER_TARGET);
            if (depth) movie_pass::Transition12(commands, depth->resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                                                D3D12_RESOURCE_STATE_DEPTH_WRITE);
            record(slot, commands, format, desc.SampleDesc, handle);
            if (depth) movie_pass::Transition12(commands, depth->resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                D3D12_RESOURCE_STATE_COMMON);
            movie_pass::Transition12(commands, target.resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                     D3D12_RESOURCE_STATE_COMMON);
        } catch (...) {
            commands->Close();
            throw;
        }
        Require(commands->Close(), "Close native game draw");
        ID3D12CommandList* lists[]{commands};
        queue->ExecuteCommandLists(1, lists);
        if (executed) *executed = true;
        const UINT64 next = serial + 1;
        Require(queue->Signal(fence.Get(), next), "Signal native game work");
        frame.serial = serial = next;
        if (depth && FAILED(depth->Return(serial, fence.Get()))) {
            Drain();
            depth->RetryCompleted();
        }
        const HRESULT returned = target.Return(serial, fence.Get());
        if (FAILED(returned)) {
            // If the runtime refused the fence, finish our queue's work before
            // attempting a synchronous check-in. Keep the submitted draw singular.
            Drain();
            Require(interop->ReturnUnderlyingResource(surface, 0, nullptr, nullptr), "Return completed game render target");
        }
        return true;
    }

private:
    void MakePipeline(DXGI_FORMAT format, DXGI_SAMPLE_DESC samples, Com<ID3D12PipelineState>& result) {
        const D3D12_INPUT_ELEMENT_DESC layout[]{
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,16,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get();
        desc.VS = {HudVertex,sizeof(HudVertex)};
        desc.PS = {HudPixel,sizeof(HudPixel)};
        desc.InputLayout = {layout,2};
        auto& blend = desc.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.SampleMask = ~UINT{0};
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        desc.RasterizerState.MultisampleEnable = samples.Count > 1;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        desc.DepthStencilState.FrontFace.StencilFailOp = desc.DepthStencilState.FrontFace.StencilDepthFailOp =
            desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
        desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = format;
        desc.SampleDesc = samples;
        Require(device->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), result.AsVoid()),
                "Create native HUD pipeline");
    }

    Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;
    Com<ID3D12Fence> fence;
    Com<ID3D12RootSignature> root;
    std::array<Frame,3> frames;
    UINT64 serial = 0;
};

// This is called at the same FE point as the D3D9 instruments. It gets the
// active game color surface every time, including after a reset or resize.
class GameHud {
public:
    bool Draw(IDirect3DDevice9* legacy, const void* vertices, size_t size, size_t stride) noexcept {
        if (!legacy || !vertices || !size || size % 3 || size > 3072 || stride != 20) return false;
        Com<Interop> interop;
        if (FAILED(legacy->QueryInterface(InteropId, interop.AsVoid()))) return false;
        Com<IDirect3DSurface9> color;
        D3DVIEWPORT9 viewport{};
        D3DSURFACE_DESC desc{};
        if (FAILED(legacy->GetRenderTarget(0, color.Put())) || FAILED(color->GetDesc(&desc)) ||
            FAILED(legacy->GetViewport(&viewport)) || !viewport.Width || !viewport.Height) return false;
        try {
            Com<ID3D12Device> current;
            Require(interop->GetD3D12Device(__uuidof(ID3D12Device), current.AsVoid()), "Get game D3D12 device");
            if (!pass || pass->Device() != current.Get()) pass = std::make_unique<NativeHudPass>(current.Get());
            const HudViewport area{float(viewport.X),float(viewport.Y),float(viewport.Width),float(viewport.Height)};
            const bool drawn = pass->Draw(interop.Get(), color.Get(), desc, area, vertices, UINT(size));
            if (drawn) ++drawCount;
            return drawn;
        } catch (const movie_pass::D3DError& error) {
            lastError = error.Code();
            return false;
        } catch (...) {
            lastError = E_FAIL;
            return false;
        }
    }
    UINT64 DrawCount() const { return drawCount; }
    HRESULT LastError() const { return lastError; }
    void Reset() { pass.reset(); }
private:
    std::unique_ptr<NativeHudPass> pass;
    UINT64 drawCount = 0;
    HRESULT lastError = S_OK;
};

} // namespace scene_hud
