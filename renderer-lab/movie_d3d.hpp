#pragma once

#include "movie_pass.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace movie_pass {

class D3DError : public std::runtime_error {
public:
    D3DError(HRESULT code, const char* message) : std::runtime_error(message), code(code) {}
    HRESULT Code() const { return code; }

private:
    HRESULT code;
};

inline void Require(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) return;
    char error[128]{};
    std::snprintf(error, sizeof(error), "%s returned 0x%08lx", operation, static_cast<unsigned long>(result));
    throw D3DError(result, error);
}

template <typename T>
class Com {
public:
    Com() = default;
    explicit Com(T* borrowed) : pointer(borrowed) { if (pointer) pointer->AddRef(); }
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { Reset(); }
    T* Get() const { return pointer; }
    T* operator->() const { return pointer; }
    T** Put() { Reset(); return &pointer; }
    void** AsVoid() { return reinterpret_cast<void**>(Put()); }
    void Reset() { if (const auto previous = std::exchange(pointer, nullptr)) previous->Release(); }
    void Swap(Com& other) { std::swap(pointer, other.pointer); }
private:
    T* pointer = nullptr;
};

inline void CompileShader(const char* entry, const char* target, Com<ID3DBlob>& shader) {
    constexpr char program[] = R"(
        struct Vertex {
            float4 position : SV_Position;
            float2 uv : TEXCOORD;
        };
        Vertex VertexMain(uint index : SV_VertexID) {
            Vertex v;
            v.uv = float2((index << 1) & 2, index & 2);
            v.position = float4(v.uv.x * 2 - 1, 1 - v.uv.y * 2, 0, 1);
            return v;
        }
        Texture2D frame : register(t0);
        SamplerState pointClamp : register(s0);
        float4 PixelMain(Vertex vertex) : SV_Target {
            return frame.Sample(pointClamp, vertex.uv);
        }
    )";
    Com<ID3DBlob> errors;
    const auto result = D3DCompile(program, sizeof(program) - 1, nullptr, nullptr, nullptr,
                     entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, shader.Put(), errors.Put());
    if (FAILED(result) && errors.Get()) {
        throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    }
    Require(result, "D3DCompile");
}

class D3D11MoviePass {
public:
    D3D11MoviePass(ID3D11Device* owner, ID3DBlob* vertexCode, ID3DBlob* pixelCode) : device(owner) {
        Require(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr,
                                           vertex.Put()), "Create movie VS");
        Require(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr,
                                          pixel.Put()), "Create movie PS");
        D3D11_SAMPLER_DESC sample{};
        sample.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sample.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sample.MaxLOD = std::numeric_limits<float>::max();
        Require(device->CreateSamplerState(&sample, sampler.Put()), "Create movie sampler");
        D3D11_RASTERIZER_DESC raster{};
        raster.CullMode = D3D11_CULL_NONE;
        raster.FillMode = D3D11_FILL_SOLID;
        raster.DepthClipEnable = TRUE;
        Require(device->CreateRasterizerState(&raster, rasterizer.Put()), "Create rasterizer");
    }

    void Draw(ID3D11DeviceContext* context, Frame frame, ID3D11RenderTargetView* attachment, Extent target) {
        OutputSize(frame, target);
        Upload(context, frame);
        const auto rect = Fit(frame.size, target);
        const D3D11_VIEWPORT viewport{float(rect.left), float(rect.top), float(rect.size.width),
                                      float(rect.size.height), 0.0f, 1.0f};
        ID3D11ShaderResourceView* image = srv.Get();
        ID3D11SamplerState* boundSampler = sampler.Get();
        constexpr float black[]{0, 0, 0, 1};
        context->ClearRenderTargetView(attachment, black);
        context->OMSetRenderTargets(1, &attachment, nullptr);
        context->RSSetState(rasterizer.Get());
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(), nullptr, 0);
        context->PSSetShader(pixel.Get(), nullptr, 0);
        context->PSSetShaderResources(0, 1, &image);
        context->PSSetSamplers(0, 1, &boundSampler);
        context->Draw(3, 0);
        image = nullptr;
        context->PSSetShaderResources(0, 1, &image);
    }

private:
    void Upload(ID3D11DeviceContext* context, Frame frame) {
        if (inputSize.width != frame.size.width || inputSize.height != frame.size.height) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = frame.size.width;
            desc.Height = frame.size.height;
            desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            Com<ID3D11Texture2D> nextInput;
            Com<ID3D11ShaderResourceView> nextView;
            Require(device->CreateTexture2D(&desc, nullptr, nextInput.Put()), "Create movie input");
            Require(device->CreateShaderResourceView(nextInput.Get(), nullptr, nextView.Put()), "Create movie SRV");
            input.Swap(nextInput);
            srv.Swap(nextView);
            inputSize = frame.size;
        }
        D3D11_MAPPED_SUBRESOURCE map{};
        Require(context->Map(input.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map), "Map movie input");
        for (unsigned row = 0; row < frame.size.height; ++row) {
            std::memcpy(static_cast<std::uint8_t*>(map.pData) + std::size_t(row) * map.RowPitch,
                        frame.bgra + std::size_t(row) * frame.rowBytes, frame.size.width * 4);
        }
        context->Unmap(input.Get(), 0);
    }

    Com<ID3D11Device> device;
    Extent inputSize{};
    Com<ID3D11Texture2D> input;
    Com<ID3D11ShaderResourceView> srv;
    Com<ID3D11VertexShader> vertex;
    Com<ID3D11PixelShader> pixel;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11RasterizerState> rasterizer;
};

inline Pixels ReadBackD3D11(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* color) {
    D3D11_TEXTURE2D_DESC desc{};
    color->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Com<ID3D11Texture2D> readback;
    Require(device->CreateTexture2D(&desc, nullptr, readback.Put()), "Create readback");
    context->CopyResource(readback.Get(), color);
    Pixels output(std::size_t(desc.Width) * desc.Height * 4);
    D3D11_MAPPED_SUBRESOURCE map{};
    Require(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &map), "Map readback");
    for (unsigned row = 0; row < desc.Height; ++row) {
        std::memcpy(output.data() + std::size_t(row) * desc.Width * 4,
                    static_cast<const std::uint8_t*>(map.pData) + std::size_t(row) * map.RowPitch, desc.Width * 4);
    }
    context->Unmap(readback.Get(), 0);
    return output;
}

inline Pixels RenderD3D11(IDXGIAdapter1* adapter, Frame frame, Extent target,
                          ID3DBlob* vertexCode, ID3DBlob* pixelCode) {
    OutputSize(frame, target);
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Require(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                             D3D11_SDK_VERSION, device.Put(), nullptr, context.Put()), "D3D11CreateDevice");
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = target.width;
    desc.Height = target.height;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    Com<ID3D11Texture2D> color;
    Com<ID3D11RenderTargetView> rtv;
    Require(device->CreateTexture2D(&desc, nullptr, color.Put()), "Create color target");
    Require(device->CreateRenderTargetView(color.Get(), nullptr, rtv.Put()), "Create color RTV");
    D3D11MoviePass movies(device.Get(), vertexCode, pixelCode);
    movies.Draw(context.Get(), frame, rtv.Get(), target);
    return ReadBackD3D11(device.Get(), context.Get(), color.Get());
}

inline D3D12_RESOURCE_DESC Texture12(Extent size, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = size.width;
    desc.Height = size.height;
    desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Format = format;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return desc;
}

inline D3D12_RESOURCE_DESC Buffer12(UINT64 size) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

inline void CreateResource12(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc,
                             D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state, Com<ID3D12Resource>& resource) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    Require(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                             __uuidof(ID3D12Resource), resource.AsVoid()), "Create D3D12 resource");
}

inline void MakeHeap12(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                       D3D12_DESCRIPTOR_HEAP_FLAGS flags, Com<ID3D12DescriptorHeap>& heap, UINT count = 1) {
    const D3D12_DESCRIPTOR_HEAP_DESC desc{type, count, flags, 0};
    Require(device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), heap.AsVoid()),
            "Create D3D12 descriptor heap");
}

inline void MakePipeline12(ID3D12Device* device, ID3DBlob* vertex, ID3DBlob* pixel,
                           Com<ID3D12RootSignature>& signature, Com<ID3D12PipelineState>& pipeline) {
    const D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.DescriptorTable = {1, &range};
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = std::numeric_limits<float>::max();
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    const D3D12_ROOT_SIGNATURE_DESC root{1, &parameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    Com<ID3DBlob> data;
    Com<ID3DBlob> errors;
    Require(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, data.Put(), errors.Put()),
            "Serialize movie root signature");
    Require(device->CreateRootSignature(0, data->GetBufferPointer(), data->GetBufferSize(),
                                         __uuidof(ID3D12RootSignature), signature.AsVoid()),
            "Create movie root signature");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = signature.Get();
    desc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    auto& blend = desc.BlendState.RenderTarget[0];
    blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = ~UINT{0};
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.RasterizerState.DepthClipEnable = TRUE;
    desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.DepthStencilState.FrontFace.StencilFunc = desc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    desc.DepthStencilState.FrontFace.StencilFailOp = desc.DepthStencilState.FrontFace.StencilDepthFailOp =
        desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = desc.SampleDesc.Count = 1;
    desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    Require(device->CreateGraphicsPipelineState(&desc, __uuidof(ID3D12PipelineState), pipeline.AsVoid()),
            "Create D3D12 movie pipeline state");
}

inline void Transition12(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                         D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to};
    list->ResourceBarrier(1, &barrier);
}

inline void TransferTexture12(ID3D12GraphicsCommandList* list, ID3D12Resource* texture, ID3D12Resource* buffer,
                              D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint, bool upload) {
    D3D12_TEXTURE_COPY_LOCATION image{};
    image.pResource = texture;
    image.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION memory{};
    memory.pResource = buffer;
    memory.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    memory.PlacedFootprint = footprint;
    const auto* destination = upload ? &image : &memory;
    const auto* source = upload ? &memory : &image;
    list->CopyTextureRegion(destination, 0, 0, 0, source, nullptr);
}

inline void DrawMovie12(ID3D12GraphicsCommandList* list, ID3D12RootSignature* signature,
                        ID3D12DescriptorHeap* shaderHeap, D3D12_CPU_DESCRIPTOR_HANDLE targetView,
                        Extent source, Extent target) {
    const auto rect = Fit(source, target);
    const D3D12_VIEWPORT viewport{float(rect.left), float(rect.top), float(rect.size.width),
                                  float(rect.size.height), 0.0f, 1.0f};
    const D3D12_RECT clip{0, 0, LONG(target.width), LONG(target.height)};
    constexpr float black[]{0, 0, 0, 1};
    list->ClearRenderTargetView(targetView, black, 0, nullptr);
    list->OMSetRenderTargets(1, &targetView, FALSE, nullptr);
    list->SetGraphicsRootSignature(signature);
    ID3D12DescriptorHeap* heaps[]{shaderHeap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootDescriptorTable(0, shaderHeap->GetGPUDescriptorHandleForHeapStart());
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &clip);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
}

inline Pixels RenderD3D12(IDXGIAdapter1* adapter, Frame frame, Extent target,
                         ID3DBlob* vertex, ID3DBlob* pixel) {
    const auto outputSize = OutputSize(frame, target);
    Com<ID3D12Device> device;
    Require(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), device.AsVoid()),
            "D3D12CreateDevice");
    Com<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Require(device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), queue.AsVoid()), "Create D3D12 queue");
    Com<ID3D12CommandAllocator> allocator;
    Require(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                            allocator.AsVoid()), "Create D3D12 command allocator");

    const auto inputDesc = Texture12(frame.size, DXGI_FORMAT_B8G8R8A8_UNORM, D3D12_RESOURCE_FLAG_NONE);
    const auto colorDesc = Texture12(target, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    Com<ID3D12Resource> input, color, staging, readback;
    CreateResource12(device.Get(), inputDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, input);
    CreateResource12(device.Get(), colorDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RENDER_TARGET, color);
    UINT64 uploadSize = 0;
    UINT64 resultSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT uploadLayout{}, resultLayout{};
    device->GetCopyableFootprints(&inputDesc, 0, 1, 0, &uploadLayout, nullptr, nullptr, &uploadSize);
    device->GetCopyableFootprints(&colorDesc, 0, 1, 0, &resultLayout, nullptr, nullptr, &resultSize);
    CreateResource12(device.Get(), Buffer12(uploadSize), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, staging);
    CreateResource12(device.Get(), Buffer12(resultSize), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, readback);
    const D3D12_RANGE empty{0, 0};
    void* mapped = nullptr;
    Require(staging->Map(0, &empty, &mapped), "Map D3D12 upload");
    for (unsigned row = 0; row < frame.size.height; ++row) {
        std::memcpy(static_cast<std::uint8_t*>(mapped) + uploadLayout.Offset + std::size_t(row) * uploadLayout.Footprint.RowPitch,
                    frame.bgra + std::size_t(row) * frame.rowBytes, frame.size.width * 4);
    }
    staging->Unmap(0, nullptr);

    Com<ID3D12DescriptorHeap> shaderHeap, targetHeap;
    MakeHeap12(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, shaderHeap);
    MakeHeap12(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, targetHeap);
    device->CreateShaderResourceView(input.Get(), nullptr, shaderHeap->GetCPUDescriptorHandleForHeapStart());
    const auto rtv = targetHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(color.Get(), nullptr, rtv);
    Com<ID3D12RootSignature> signature;
    Com<ID3D12PipelineState> pipeline;
    MakePipeline12(device.Get(), vertex, pixel, signature, pipeline);
    Com<ID3D12GraphicsCommandList> list;
    Require(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), pipeline.Get(),
             __uuidof(ID3D12GraphicsCommandList), list.AsVoid()), "Create D3D12 command list");
    TransferTexture12(list.Get(), input.Get(), staging.Get(), uploadLayout, true);
    Transition12(list.Get(), input.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    DrawMovie12(list.Get(), signature.Get(), shaderHeap.Get(), rtv, frame.size, target);
    Transition12(list.Get(), color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    TransferTexture12(list.Get(), color.Get(), readback.Get(), resultLayout, false);
    Require(list->Close(), "Close D3D12 command list");

    Com<ID3D12Fence> fence;
    Require(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), fence.AsVoid()), "Create D3D12 fence");
    ID3D12CommandList* commands[]{list.Get()};
    queue->ExecuteCommandLists(1, commands);
    Require(queue->Signal(fence.Get(), 1), "Signal D3D12 fence");
    Require(fence->SetEventOnCompletion(1, nullptr), "Wait for D3D12 fence");
    const D3D12_RANGE range{0, static_cast<SIZE_T>(resultSize)};
    Pixels output(outputSize);
    Require(readback->Map(0, &range, &mapped), "Map D3D12 readback");
    for (unsigned row = 0; row < target.height; ++row) {
        std::memcpy(output.data() + std::size_t(row) * target.width * 4, static_cast<const std::uint8_t*>(mapped) +
                    resultLayout.Offset + std::size_t(row) * resultLayout.Footprint.RowPitch, target.width * 4);
    }
    readback->Unmap(0, &empty);
    return output;
}

} // namespace movie_pass
