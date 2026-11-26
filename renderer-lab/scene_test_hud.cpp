#include "../vram_probe.cpp"
#include "scene_hud_d3d12.hpp"

using movie_pass::Com;
using movie_pass::Require;

namespace {

void Check(bool result, const char* name) {
    if (!result) throw std::runtime_error(name);
}

class Window {
public:
    Window() : handle(CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,320,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)) {
        Check(handle != nullptr,"Create hidden test window");
    }
    ~Window() { DestroyWindow(handle); }
    HWND handle;
};

void DrawOriginal(IDirect3DDevice9* d, const InstrumentGeometry& g) {
    Require(d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE),"FVF");
    Require(d->SetVertexShader(nullptr),"VS");
    Require(d->SetPixelShader(nullptr),"PS");
    for (auto [state,value] : std::initializer_list<std::pair<D3DRENDERSTATETYPE,DWORD>>{
        {D3DRS_ZENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_LIGHTING,FALSE},
        {D3DRS_ALPHABLENDENABLE,TRUE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},
        {D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},
        {D3DRS_BLENDOP,D3DBLENDOP_ADD},{D3DRS_COLORWRITEENABLE,15}}) {
        Require(d->SetRenderState(state,value),"Set original render state");
    }
    Require(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1),"Diffuse operation");
    Require(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE),"Diffuse color");
    Require(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1),"Alpha operation");
    Require(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE),"Diffuse alpha");
    Require(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE),"Disable stage 1");
    Require(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(g.count / 3),g.vertices.data(),sizeof(InstrumentVertex)),"Draw original HUD");
}

std::vector<DWORD> Render(IDirect3DDevice9* device, IDirect3DSurface9* target, D3DVIEWPORT9 viewport,
                          scene_hud::GameHud* pass, InstrumentReading reading) {
    D3DSURFACE_DESC desc{};
    Require(target->GetDesc(&desc),"Target description");
    Require(device->SetRenderTarget(0,target),"Set game target");
    const D3DCOLOR bg = D3DCOLOR_ARGB(107,43,57,91);
    Require(device->Clear(0,nullptr,D3DCLEAR_TARGET,bg,1,0),"Game clear");
    Require(device->SetViewport(&viewport),"Set game viewport");
    Require(device->BeginScene(),"Begin game FE pass");
    InstrumentGeometry g(PlaceInstruments(viewport,HudBounds{0,0,312,184}));
    BuildInstrumentGeometry(g,reading);
    if (pass) {
        Require(device->SetRenderState(D3DRS_FOGENABLE,TRUE),"Set sentinel state");
        bool drawn = pass->Draw(device,g.vertices.data(),g.count,sizeof(InstrumentVertex));
        if (!drawn) std::printf("Native draw returned false, HRESULT=%08lx\n",pass->LastError());
        Check(drawn,"Native D3D12 instruments failed");
        DWORD saved = 0;
        Require(device->GetRenderState(D3DRS_FOGENABLE,&saved),"Read game fog state");
        Check(saved == TRUE,"Native HUD disturbed the game's D3D9 pipeline");
        Require(device->SetRenderState(D3DRS_FOGENABLE,FALSE),"Restore test fog state");
    } else DrawOriginal(device,g);
    Require(device->EndScene(),"End game FE pass");
    // Read the native draw *through the game device* immediately after EndScene.
    Com<IDirect3DSurface9> staging;
    Require(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,staging.Put(),nullptr),"Create readback");
    Com<IDirect3DSurface9> resolved;
    if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
        Require(device->CreateRenderTarget(desc.Width,desc.Height,desc.Format,D3DMULTISAMPLE_NONE,0,FALSE,resolved.Put(),nullptr),"Create MSAA resolve");
        Require(device->StretchRect(target,nullptr,resolved.Get(),nullptr,D3DTEXF_NONE),"Resolve MSAA");
        target = resolved.Get();
    }
    Require(device->GetRenderTargetData(target,staging.Get()),"Read game-visible pixels");
    D3DLOCKED_RECT rect{};
    Require(staging->LockRect(&rect,nullptr,D3DLOCK_READONLY),"Read final image");
    std::vector<DWORD> pixels(desc.Width * desc.Height);
    for (UINT i = 0; i < desc.Height; ++i) std::memcpy(pixels.data()+i*desc.Width,static_cast<BYTE*>(rect.pBits)+i*rect.Pitch,desc.Width*4);
    staging->UnlockRect();
    return pixels;
}

void Compare(IDirect3DDevice9* device, IDirect3DSurface9* dest, D3DVIEWPORT9 area,
             scene_hud::GameHud& pass, InstrumentReading reading, const char* label) {
    const auto original = Render(device,dest,area,nullptr,reading);
    const auto modern = Render(device,dest,area,&pass,reading);
    unsigned differences = 0, colored = 0, maxError = 0;
    for (size_t i=0;i<original.size();++i) {
        colored += (original[i] & 0xffffff) != 0x2b395b;
        if (modern[i] != original[i]) {
            // X8 targets may return undefined high bits even within one present.
            D3DSURFACE_DESC desc{};
            Require(dest->GetDesc(&desc),"Compare target format");
            const bool alpha = desc.Format == D3DFMT_A8R8G8B8;
            for (unsigned n=0; n<(alpha?4u:3u); ++n) {
                int a=(original[i]>>(n*8))&255, b=(modern[i]>>(n*8))&255;
                maxError=std::max(maxError,unsigned(std::abs(a-b)));
            }
            differences += (alpha ? original[i]!=modern[i] : ((original[i]^modern[i])&0xffffff)!=0);
        }
    }
    std::printf("%s: %zu pixels, %u HUD pixels, %u different, max error %u\n",label,original.size(),colored,differences,maxError);
    Check(colored>300,"No meaningful instrument geometry");
    Check(differences == 0,"Instrument pixels differ");
}

void Run(bool useEx) {
    Window window;
    HMODULE runtime = LoadSystemLibrary(L"d3d9.dll");
    Check(runtime != nullptr,"Load Windows d3d9");
    Com<IDirect3D9> factory;
    if (useEx) {
        using CreateEx = HRESULT(WINAPI*)(UINT,scene_hud::FactoryOverride*,UINT,IDirect3D9Ex**);
        const auto create = reinterpret_cast<CreateEx>(GetProcAddress(runtime,"Direct3DCreate9On12Ex"));
        Check(create != nullptr,"D3D9On12Ex unavailable");
        scene_hud::FactoryOverride options{}; options.enabled = TRUE;
        IDirect3D9Ex* instance = nullptr;
        Require(create(D3D_SDK_VERSION,&options,1,&instance),"D3D9On12Ex factory");
        *factory.Put() = instance;
    } else *factory.Put() = scene_hud::CreateInteropFactory(runtime,D3D_SDK_VERSION);
    Check(factory.Get() != nullptr,"No non-Ex D3D9On12 factory");
    D3DPRESENT_PARAMETERS p{};
    p.Windowed = TRUE;
    p.BackBufferWidth = 318;
    p.BackBufferHeight = 231;
    p.BackBufferFormat = D3DFMT_X8R8G8B8;
    p.SwapEffect = D3DSWAPEFFECT_DISCARD;
    p.hDeviceWindow = window.handle;
    p.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    Com<IDirect3DDevice9> device;
    Require(factory->CreateDevice(0,D3DDEVTYPE_HAL,window.handle,D3DCREATE_HARDWARE_VERTEXPROCESSING,&p,device.Put()),"Create game-style HAL");
    Check(scene_hud::SupportsDevice(device.Get()),"HAL has no IDirect3DDevice9On12 interface");
    if (!useEx) {
        Com<IDirect3DTexture9> managed;
        Require(device->CreateTexture(128,64,4,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,managed.Put(),nullptr),"Allocate game managed texture");
        D3DLOCKED_RECT lock{};
        Require(managed->LockRect(2,&lock,nullptr,0),"Lock game managed mip");
        std::memset(lock.pBits,0x47,32*4);
        Require(managed->UnlockRect(2),"Update game managed mip");
        std::puts("Non-Ex, D3DPOOL_MANAGED texture + mip upload OK");
    }
    D3DCAPS9 caps{};
    Require(device->GetDeviceCaps(&caps),"Game caps");
    std::printf("9On12 HAL, SM %lu.%lu/%lu.%lu, max texture %lux%lu\n",D3DSHADER_VERSION_MAJOR(caps.VertexShaderVersion),
                D3DSHADER_VERSION_MINOR(caps.VertexShaderVersion),D3DSHADER_VERSION_MAJOR(caps.PixelShaderVersion),
                D3DSHADER_VERSION_MINOR(caps.PixelShaderVersion),caps.MaxTextureWidth,caps.MaxTextureHeight);
    scene_hud::GameHud native;
    const InstrumentReading fast{258,'5',.91f,.87f,false,true,1}, slow{19,'2',.31f,.85f,true,false,1};
    Com<IDirect3DSurface9> back;
    Require(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.Put()),"Get game backbuffer");
    Compare(device.Get(),back.Get(),{0,0,p.BackBufferWidth,p.BackBufferHeight,0,1},native,fast,"backbuffer");
    Require(device->Present(nullptr,nullptr,nullptr,nullptr),"Present combined frame");
    Com<IDirect3DTexture9> tex;
    Com<IDirect3DSurface9> color;
    Require(device->CreateTexture(513,257,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,tex.Put(),nullptr),"Allocate FE render texture");
    Require(tex->GetSurfaceLevel(0,color.Put()),"Get FE surface");
    Compare(device.Get(),color.Get(),{37,9,455,223,0,1},native,slow,"game texture, offset viewport + alpha");
    DWORD levels = 0;
    if (SUCCEEDED(factory->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_4_SAMPLES,&levels))) {
        Com<IDirect3DSurface9> multisample;
        Require(device->CreateRenderTarget(320,240,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_4_SAMPLES,0,FALSE,
                                           multisample.Put(),nullptr),"Allocate MSAA surface");
        Compare(device.Get(),multisample.Get(),{0,0,320,240,0,1},native,fast,"MSAA 4x");
    }
    Require(device->SetRenderTarget(0,back.Get()),"Restore backbuffer");
    color.Reset(); tex.Reset(); back.Reset();
    p.BackBufferWidth=211; p.BackBufferHeight=399;
    Require(device->Reset(&p),"Resize normal game device with native pipeline resident");
    Require(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,back.Put()),"Get resized image");
    Compare(device.Get(),back.Get(),{0,0,211,399,0,1},native,slow,"portrait after Reset");
    Require(device->Present(nullptr,nullptr,nullptr,nullptr),"Present after Reset");
    std::printf("Native pass drew %llu verified frames on the game's render targets\n",native.DrawCount());
}
} // namespace

int main(int argc,char**) {
    try { Run(argc > 1); return 0; }
    catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
