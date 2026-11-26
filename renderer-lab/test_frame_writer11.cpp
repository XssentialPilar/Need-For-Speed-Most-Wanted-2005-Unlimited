#include "frame_present_d3d11.hpp"
#include <cstdio>

using movie_pass::Com;
using movie_pass::Require;

namespace {
void Run(IDXGIFactory4* factory, IDXGIAdapter1* adapter, HWND window, unsigned samples) {
    Com<ID3D12Device> gpu;
    Require(D3D12CreateDevice(adapter,D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),gpu.AsVoid()), "Create game GPU");
    Com<ID3D12CommandQueue> direct;
    const D3D12_COMMAND_QUEUE_DESC queue{D3D12_COMMAND_LIST_TYPE_DIRECT,0,D3D12_COMMAND_QUEUE_FLAG_NONE,0};
    Require(gpu->CreateCommandQueue(&queue,__uuidof(ID3D12CommandQueue),direct.AsVoid()), "Create test queue");
    frame_present::Writer11 writer(gpu.Get(),direct.Get());
    DXGI_SWAP_CHAIN_DESC1 swap{};
    swap.Width=128; swap.Height=96; swap.Format=DXGI_FORMAT_B8G8R8A8_UNORM; swap.BufferCount=3;
    swap.SampleDesc.Count=1; swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Com<IDXGISwapChain1> chain1;
    Com<IDXGISwapChain3> chain;
    Require(factory->CreateSwapChainForHwnd(writer.SwapDevice(),window,&swap,nullptr,nullptr,chain1.Put()), "Create D3D11 flip swapchain");
    Require(chain1->QueryInterface(__uuidof(IDXGISwapChain3),chain.AsVoid()), "Read current flip index");
    Com<ID3D11Device> output;
    Com<ID3D11DeviceContext> ctx;
    Require(writer.SwapDevice()->QueryInterface(__uuidof(ID3D11Device),output.AsVoid()), "Get output device");
    output->GetImmediateContext(ctx.Put());
    D3D12_RESOURCE_DESC input{};
    input.Width=64; input.Height=48; input.MipLevels=input.DepthOrArraySize=1; input.SampleDesc.Count=samples;
    input.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; input.Format=DXGI_FORMAT_B8G8R8X8_UNORM;
    input.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Com<ID3D12Resource> color;
    movie_pass::CreateResource12(gpu.Get(),input,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,color);
    Com<ID3D12DescriptorHeap> rtv;
    movie_pass::MakeHeap12(gpu.Get(),D3D12_DESCRIPTOR_HEAP_TYPE_RTV,D3D12_DESCRIPTOR_HEAP_FLAG_NONE,rtv);
    const auto handle=rtv->GetCPUDescriptorHandleForHeapStart();
    gpu->CreateRenderTargetView(color.Get(),nullptr,handle);
    D3DSURFACE_DESC legacy{};
    legacy.Width=64; legacy.Height=48; legacy.Format=D3DFMT_X8R8G8B8;
    for (unsigned frame=0;frame<7;++frame) {
        Com<ID3D12CommandAllocator> allocator;
        Com<ID3D12GraphicsCommandList> cmd;
        Require(gpu->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),
                                            allocator.AsVoid()), "Create scene allocator");
        Require(gpu->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,
                                       __uuidof(ID3D12GraphicsCommandList),cmd.AsVoid()), "Create scene list");
        movie_pass::Transition12(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float red[]{1,0,0,1}, blue[]{0,0,1,1};
        const D3D12_RECT right{32,0,64,48};
        cmd->ClearRenderTargetView(handle,frame%2?red:blue,0,nullptr);
        cmd->ClearRenderTargetView(handle,frame%2?blue:red,1,&right);
        movie_pass::Transition12(cmd.Get(),color.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COMMON);
        Require(cmd->Close(), "Close scene commands");
        ID3D12CommandList* lists[]{cmd.Get()}; direct->ExecuteCommandLists(1,lists);
        const UINT index=chain->GetCurrentBackBufferIndex();
        writer.Write(color.Get(),legacy,chain.Get(),index);
        {
            Com<ID3D11Texture2D> buffer;
            Require(chain->GetBuffer(0,__uuidof(ID3D11Texture2D),buffer.AsVoid()), "Read actual D3D11 backbuffer");
            const auto image=movie_pass::ReadBackD3D11(output.Get(),ctx.Get(),buffer.Get());
            for (auto x : {8u,90u}) {
                const BYTE* at=image.data()+4*(8*128+x);
                const bool isRed=(x<64)==bool(frame%2);
                if (at[0]!=(isRed?0:255) || at[1] || at[2]!=(isRed?255:0) || at[3]!=255) {
                    std::fprintf(stderr,"frame %u index %u x %u BGRA=%u,%u,%u,%u\n",frame,index,x,at[0],at[1],at[2],at[3]);
                    throw std::runtime_error("Wrong native frame pixels");
                }
            }
        }
        Require(chain->Present(0,0), "Native D3D11 Present");
    }
}
}
int main() try {
    Com<IDXGIFactory4> factory;
    Require(CreateDXGIFactory1(__uuidof(IDXGIFactory4),factory.AsVoid()), "Enumerate adapters");
    unsigned tests=0;
    for (UINT i=0;;++i) {
        Com<IDXGIAdapter1> gpu;
        if (factory->EnumAdapters1(i,gpu.Put())==DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{}; Require(gpu->GetDesc1(&desc),"Read adapter");
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        for (unsigned samples : {1u,4u}) {
            struct Window { HWND handle=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,128,96,nullptr,nullptr,nullptr,nullptr);
                            ~Window(){ if (handle) DestroyWindow(handle); } } window;
            if (!window.handle) throw std::runtime_error("Create private test window");
            Run(factory.Get(),gpu.Get(),window.handle,samples);
            std::printf("adapter %u, %ux MSAA, 7 frames XRGB to BGRA, pixels and Present OK\n",i,samples);
            ++tests;
        }
    }
    return tests ? 0 : 1;
} catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 2; }
