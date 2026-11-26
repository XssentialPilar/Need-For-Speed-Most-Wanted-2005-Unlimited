#include "scene_hud_d3d12.hpp"
#include <d3dx9.h>
#include <cstdio>

using movie_pass::Com;
using movie_pass::Require;

namespace {
struct Survey {
    IDirect3DDevice9* legacy;
    IDirect3DDevice9* mixed;
    unsigned effects = 0, techniques = 0, passes = 0;
    unsigned accepted[2]{};
    unsigned losses = 0;
};

BOOL CALLBACK Inspect(HMODULE image, LPCWSTR type, LPWSTR name, LONG_PTR context) try {
    auto& survey = *reinterpret_cast<Survey*>(context);
    const auto item = FindResourceW(image,name,type);
    const DWORD length = SizeofResource(image,item);
    auto* data = static_cast<const BYTE*>(LockResource(LoadResource(image,item)));
    DWORD signature = 0;
    if (data && length >= 4) std::memcpy(&signature,data,4);
    if (signature != 0xfeff0901) return TRUE;
    Com<ID3DXEffect> fx[2];
    IDirect3DDevice9* devices[]{survey.legacy,survey.mixed};
    for (unsigned i=0;i<2;++i) {
        Require(D3DXCreateEffect(devices[i],data,length,nullptr,nullptr,0,nullptr,fx[i].Put(),nullptr),"Create retail effect");
    }
    D3DXEFFECT_DESC description{};
    Require(fx[0]->GetDesc(&description),"Read effect schema");
    ++survey.effects;
    for (UINT i=0;i<description.Techniques;++i) {
        bool available[2]{};
        for (unsigned d=0;d<2;++d) {
            auto* effect = fx[d].Get();
            const auto technique = effect->GetTechnique(i);
            available[d] = SUCCEEDED(effect->ValidateTechnique(technique));
            if (available[d]) ++survey.accepted[d];
            Require(effect->SetTechnique(technique),"Select retail technique");
            UINT count=0;
            Require(effect->Begin(&count,0),"Begin retail effect on device");
            for (UINT j=0;j<count;++j) {
                Require(effect->BeginPass(j),"Bind retail pass on device");
                Require(effect->CommitChanges(),"Commit retail effect state");
                Require(effect->EndPass(),"End retail pass");
            }
            Require(effect->End(),"Restore state after retail effect");
            if (d==1) survey.passes+=count;
        }
        if (available[0] && !available[1]) {
            ++survey.losses;
            std::fwprintf(stderr,L"%ls technique %u is available only on the legacy driver\n",name,i);
        }
        ++survey.techniques;
    }
    return TRUE;
} catch (const std::exception& error) {
    std::fprintf(stderr,"%s\n",error.what());
    return FALSE;
}
} // namespace

int wmain(int argc,wchar_t** argv) {
    if (argc!=2) return 1;
    const HMODULE image=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_AS_IMAGE_RESOURCE|LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
    if (!image) return 2;
    const HWND window=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    int result=3;
    try {
        Com<IDirect3D9> factories[2];
        *factories[0].Put()=Direct3DCreate9(D3D_SDK_VERSION);
        *factories[1].Put()=scene_hud::CreateInteropFactory(GetModuleHandleW(L"d3d9.dll"),D3D_SDK_VERSION);
        Com<IDirect3DDevice9> devices[2];
        for (unsigned i=0;i<2;++i) {
            if (!factories[i].Get()) throw std::runtime_error("Missing Direct3D9 factory");
            D3DPRESENT_PARAMETERS pp{};
            pp.Windowed=TRUE; pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.BackBufferWidth=pp.BackBufferHeight=64;
            pp.BackBufferFormat=D3DFMT_X8R8G8B8; pp.hDeviceWindow=window;
            Require(factories[i]->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,
                    &pp,devices[i].Put()),"Create comparable non-Ex HALs");
        }
        if (!scene_hud::SupportsDevice(devices[1].Get())) throw std::runtime_error("Missing 9On12 interop");
        Survey survey{devices[0].Get(),devices[1].Get()};
        if (!EnumResourceNamesW(image,MAKEINTRESOURCEW(10),Inspect,reinterpret_cast<LONG_PTR>(&survey))) {
            throw std::runtime_error("Retail effect enumeration incomplete");
        }
        std::printf("%u effects, %u total techniques, %u validated on standard D3D9, %u validated on 9On12, "
            "%u mixed-driver passes bound and committed, %u lost techniques\n",survey.effects,survey.techniques,
            survey.accepted[0],survey.accepted[1],survey.passes,survey.losses);
        result = survey.effects == 31 && survey.techniques == 76 && !survey.losses && survey.passes ? 0 : 4;
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); }
    DestroyWindow(window);
    FreeLibrary(image);
    return result;
}
