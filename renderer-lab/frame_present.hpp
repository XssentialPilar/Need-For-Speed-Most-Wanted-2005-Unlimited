#pragma once
#include <string>
#include <cmath>
#include "frame_present_d3d12.hpp"
#include <dxgi1_5.h>
#if __has_include("frame_present_d3d11.hpp")
#include "frame_present_d3d11.hpp"
#define UNLIMITED_FRAME_WRITER11
#endif
#if __has_include("frame_present_vk.hpp") && __has_include(<vulkan/vulkan.h>)
#include "frame_present_vk.hpp"
#define UNLIMITED_FRAME_WRITERVK
#endif

namespace frame_present {

inline UINT SyncInterval(UINT value) {
    switch(value) {
    case D3DPRESENT_INTERVAL_IMMEDIATE:return 0;
    case D3DPRESENT_INTERVAL_TWO:return 2;
    case D3DPRESENT_INTERVAL_THREE:return 3;
    case D3DPRESENT_INTERVAL_FOUR:return 4;
    default:return 1;
    }
}

class Presenter {
    class Session {
    public:
        Session(ID3D12Device* gpu,HWND window,UINT kind,const D3DPRESENT_PARAMETERS& intent)
            : owner(gpu),hwnd(window),api(kind),requested(intent) {
            const D3D12_COMMAND_QUEUE_DESC q{D3D12_COMMAND_LIST_TYPE_DIRECT,0,D3D12_COMMAND_QUEUE_FLAG_NONE,0};
            Require(gpu->CreateCommandQueue(&q,__uuidof(ID3D12CommandQueue),queue.AsVoid()),"Create game frame queue");
            Require(gpu->CreateFence(0,D3D12_FENCE_FLAG_NONE,__uuidof(ID3D12Fence),fence.AsVoid()),"Create game frame ownership fence");
            if(api==12) writer12=std::make_unique<Writer12>(gpu,queue.Get());
#ifdef UNLIMITED_FRAME_WRITER11
            else if(api==11) writer11=std::make_unique<Writer11>(gpu,queue.Get());
#endif
#ifdef UNLIMITED_FRAME_WRITERVK
            else if(api==13) {
                vk=std::make_unique<PresenterVk>(gpu,queue.Get(),window);
                return;
            }
#endif
            else throw std::runtime_error("Frame presentation backend is unavailable");
            static const auto dxgi=LoadLibraryExW(L"dxgi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            const auto create=dxgi?reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(dxgi,"CreateDXGIFactory1")):nullptr;
            if (!create) throw std::runtime_error("DXGI presentation runtime unavailable");
            Require(create(__uuidof(IDXGIFactory2),factory.AsVoid()),"Create game frame DXGI factory");
            Com<IDXGIFactory5> newer;
            BOOL tearing=FALSE;
            if(SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5),newer.AsVoid())) &&
               SUCCEEDED(newer->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,&tearing,sizeof(tearing))) && tearing)
                flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }
        ~Session() { Drain();if(fullscreen && chain.Get()) chain->SetFullscreenState(FALSE,nullptr); }
        bool Matches(const D3DPRESENT_PARAMETERS& intent) const {
            return requested.Windowed==intent.Windowed && (intent.Windowed ||
                (requested.BackBufferWidth==intent.BackBufferWidth && requested.BackBufferHeight==intent.BackBufferHeight &&
                 requested.FullScreen_RefreshRateInHz==intent.FullScreen_RefreshRateInHz));
        }
        bool SuspendScene(const D3DPRESENT_PARAMETERS* nextIntent) noexcept {
#ifdef UNLIMITED_FRAME_WRITERVK
            if(vk) {
                Drain();
                if(!vk->BeforeSceneReset())return false;
                if(nextIntent) requested=*nextIntent;
                configureVk=true;
                return true;
            }
#else
            (void)nextIntent;
#endif
            return false;
        }
        const char* DisplayMode() const noexcept {
#ifdef UNLIMITED_FRAME_WRITERVK
            if(vk) return vk->DisplayMode();
#endif
            if(requested.Windowed) return "windowed";
            return fullscreen?"exclusive":"pending";
        }
        void Drain() noexcept {
            if(serial && fence->GetCompletedValue()<serial) fence->SetEventOnCompletion(serial,nullptr);
        }
        HRESULT Draw(scene_hud::Interop* interop,IDirect3DSurface9* source,const D3DSURFACE_DESC& legacy,UINT interval) {
            if (!legacy.Width || !legacy.Height) throw std::runtime_error("Invalid game backbuffer extent");
            if(api!=13 && (!chain.Get() || width!=legacy.Width || height!=legacy.Height || legacyFormat!=legacy.Format)) {
                DXGI_FORMAT pixel=DXGI_FORMAT_UNKNOWN;
                {
                    scene_hud::TargetLease inspection(interop,source,queue.Get());
                    pixel=FlipFormat(inspection.resource->GetDesc().Format);
                }
                if(pixel==DXGI_FORMAT_UNKNOWN) throw std::runtime_error("Unsupported game frame pixel format");
                if(!chain.Get()) {
                    DXGI_SWAP_CHAIN_DESC1 d{};
                    d.Width=legacy.Width;d.Height=legacy.Height;d.Format=pixel;d.SampleDesc.Count=1;
                    d.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;d.BufferCount=3;d.Scaling=DXGI_SCALING_STRETCH;
                    if(!requested.Windowed) flags=DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
                    d.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;d.AlphaMode=DXGI_ALPHA_MODE_IGNORE;d.Flags=flags;
                    IUnknown* producer=nullptr;
                    if(writer12) producer=writer12->SwapDevice();
#ifdef UNLIMITED_FRAME_WRITER11
                    else producer=writer11->SwapDevice();
#endif
                    Com<IDXGISwapChain1> initial;
                    Require(factory->CreateSwapChainForHwnd(producer,hwnd,&d,nullptr,nullptr,initial.Put()),"Attach native frame swapchain to game HWND");
                    Require(initial->QueryInterface(__uuidof(IDXGISwapChain3),chain.AsVoid()),"Query native frame swapchain");
                    // The game continues to own window styles and Alt+Enter.
                    Require(factory->MakeWindowAssociation(hwnd,DXGI_MWA_NO_WINDOW_CHANGES|DXGI_MWA_NO_ALT_ENTER),"Keep game window management");
                    if(!requested.Windowed) EnterFullscreen(pixel,legacy);
                } else {
                    Drain();
                    Require(chain->ResizeBuffers(0,legacy.Width,legacy.Height,pixel,flags),"Resize native frame buffers");
                }
                width=legacy.Width;height=legacy.Height;legacyFormat=legacy.Format;
            }
            scene_hud::TargetLease lease(interop,source,queue.Get());
            HRESULT result=S_OK;
            try {
#ifdef UNLIMITED_FRAME_WRITERVK
            if(vk) {
                // Window style, dimensions and monitor have settled after
                // the engine's Reset. Resolve the monitor against the new HWND geometry.
                if(configureVk) {
                    if(!requested.Windowed)vk->ConfigureFullscreen(requested);
                    configureVk=false;
                }
                result=vk->Present(lease.resource.Get(),legacy,interval);
            } else
#endif
            {
                const UINT index=chain->GetCurrentBackBufferIndex();
                if(writer12) writer12->Write(lease.resource.Get(),legacy,chain.Get(),index);
#ifdef UNLIMITED_FRAME_WRITER11
                else writer11->Write(lease.resource.Get(),legacy,chain.Get(),index);
#endif
            }
            } catch (...) {
                // A backend can submit the source copy before detecting a
                // presentation failure. Protect that read on the fallback path.
                ReturnSource(lease,interop,source);
                throw;
            }
            ReturnSource(lease,interop,source);
            if(chain.Get()) {
                const UINT presentFlags=(!fullscreen && interval==0 && (flags&DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING))?DXGI_PRESENT_ALLOW_TEARING:0;
                result=chain->Present(interval,presentFlags);
            }
#ifdef UNLIMITED_FRAME_WRITERVK
            if(vk && result!=S_OK && vk->LastError()[0]) throw movie_pass::D3DError(result,vk->LastError());
#endif
            return result;
        }
        Com<ID3D12Device> owner;
        HWND hwnd;
        UINT api;
    private:
        void ReturnSource(scene_hud::TargetLease& lease,scene_hud::Interop* interop,IDirect3DSurface9* source) {
            const auto next=serial+1;
            Require(queue->Signal(fence.Get(),next),"Signal game backbuffer read");
            serial=next;
            if(FAILED(lease.Return(serial,fence.Get()))) {
                Drain();Require(interop->ReturnUnderlyingResource(source,0,nullptr,nullptr),"Return completed game backbuffer");
            }
        }
        void EnterFullscreen(DXGI_FORMAT pixel,const D3DSURFACE_DESC& frame) {
            if ((requested.BackBufferWidth && requested.BackBufferWidth!=frame.Width) ||
                (requested.BackBufferHeight && requested.BackBufferHeight!=frame.Height))
                throw std::runtime_error("Original fullscreen request differs from scene dimensions");
            Com<IDXGIOutput> output;
            Require(chain->GetContainingOutput(output.Put()),"Find fullscreen game output");
            DXGI_OUTPUT_DESC display{};
            Require(output->GetDesc(&display),"Read fullscreen game monitor");
            if(display.Monitor!=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST))
                throw std::runtime_error("Fullscreen presentation output differs from the game monitor");
            DEVMODEW desktop{};desktop.dmSize=sizeof(desktop);
            const bool haveMode=EnumDisplaySettingsExW(display.DeviceName,ENUM_CURRENT_SETTINGS,&desktop,0)!=0;
            DXGI_MODE_DESC want{};
            want.Width=frame.Width;want.Height=frame.Height;want.Format=pixel;
            const UINT hz=requested.FullScreen_RefreshRateInHz;
            if(hz) want.RefreshRate={hz,1};
            else if(haveMode && desktop.dmDisplayFrequency>1) want.RefreshRate={desktop.dmDisplayFrequency,1};
            DXGI_MODE_DESC mode{};
            Require(output->FindClosestMatchingMode(&want,&mode,nullptr),"Find requested native fullscreen mode");
            if (mode.Width!=want.Width || mode.Height!=want.Height ||
                (hz && (!mode.RefreshRate.Denominator || std::abs(double(mode.RefreshRate.Numerator)/mode.RefreshRate.Denominator-hz)>0.6)))
                throw std::runtime_error("Monitor cannot present the requested fullscreen dimensions or refresh");
            const HRESULT enter=chain->SetFullscreenState(TRUE,output.Get());
            if(enter!=S_OK) throw movie_pass::D3DError(enter,"Native fullscreen acquisition unavailable");
            fullscreen=true;
            if (!haveMode || desktop.dmPelsWidth!=mode.Width || desktop.dmPelsHeight!=mode.Height ||
                (mode.RefreshRate.Denominator && std::abs(double(mode.RefreshRate.Numerator)/mode.RefreshRate.Denominator-desktop.dmDisplayFrequency)>0.6))
                Require(chain->ResizeTarget(&mode),"Set requested fullscreen resolution and refresh");
            Require(chain->ResizeBuffers(0,frame.Width,frame.Height,pixel,flags),"Apply fullscreen flip buffers");
            BOOL active=FALSE;
            Require(chain->GetFullscreenState(&active,nullptr),"Verify native fullscreen ownership");
            if(!active) throw std::runtime_error("Native swapchain did not acquire fullscreen");
        }
        Com<ID3D12CommandQueue> queue;
        Com<ID3D12Fence> fence;
        Com<IDXGIFactory2> factory;
        Com<IDXGISwapChain3> chain;
        std::unique_ptr<Writer12> writer12;
#ifdef UNLIMITED_FRAME_WRITER11
        std::unique_ptr<Writer11> writer11;
#endif
#ifdef UNLIMITED_FRAME_WRITERVK
        std::unique_ptr<PresenterVk> vk;
        bool configureVk=true;
#endif
        D3DFORMAT legacyFormat=D3DFMT_UNKNOWN;
        D3DPRESENT_PARAMETERS requested{};
        bool fullscreen=false;
        UINT width{},height{},flags{};
        UINT64 serial{};
    };
public:
    // Use in place of Present(nullptr,nullptr,nullptr,nullptr). Returning false
    // gives the original game ownership of this frame, including lost-device handling.
    bool TryPresent(IDirect3DDevice9* device,UINT api,HRESULT& result,const D3DPRESENT_PARAMETERS* original=nullptr) noexcept {
        if(api!=11 && api!=12 && api!=13) { Reset();return false; }
        if(!device) {Reset();return false;}
        if(device==retryDevice && api==retryApi && GetTickCount64()<retryTime) return false;
        error[0]=0;
        permanent=false;
        needsPrivateReset=false;
        try {
            const auto ready=device->TestCooperativeLevel();
            if(FAILED(ready)) {Suspend();return false;}
            Com<IDirect3DSwapChain9> old;
            D3DPRESENT_PARAMETERS pp{};
            Require(device->GetSwapChain(0,old.Put()),"Get game swapchain");
            Require(old->GetPresentParameters(&pp),"Read game presentation mode");
            if(!pp.hDeviceWindow) {
                D3DDEVICE_CREATION_PARAMETERS creation{};
                Require(device->GetCreationParameters(&creation),"Read game focus window");
                pp.hDeviceWindow=creation.hFocusWindow;
            }
            if(!pp.hDeviceWindow || IsIconic(pp.hDeviceWindow)) { Suspend();return false; }
            if(!pp.Windowed || (original && !original->Windowed)) {
                DWORD foreground{};
                GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
                if(foreground!=GetCurrentProcessId()) { Suspend();return false; }
            }
            // Applying graphics settings can recreate the engine's implicit
            // swapchain in exclusive mode. Let its own reset path release
            // default-pool resources before rebuilding the private scene.
            if(!pp.Windowed) return ExclusiveScene(device,api,original!=nullptr);
            Com<scene_hud::Interop> interop;
            Require(device->QueryInterface(scene_hud::InteropId,interop.AsVoid()),"Game device lacks D3D9On12 presentation interop");
            Com<ID3D12Device> gpu;
            Require(interop->GetD3D12Device(__uuidof(ID3D12Device),gpu.AsVoid()),"Get scene rendering device");
            const auto& intent=original?*original:pp;
            const bool same=session && session->owner.Get()==gpu.Get() && session->hwnd==pp.hDeviceWindow && api==session->api;
            if(!same || (!session->Matches(intent) && !session->SuspendScene(&intent))) {
                session.reset();session=std::make_shared<Session>(gpu.Get(),pp.hDeviceWindow,api,intent);
            }
            Com<IDirect3DSurface9> source;
            Require(old->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,source.Put()),"Read composed game backbuffer");
            D3DSURFACE_DESC desc{};
            Require(source->GetDesc(&desc),"Read composed game frame descriptor");
            auto active=session;
            const auto shown=active->Draw(interop.Get(),source.Get(),desc,SyncInterval(intent.PresentationInterval));
            if(shown!=S_OK) {
                if(FAILED(shown)) Require(shown,"Present complete game frame");
                throw movie_pass::D3DError(shown,"Native frame swapchain could not display the game window");
            }
            error[0]=0;retryDevice=nullptr;hardAttempts=0;++frames;result=D3D_OK;return true;
        } catch (const movie_pass::D3DError& e) {
            return Failed(device,api,e.what(),Temporary(e.Code()));
        } catch (const std::exception& e) {
            return Failed(device,api,e.what(),false);
        } catch (...) {return Failed(device,api,"Unknown native presentation error",false);}
    }
    void BeforeSceneReset(const D3DPRESENT_PARAMETERS& intent) noexcept {Suspend(&intent);ClearRetry();}
    void Reset() noexcept {session.reset();ClearRetry();}
    UINT64 Count() const {return frames;}
    const char* LastErrorText() const {return error;}
    const char* PresentationMode() const noexcept {return session?session->DisplayMode():"original";}
    bool NeedsPrivateSceneReset() const noexcept {return needsPrivateReset;}
    bool ShouldRestoreOriginalMode() const {return permanent;}
private:
    void ClearRetry() noexcept {retryDevice=nullptr;retryTime=0;hardAttempts=0;permanent=false;needsPrivateReset=false;error[0]=0;}
    void Suspend(const D3DPRESENT_PARAMETERS* intent=nullptr) noexcept {
        if(session && !session->SuspendScene(intent))session.reset();
    }
    bool ExclusiveScene(IDirect3DDevice9* device,UINT api,bool managed) noexcept {
        Failed(device,api,"D3D9 scene swapchain owns exclusive fullscreen",managed);
        needsPrivateReset=managed;
        return false;
    }
    static bool Temporary(HRESULT h) {
        switch(h) {
        case S_FALSE: case DXGI_STATUS_OCCLUDED: case DXGI_STATUS_MODE_CHANGED: case DXGI_STATUS_MODE_CHANGE_IN_PROGRESS:
        case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: case DXGI_ERROR_WAS_STILL_DRAWING: case DXGI_ERROR_ACCESS_LOST:
        case DXGI_ERROR_SESSION_DISCONNECTED: case DXGI_ERROR_DEVICE_REMOVED: case DXGI_ERROR_DEVICE_RESET:
        case D3DERR_DEVICELOST: case D3DERR_DEVICENOTRESET: case E_OUTOFMEMORY: case DXGI_ERROR_WAIT_TIMEOUT:
            return true;
        default: return false;
        }
    }
    bool Failed(IDirect3DDevice9* device,UINT api,const char* why,bool transient) noexcept {
        const bool repeated=retryDevice==device && retryApi==api && std::strcmp(previousFailure,why)==0;
        hardAttempts=transient?0u:(repeated?hardAttempts+1u:1u);
        permanent=hardAttempts>=2;
        std::snprintf(previousFailure,sizeof(previousFailure),"%s",why);
        std::snprintf(error,sizeof(error),"%s",why);
        if(transient)Suspend();
        else session.reset();
        retryDevice=device;retryApi=api;
        retryTime=GetTickCount64()+(permanent?3000u:transient?350u:200u);
        return false;
    }
    std::shared_ptr<Session> session;
    IDirect3DDevice9* retryDevice{}; // Identity only, never dereferenced.
    UINT retryApi{};
    ULONGLONG retryTime{};
    UINT64 frames{};
    UINT hardAttempts{};
    bool permanent=false,needsPrivateReset=false;
    char error[192]{},previousFailure[192]{};
};
} // namespace frame_present
