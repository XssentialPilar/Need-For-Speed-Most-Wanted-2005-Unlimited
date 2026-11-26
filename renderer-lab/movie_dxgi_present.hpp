#pragma once

#include "movie_d3d.hpp"
#include "movie_presentation.hpp"
#include <dxgi1_4.h>

#include <array>
#include <memory>

namespace movie_pass {

inline bool IsDeviceLost(HRESULT result) {
    return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
           result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

inline void CreateMovieSwapChain(IDXGIAdapter1* adapter, IUnknown* deviceOrQueue, HWND window,
                                 Extent extent, Com<IDXGISwapChain1>& result) {
    Com<IDXGIFactory2> factory;
    Require(adapter->GetParent(__uuidof(IDXGIFactory2), factory.AsVoid()), "Get device DXGI factory");
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = extent.width;
    desc.Height = extent.height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    Require(factory->CreateSwapChainForHwnd(deviceOrQueue, window, &desc, nullptr, nullptr, result.Put()),
            "Create HWND flip swap chain");
    Require(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "Disable DXGI Alt+Enter");
}

class D3D11MoviePresenter {
public:
    D3D11MoviePresenter(HWND window, IDXGIAdapter1* adapter, ID3DBlob* vertex, ID3DBlob* pixel)
        : window(window), adapter(adapter), vertex(vertex), pixel(pixel) {
        if (!window || !adapter || !vertex || !pixel) throw std::invalid_argument("Missing D3D11 presentation input");
    }

    ~D3D11MoviePresenter() { ResetDevice(); }

    MoviePresentation Draw(Frame frame) {
        frameReady = false;
        const auto target = ClientExtent(window);
        if (!target.width || !target.height) return MoviePresentation::Suspended;
        OutputSize(frame, target);
        try {
            if (!pass) Initialize(target);
            UpdateBackBuffer(target);
            pass->Draw(context.Get(), frame, colorView.Get(), extent);
        } catch (const D3DError& error) {
            if (!IsDeviceLost(error.Code())) throw;
            ResetDevice();
            return MoviePresentation::DeviceLost;
        }
        frameReady = true;
        return MoviePresentation::Drawn;
    }

    MoviePresentation Present(bool vsync) {
        if (!frameReady) throw std::logic_error("No rendered D3D11 frame to present");
        frameReady = false;
        const auto status = swapChain->Present(vsync ? 1 : 0, 0);
        if (IsDeviceLost(status)) {
            ResetDevice();
            return MoviePresentation::DeviceLost;
        }
        Require(status, "Present D3D11 movie");
        occluded = status == DXGI_STATUS_OCCLUDED;
        return occluded ? MoviePresentation::Occluded : MoviePresentation::Presented;
    }

    MoviePresentation Submit(Frame frame, bool vsync) {
        if (occluded) {
            const auto status = swapChain->Present(0, DXGI_PRESENT_TEST);
            if (IsDeviceLost(status)) {
                ResetDevice();
                return MoviePresentation::DeviceLost;
            }
            Require(status, "Test D3D11 movie visibility");
            if (status == DXGI_STATUS_OCCLUDED) return MoviePresentation::Occluded;
            occluded = false;
        }
        const auto result = Draw(frame);
        return result == MoviePresentation::Drawn ? Present(vsync) : result;
    }

    Pixels ReadBack() const {
        if (!frameReady) throw std::logic_error("D3D11 readback requires a rendered frame before Present");
        return ReadBackD3D11(device.Get(), context.Get(), color.Get());
    }

    Extent BackBufferExtent() const { return extent; }

    void ResetDevice() {
        frameReady = occluded = false;
        if (context.Get()) context->ClearState();
        pass.reset();
        colorView.Reset();
        color.Reset();
        swapChain.Reset();
        if (context.Get()) context->Flush();
        context.Reset();
        device.Reset();
        extent = {};
    }

private:
    void Initialize(Extent target) {
        try {
            Require(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                      D3D11_SDK_VERSION, device.Put(), nullptr, context.Put()),
                    "Create D3D11 presentation device");
            CreateMovieSwapChain(adapter.Get(), device.Get(), window, target, swapChain);
            extent = target;
            pass = std::make_unique<D3D11MoviePass>(device.Get(), vertex.Get(), pixel.Get());
        } catch (...) {
            ResetDevice();
            throw;
        }
    }

    void UpdateBackBuffer(Extent target) {
        if (target.width != extent.width || target.height != extent.height) {
            context->OMSetRenderTargets(0, nullptr, nullptr);
            colorView.Reset();
            color.Reset();
            context->Flush();
            Require(swapChain->ResizeBuffers(0, target.width, target.height, DXGI_FORMAT_UNKNOWN, 0),
                    "Resize D3D11 movie back buffers");
            extent = target;
        }
        if (!colorView.Get()) {
            Require(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), color.AsVoid()), "Get D3D11 back buffer");
            Require(device->CreateRenderTargetView(color.Get(), nullptr, colorView.Put()), "Create D3D11 back buffer view");
        }
    }

    HWND window;
    Com<IDXGIAdapter1> adapter;
    Com<ID3DBlob> vertex, pixel;
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<IDXGISwapChain1> swapChain;
    Com<ID3D11Texture2D> color;
    Com<ID3D11RenderTargetView> colorView;
    std::unique_ptr<D3D11MoviePass> pass;
    Extent extent{};
    bool frameReady = false;
    bool occluded = false;
};

class D3D12MoviePresenter {
public:
    D3D12MoviePresenter(HWND window, IDXGIAdapter1* adapter, ID3DBlob* vertex, ID3DBlob* pixel)
        : window(window), adapter(adapter), vertex(vertex), pixel(pixel) {
        if (!window || !adapter || !vertex || !pixel) throw std::invalid_argument("Missing D3D12 presentation input");
    }

    ~D3D12MoviePresenter() { ResetDevice(); }

    MoviePresentation Draw(Frame frame) {
        frameReady = false;
        const auto target = ClientExtent(window);
        if (!target.width || !target.height) return MoviePresentation::Suspended;
        OutputSize(frame, target);
        try {
            if (!pipeline.Get()) Initialize(target);
            UpdateBackBuffers(target);
            PrepareInput(frame.size);
            bufferIndex = swapChain->GetCurrentBackBufferIndex();
            auto& current = buffers[bufferIndex];
            Wait(current.lastSubmit);
            Upload(frame, current);

            Require(current.allocator->Reset(), "Reset D3D12 movie frame allocator");
            Require(commands->Reset(current.allocator.Get(), pipeline.Get()), "Reset D3D12 movie commands");
            if (inputReadable) {
                Transition12(commands.Get(), input.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                             D3D12_RESOURCE_STATE_COPY_DEST);
            }
            TransferTexture12(commands.Get(), input.Get(), current.upload.Get(), uploadLayout, true);
            Transition12(commands.Get(), input.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            Transition12(commands.Get(), current.color.Get(), D3D12_RESOURCE_STATE_PRESENT,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);
            DrawMovie12(commands.Get(), root.Get(), shaderHeap.Get(), TargetView(bufferIndex), frame.size, extent);
            Transition12(commands.Get(), current.color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_PRESENT);
            Execute(commands.Get());
            inputReadable = true;
            current.lastSubmit = Signal();
        } catch (const D3DError& error) {
            if (!IsDeviceLost(error.Code())) throw;
            ResetDevice();
            return MoviePresentation::DeviceLost;
        }
        frameReady = true;
        return MoviePresentation::Drawn;
    }

    MoviePresentation Present(bool vsync) {
        if (!frameReady) throw std::logic_error("No rendered D3D12 frame to present");
        frameReady = false;
        const auto status = swapChain->Present(vsync ? 1 : 0, 0);
        if (IsDeviceLost(status)) {
            ResetDevice();
            return MoviePresentation::DeviceLost;
        }
        Require(status, "Present D3D12 movie");
        occluded = status == DXGI_STATUS_OCCLUDED;
        return occluded ? MoviePresentation::Occluded : MoviePresentation::Presented;
    }

    MoviePresentation Submit(Frame frame, bool vsync) {
        if (occluded) {
            const auto status = swapChain->Present(0, DXGI_PRESENT_TEST);
            if (IsDeviceLost(status)) {
                ResetDevice();
                return MoviePresentation::DeviceLost;
            }
            Require(status, "Test D3D12 movie visibility");
            if (status == DXGI_STATUS_OCCLUDED) return MoviePresentation::Occluded;
            occluded = false;
        }
        const auto result = Draw(frame);
        return result == MoviePresentation::Drawn ? Present(vsync) : result;
    }

    Pixels ReadBack() {
        if (!frameReady) throw std::logic_error("D3D12 readback requires a rendered frame before Present");
        auto& current = buffers[bufferIndex];
        const auto desc = current.color->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT readLayout{};
        UINT64 readSize = 0;
        device->GetCopyableFootprints(&desc, 0, 1, 0, &readLayout, nullptr, nullptr, &readSize);
        Com<ID3D12Resource> readback;
        CreateResource12(device.Get(), Buffer12(readSize), D3D12_HEAP_TYPE_READBACK,
                         D3D12_RESOURCE_STATE_COPY_DEST, readback);
        Wait(current.lastSubmit);
        Require(current.allocator->Reset(), "Reset D3D12 movie readback allocator");
        Require(commands->Reset(current.allocator.Get(), nullptr), "Reset D3D12 movie readback commands");
        Transition12(commands.Get(), current.color.Get(), D3D12_RESOURCE_STATE_PRESENT,
                     D3D12_RESOURCE_STATE_COPY_SOURCE);
        TransferTexture12(commands.Get(), current.color.Get(), readback.Get(), readLayout, false);
        Transition12(commands.Get(), current.color.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                     D3D12_RESOURCE_STATE_PRESENT);
        Execute(commands.Get());
        current.lastSubmit = Signal();
        Wait(current.lastSubmit);
        Pixels pixels(std::size_t(extent.width) * extent.height * 4);
        const D3D12_RANGE range{0, static_cast<SIZE_T>(readSize)};
        void* mapped = nullptr;
        Require(readback->Map(0, &range, &mapped), "Map D3D12 presentation readback");
        for (unsigned row = 0; row < extent.height; ++row) {
            std::memcpy(pixels.data() + std::size_t(row) * extent.width * 4, static_cast<const std::uint8_t*>(mapped) +
                        readLayout.Offset + std::size_t(row) * readLayout.Footprint.RowPitch, extent.width * 4);
        }
        constexpr D3D12_RANGE nothing{0, 0};
        readback->Unmap(0, &nothing);
        return pixels;
    }

    Extent BackBufferExtent() const { return extent; }

    void ResetDevice() noexcept {
        if (fence.Get() && device.Get() && SUCCEEDED(device->GetDeviceRemovedReason())) {
            try { Wait(fenceValue); } catch (const D3DError&) {}
        }
        commands.Reset();
        for (auto& buffer : buffers) {
            buffer.color.Reset();
            buffer.upload.Reset();
            buffer.allocator.Reset();
            buffer.lastSubmit = 0;
        }
        input.Reset();
        pipeline.Reset();
        root.Reset();
        shaderHeap.Reset();
        targets.Reset();
        swapChain.Reset();
        queue.Reset();
        fence.Reset();
        device.Reset();
        extent = inputSize = {};
        fenceValue = 0;
        frameReady = occluded = inputReadable = false;
    }

private:
    struct Buffer {
        Com<ID3D12Resource> color, upload;
        Com<ID3D12CommandAllocator> allocator;
        UINT64 lastSubmit = 0;
    };

    void Initialize(Extent target) {
        try {
            Require(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), device.AsVoid()),
                    "Create D3D12 presentation device");
            D3D12_COMMAND_QUEUE_DESC desc{};
            desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            Require(device->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), queue.AsVoid()),
                    "Create D3D12 presentation queue");
            Com<IDXGISwapChain1> flip;
            CreateMovieSwapChain(adapter.Get(), queue.Get(), window, target, flip);
            Require(flip->QueryInterface(__uuidof(IDXGISwapChain3), swapChain.AsVoid()), "Query D3D12 swap chain");
            Require(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), fence.AsVoid()),
                    "Create D3D12 presentation fence");
            for (auto& buffer : buffers) {
                Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                        __uuidof(ID3D12CommandAllocator), buffer.allocator.AsVoid()), "Create D3D12 frame allocator");
            }
            Require(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, buffers[0].allocator.Get(), nullptr,
                        __uuidof(ID3D12GraphicsCommandList), commands.AsVoid()), "Create D3D12 presentation commands");
            Require(commands->Close(), "Close initial D3D12 presentation commands");
            MakeHeap12(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, targets,
                       static_cast<UINT>(buffers.size()));
            MakeHeap12(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                       D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, shaderHeap);
            descriptorIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            MakePipeline12(device.Get(), vertex.Get(), pixel.Get(), root, pipeline);
            extent = target;
        } catch (...) {
            ResetDevice();
            throw;
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE TargetView(UINT index) const {
        auto view = targets->GetCPUDescriptorHandleForHeapStart();
        view.ptr += std::size_t(index) * descriptorIncrement;
        return view;
    }

    void UpdateBackBuffers(Extent target) {
        if (target.width != extent.width || target.height != extent.height) {
            Wait(fenceValue);
            for (auto& buffer : buffers) buffer.color.Reset();
            Require(swapChain->ResizeBuffers(0, target.width, target.height, DXGI_FORMAT_UNKNOWN, 0),
                    "Resize D3D12 movie back buffers");
            extent = target;
        }
        for (UINT index = 0; index < buffers.size(); ++index) {
            auto& buffer = buffers[index];
            if (buffer.color.Get()) continue;
            Require(swapChain->GetBuffer(index, __uuidof(ID3D12Resource), buffer.color.AsVoid()), "Get D3D12 back buffer");
            device->CreateRenderTargetView(buffer.color.Get(), nullptr, TargetView(index));
        }
    }

    void PrepareInput(Extent target) {
        if (inputSize.width == target.width && inputSize.height == target.height) return;
        Wait(fenceValue);
        const auto desc = Texture12(target, DXGI_FORMAT_B8G8R8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
        Com<ID3D12Resource> next;
        CreateResource12(device.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, next);
        device->GetCopyableFootprints(&desc, 0, 1, 0, &uploadLayout, nullptr, nullptr, &uploadSize);
        device->CreateShaderResourceView(next.Get(), nullptr, shaderHeap->GetCPUDescriptorHandleForHeapStart());
        input.Swap(next);
        for (auto& buffer : buffers) buffer.upload.Reset();
        inputReadable = false;
        inputSize = target;
    }

    void Upload(Frame frame, Buffer& target) {
        if (!target.upload.Get()) {
            CreateResource12(device.Get(), Buffer12(uploadSize), D3D12_HEAP_TYPE_UPLOAD,
                             D3D12_RESOURCE_STATE_GENERIC_READ, target.upload);
        }
        constexpr D3D12_RANGE nothing{0, 0};
        void* mapped = nullptr;
        Require(target.upload->Map(0, &nothing, &mapped), "Map D3D12 movie upload");
        for (unsigned row = 0; row < frame.size.height; ++row) {
            std::memcpy(static_cast<std::uint8_t*>(mapped) + uploadLayout.Offset +
                        std::size_t(row) * uploadLayout.Footprint.RowPitch,
                        frame.bgra + std::size_t(row) * frame.rowBytes, frame.size.width * 4);
        }
        target.upload->Unmap(0, nullptr);
    }

    void Execute(ID3D12GraphicsCommandList* list) {
        Require(list->Close(), "Close D3D12 presentation commands");
        ID3D12CommandList* work[]{list};
        queue->ExecuteCommandLists(1, work);
    }

    UINT64 Signal() {
        const auto next = fenceValue + 1;
        Require(queue->Signal(fence.Get(), next), "Signal D3D12 presentation fence");
        return fenceValue = next;
    }

    void Wait(UINT64 value) {
        if (!value) return;
        const auto completed = fence->GetCompletedValue();
        if (completed == std::numeric_limits<UINT64>::max()) Require(DXGI_ERROR_DEVICE_REMOVED, "D3D12 fence device");
        if (completed < value) Require(fence->SetEventOnCompletion(value, nullptr), "Wait D3D12 presentation fence");
        if (fence->GetCompletedValue() == std::numeric_limits<UINT64>::max()) {
            Require(DXGI_ERROR_DEVICE_REMOVED, "D3D12 fence device");
        }
    }

    HWND window;
    Com<IDXGIAdapter1> adapter;
    Com<ID3DBlob> vertex, pixel;
    Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;
    Com<IDXGISwapChain3> swapChain;
    Com<ID3D12Fence> fence;
    std::array<Buffer, 2> buffers;
    Com<ID3D12GraphicsCommandList> commands;
    Com<ID3D12DescriptorHeap> targets, shaderHeap;
    Com<ID3D12RootSignature> root;
    Com<ID3D12PipelineState> pipeline;
    Com<ID3D12Resource> input;
    Extent extent{}, inputSize{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT uploadLayout{};
    UINT64 uploadSize = 0, fenceValue = 0;
    UINT bufferIndex = 0, descriptorIncrement = 0;
    bool frameReady = false, occluded = false, inputReadable = false;
};

} // namespace movie_pass
