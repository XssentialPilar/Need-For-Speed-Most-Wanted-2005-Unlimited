#include "frame_present.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace frame_present;

namespace {
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
class Infra {
public:
    Infra(HWND hwnd,UINT w,UINT h,D3DFORMAT fmt,D3DMULTISAMPLE_TYPE ms) {
        auto dll=LoadLibraryExW(L"d3d9.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        *factory.Put()=scene_hud::CreateInteropFactory(dll,D3D_SDK_VERSION);Check(factory.Get()!=nullptr,"Create 9On12 factory");
        D3DPRESENT_PARAMETERS p{};
        p.hDeviceWindow=hwnd;p.Windowed=TRUE;p.BackBufferWidth=w;p.BackBufferHeight=h;p.BackBufferFormat=fmt;
        p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.MultiSampleType=ms;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        Require(factory->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING,&p,legacy.Put()),"Create legacy game window");
        Require(legacy->QueryInterface(scene_hud::InteropId,interop.AsVoid()),"Get 9On12 for full frame");
        Require(interop->GetD3D12Device(__uuidof(ID3D12Device),gpu.AsVoid()),"Get game GPU");
        Require(legacy->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,source.Put()),"Get game flip surface");
        Require(source->GetDesc(&desc),"Read game dimensions");
        const D3D12_COMMAND_QUEUE_DESC q{D3D12_COMMAND_LIST_TYPE_DIRECT,0,D3D12_COMMAND_QUEUE_FLAG_NONE,0};
        Require(gpu->CreateCommandQueue(&q,__uuidof(ID3D12CommandQueue),queue.AsVoid()),"Create test queue");
        Require(gpu->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),fence.AsVoid()),"Create readback fence");
    }
    void Paint(UINT sequence) {
        const UINT w=desc.Width,h=desc.Height;
        Require(legacy->Clear(0,nullptr,D3DCLEAR_TARGET,Colors(sequence,0),1,0),"Paint frame background");
        const D3DRECT a{0,0,LONG(w/2),LONG(h/2)},b{LONG(w/2),LONG(h/2),LONG(w),LONG(h)};
        Require(legacy->Clear(1,&a,D3DCLEAR_TARGET,Colors(sequence,1),1,0),"Paint first quadrant");
        Require(legacy->Clear(1,&b,D3DCLEAR_TARGET,Colors(sequence,2),1,0),"Paint last quadrant");
    }
    static DWORD Colors(UINT seq,UINT i) {
        return UINT(0x11004020u+(seq*17<<16)+(i*53<<24)+(i*31<<8)+seq*3);
    }
    void Compare(ID3D12Resource* image,UINT seq) {
        const auto d=image->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT rows{};UINT64 rowbytes{},bytes{};
        gpu->GetCopyableFootprints(&d,0,1,0,&fp,&rows,&rowbytes,&bytes);
        Com<ID3D12Resource> read;
        movie_pass::CreateResource12(gpu.Get(),movie_pass::Buffer12(bytes),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST,read);
        Com<ID3D12CommandAllocator> alloc;
        Require(gpu->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,__uuidof(ID3D12CommandAllocator),alloc.AsVoid()),"Make verification allocator");
        Com<ID3D12GraphicsCommandList> list;
        Require(gpu->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,__uuidof(ID3D12GraphicsCommandList),list.AsVoid()),"Make verification list");
        movie_pass::Transition12(list.Get(),image,D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION target{};target.pResource=read.Get();target.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;target.PlacedFootprint=fp;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=image;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&target,0,0,0,&src,nullptr);
        movie_pass::Transition12(list.Get(),image,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);
        Require(list->Close(),"Close verification list");
        ID3D12CommandList* cmd[]{list.Get()};queue->ExecuteCommandLists(1,cmd);
        Require(queue->Signal(fence.Get(),++serial),"Signal verification");
        Require(fence->SetEventOnCompletion(serial,nullptr),"Wait for verification");
        void* data{};const D3D12_RANGE range{0,SIZE_T(bytes)};
        Require(read->Map(0,&range,&data),"Map test results");
        unsigned bad=0;
        for(UINT y=0;y<desc.Height;++y) {
            const auto* row=reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(data)+size_t(y)*fp.Footprint.RowPitch);
            for(UINT x=0;x<desc.Width;++x) {
                UINT part=x<desc.Width/2&&y<desc.Height/2?1:x>=desc.Width/2&&y>=desc.Height/2?2:0;
                const auto expected=Colors(seq,part)&0xffffff;
                if ((row[x]&0xffffff)!=expected) ++bad;
            }
        }
        const D3D12_RANGE nothing{};read->Unmap(0,&nothing);
        if (bad) std::printf("Unexpected RGB in %u pixels\n",bad);
        Check(!bad,"Complete game frame changed color or position");
    }
    Com<IDirect3D9> factory;Com<IDirect3DDevice9> legacy;
    Com<IDirect3DSurface9> source;Com<scene_hud::Interop> interop;Com<ID3D12Device> gpu;
    Com<ID3D12CommandQueue> queue;Com<ID3D12Fence> fence;
    D3DSURFACE_DESC desc{};UINT64 serial{};
};
void Test(UINT api,D3DFORMAT fmt,D3DMULTISAMPLE_TYPE ms) {
    const HWND hwnd=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,263,179,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(hwnd!=nullptr,"Create hidden render host");
    {
        Infra rig(hwnd,263,179,fmt,ms);
        if(api==12) {
            Writer12 writer(rig.gpu.Get(),rig.queue.Get());
            Com<IDXGIFactory2> factory;Require(CreateDXGIFactory1(__uuidof(IDXGIFactory2),factory.AsVoid()),"Create verification factory");
            DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=263;desc.Height=179;desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=3;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;desc.Scaling=DXGI_SCALING_STRETCH;
            Com<IDXGISwapChain1> created;Com<IDXGISwapChain3> swap;
            Require(factory->CreateSwapChainForHwnd(writer.SwapDevice(),hwnd,&desc,nullptr,nullptr,created.Put()),"Create game-window DXGI chain");
            Require(created->QueryInterface(__uuidof(IDXGISwapChain3),swap.AsVoid()),"Get flip buffers");
            UINT initial{};Require(swap->GetLastPresentCount(&initial),"Read initial present count");
            for(UINT i=1;i<=6;++i) {
                rig.Paint(i);
                const auto index=swap->GetCurrentBackBufferIndex();
                Com<ID3D12Resource> image;Require(swap->GetBuffer(index,__uuidof(ID3D12Resource),image.AsVoid()),"Get next display buffer");
                {
                    scene_hud::TargetLease lease(rig.interop.Get(),rig.source.Get(),rig.queue.Get());
                    writer.Write(lease.resource.Get(),rig.desc,swap.Get(),index);
                    Require(rig.queue->Signal(rig.fence.Get(),++rig.serial),"Signal composition");
                    Require(lease.Return(rig.serial,rig.fence.Get()),"Release game frame");
                }
                rig.Compare(image.Get(),i);
                Require(swap->Present(0,0),"Flip game's complete frame");
            }
            UINT last{};Require(swap->GetLastPresentCount(&last),"Read final native present count");
            Check(last-initial==6,"Native game-window present counter did not advance");
        }
        Presenter presented;
        HRESULT result{};
        for (UINT i=1;i<=12;++i) {
            rig.Paint(i);
            if (!presented.TryPresent(rig.legacy.Get(),api,result)) {
                std::fprintf(stderr,"Live API %u: %s\n",api,presented.LastErrorText());
                throw std::runtime_error("File production presenter rejected game buffer");
            }
            Require(result,"Native complete-frame presentation");
        }
        Check(presented.Count()==12,"Incorrect successful present count");
        Check(!presented.TryPresent(rig.legacy.Get(),9,result),"Compatibility switch ignored");
        if(api==11 || api==12) {
            // Reject the impossible mode before any display transition. The
            // legacy owner must still be able to present the same buffer.
            D3DPRESENT_PARAMETERS impossible{};
            impossible.BackBufferWidth=264;impossible.BackBufferHeight=179;impossible.Windowed=FALSE;
            Check(!presented.TryPresent(rig.legacy.Get(),api,result,&impossible),"Accepted an incompatible exclusive mode");
            DWORD active{};
            GetWindowThreadProcessId(GetForegroundWindow(),&active);
            if(active!=GetCurrentProcessId())
                Check(!presented.ShouldRestoreOriginalMode() && !presented.LastErrorText()[0],"Losing focus incorrectly disabled native presentation");
        }
        Require(rig.legacy->Present(nullptr,nullptr,nullptr,nullptr),"Restore original game presentation");
        presented.Reset();
        rig.source.Reset();
        D3DPRESENT_PARAMETERS resized{};
        resized.hDeviceWindow=hwnd;resized.Windowed=TRUE;resized.BackBufferWidth=311;resized.BackBufferHeight=107;
        resized.BackBufferFormat=fmt;resized.SwapEffect=D3DSWAPEFFECT_DISCARD;resized.MultiSampleType=ms;
        resized.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        Require(rig.legacy->Reset(&resized),"Reset original game scene size");
        Require(rig.legacy->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,rig.source.Put()),"Acquire resized game frame");
        Require(rig.source->GetDesc(&rig.desc),"Read resized game frame");
        rig.Paint(0);
        Check(presented.TryPresent(rig.legacy.Get(),api,result),"Could not resume native flips after scene reset");
        Require(result,"Flip resized scene");
        std::printf("API=%u D3DFMT=%u MSAA=%u: %s12 completed native flips and successful return to D3D9\n",api,fmt,ms,
                api==12?"282,462 RGB pixel comparisons matched; ":"");
    }
    DestroyWindow(hwnd);
}
}
int main(int argc,char** argv) try {
    const UINT api=argc>1?std::strtoul(argv[1],nullptr,10):12;
    for (auto format : {D3DFMT_X8R8G8B8,D3DFMT_A8R8G8B8})
        for(auto samples : {D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_4_SAMPLES}) Test(api,format,samples);
    return 0;
} catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 2;}
