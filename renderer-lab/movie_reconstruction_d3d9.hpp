#pragma once
#include "scene_movie_d3d12.hpp"

namespace movie_reconstruction {
#include "movie_reconstruction_d3d9.inc"

// Store the shader on the decoded texture so that it follows the same device lifetime.
inline constexpr GUID movieShaderKey{0x453691d4,0x632e,0x4718,
    {0x88,0x12,0xbe,0x9f,0xd6,0x55,0x8a,0x13}};

inline bool Draw(IDirect3DDevice9* device, IDirect3DTexture9* source, D3DPRIMITIVETYPE type,
                 UINT count, const void* vertices, UINT stride, HRESULT& result) {
    if (!device || !source || !vertices || type!=D3DPT_TRIANGLEFAN || count!=2 ||
        stride!=sizeof(scene_movie::Vertex)) return false;
    D3DSURFACE_DESC desc{};
    movie_pass::Com<IDirect3DBaseTexture9> bound;
    movie_pass::Com<IDirect3DPixelShader9> original, current;
    if (FAILED(source->GetLevelDesc(0,&desc)) || desc.Width!=1024 || desc.Height!=512 ||
        FAILED(device->GetTexture(0,bound.Put())) || source!=bound.Get() ||
        FAILED(device->GetPixelShader(original.Put())) ||
        !scene_movie::MatchesShader(original.Get(),0x2e49799b0014f387ULL)) return false;

    DWORD size=sizeof(IDirect3DPixelShader9*);
    if (FAILED(source->GetPrivateData(movieShaderKey,current.Put(),&size))) {
        D3DCAPS9 caps{};
        if (FAILED(device->GetDeviceCaps(&caps)) || caps.PixelShaderVersion<D3DPS_VERSION(3,0) ||
            FAILED(device->CreatePixelShader(MoviePixel9,current.Put())) ||
            FAILED(source->SetPrivateData(movieShaderKey,current.Get(),size,D3DSPD_IUNKNOWN))) return false;
    }
    if (FAILED(device->SetPixelShader(current.Get()))) return false;
    result=device->DrawPrimitiveUP(type,count,vertices,stride);
    const auto restore=device->SetPixelShader(original.Get());
    if (SUCCEEDED(result) && FAILED(restore)) result=restore;
    return true;
}
} // namespace movie_reconstruction
