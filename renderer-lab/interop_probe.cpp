#include "scene_hud_d3d12.hpp"
#include <d3d11_4.h>

using movie_pass::Com;
int main() {
    const auto library = LoadLibraryExW(L"d3d9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    const HWND window = CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,16,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    for (int test = 0; test < 4; ++test) {
        const bool mixed = (test & 1) != 0;
        const bool ex = test >= 2;
        Com<IDirect3D9> factory;
        if (ex) {
            IDirect3D9Ex* instance = nullptr;
            using On12Ex = HRESULT(WINAPI*)(UINT,scene_hud::FactoryOverride*,UINT,IDirect3D9Ex**);
            const auto create = reinterpret_cast<On12Ex>(GetProcAddress(library,"Direct3DCreate9On12Ex"));
            scene_hud::FactoryOverride options{};
            options.enabled = mixed;
            if (mixed && create) create(D3D_SDK_VERSION,&options,1,&instance);
            else Direct3DCreate9Ex(D3D_SDK_VERSION,&instance);
            *factory.Put() = instance;
        } else *factory.Put() = mixed ? scene_hud::CreateInteropFactory(library,D3D_SDK_VERSION) : Direct3DCreate9(D3D_SDK_VERSION);
        if (!factory.Get()) continue;
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE; pp.hDeviceWindow = window; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferWidth = pp.BackBufferHeight = 16;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        Com<IDirect3DDevice9> device;
        HRESULT hr = factory->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,device.Put());
        std::printf("mixed %u ex %u device %08lx\n", mixed, ex, hr);
        if (FAILED(hr)) continue;
        HANDLE share = nullptr;
        Com<IDirect3DTexture9> texture;
        hr = device->CreateTexture(16,16,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,texture.Put(),&share);
        std::printf("non-Ex D3D9 shared handle %08lx %p\n",hr,share);
        if (share) {
            Com<ID3D11Device> d11;
            Com<ID3D11Resource> reopened;
            hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,d11.Put(),nullptr,nullptr);
            if (SUCCEEDED(hr)) hr=d11->OpenSharedResource(share,__uuidof(ID3D11Resource),reopened.AsVoid());
            std::printf("D3D11 open legacy handle: %08lx\n",hr);
        }
        Com<scene_hud::Interop> interop;
        if (FAILED(device->QueryInterface(scene_hud::InteropId,interop.AsVoid()))) continue;
        Com<ID3D12Device> d12;
        movie_pass::Require(interop->GetD3D12Device(__uuidof(ID3D12Device),d12.AsVoid()),"get D3D12");
        Com<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC desc{};
        movie_pass::Require(d12->CreateCommandQueue(&desc,__uuidof(ID3D12CommandQueue),queue.AsVoid()),"get queue");
        Com<IDirect3DSurface9> back;
        movie_pass::Require(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.Put()),"get backbuffer");
        scene_hud::TargetLease leased(interop.Get(),back.Get(),queue.Get());
        HANDLE handle = nullptr;
        hr=d12->CreateSharedHandle(leased.resource.Get(),nullptr,GENERIC_ALL,nullptr,&handle);
        std::printf("Export actual 9On12 backbuffer %08lx ptr %p\n",hr,handle);
        if(handle) CloseHandle(handle);
    }
    DestroyWindow(window);
}
