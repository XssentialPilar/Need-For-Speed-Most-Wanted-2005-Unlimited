#pragma once
#include "scene_hud_d3d12.hpp"
#include <mutex>
#include <algorithm>

namespace scene_movie {
using scene_hud::Com;
using scene_hud::Require;
#include "scene_movie_shaders.inc"
#include "scene_movie_composite_shaders.inc"

struct Vertex {
    float xyz[3];
    DWORD color;
    float uv[2], extra[2];
};
static_assert(sizeof(Vertex)==32);

template<class Shader>
bool MatchesShader(Shader* shader, UINT64 expected) {
    std::array<DWORD,512> words{};
    UINT bytes=0;
    if (!shader || FAILED(shader->GetFunction(nullptr,&bytes)) || !bytes || bytes%4 || bytes>sizeof(words) ||
        FAILED(shader->GetFunction(words.data(),&bytes))) return false;
    UINT64 hash=14695981039346656037ULL;
    const unsigned count=bytes/4;
    for (unsigned i=0;i<count;++i) {
        const DWORD word=words[i];
        if (i && (word&0xffff)==0xfffe) {
            const unsigned comment=(word>>16)&0x7fff;
            if (comment>=count-i) return false;
            i+=comment;
            continue;
        }
        for (unsigned shift=0;shift<32;shift+=8) {
            hash ^= (word>>shift)&255;
            hash *= 1099511628211ULL;
        }
    }
    return hash==expected;
}

struct State {
    D3DVIEWPORT9 viewport{};
    RECT scissor{};
    float constants[26]{};
    D3D12_RENDER_TARGET_BLEND_DESC blend{};
    D3D12_SAMPLER_DESC sampler{};
    D3D12_CULL_MODE cull = D3D12_CULL_MODE_NONE;
    UINT sampleMask = ~0u;
    bool srgbSample = false;
    bool forceOpaque = false;
};

inline bool ReadState(IDirect3DDevice9* d, State& s,const char** reason=nullptr) {
    const auto skip=[reason](const char* message) { if (reason) *reason=message;return false; };
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> ps;
    Com<IDirect3DVertexDeclaration9> declaration;
    if (FAILED(d->GetVertexShader(vs.Put())) || FAILED(d->GetPixelShader(ps.Put())) ||
        !MatchesShader(vs.Get(),0x48fbe4997fc40795ULL) || !MatchesShader(ps.Get(),0x2e49799b0014f387ULL))
        return skip("Unexpected FE vertex or pixel shader");
    if (FAILED(d->GetVertexDeclaration(declaration.Put())) || !declaration.Get()) return skip("Missing movie vertex declaration");
    D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};
    UINT elementCount=0;
    if (FAILED(declaration->GetDeclaration(nullptr,&elementCount)) || elementCount>std::size(elements) ||
        FAILED(declaration->GetDeclaration(elements,&elementCount))) return skip("Invalid movie declaration elements");
    bool position=false, color=false, uv=false;
    for (UINT i=0;i<elementCount;++i) {
        const auto& e=elements[i];
        if (e.Stream || e.UsageIndex || e.Method!=D3DDECLMETHOD_DEFAULT) continue;
        position |= e.Usage==D3DDECLUSAGE_POSITION && e.Type==D3DDECLTYPE_FLOAT3 && e.Offset==0;
        color |= e.Usage==D3DDECLUSAGE_COLOR && e.Type==D3DDECLTYPE_D3DCOLOR && e.Offset==12;
        uv |= e.Usage==D3DDECLUSAGE_TEXCOORD && e.Type>=D3DDECLTYPE_FLOAT2 && e.Type<=D3DDECLTYPE_FLOAT4 && e.Offset==16;
    }
    if (!position || !color || !uv || FAILED(d->GetVertexShaderConstantF(0,s.constants,5)) ||
        FAILED(d->GetViewport(&s.viewport)) || !s.viewport.Width || !s.viewport.Height) return skip("Unsupported movie vertices, constants or viewport");
    auto rs=[&](D3DRENDERSTATETYPE state) {
        DWORD value=0;
        Require(d->GetRenderState(state,&value),"Read FE movie state");
        return value;
    };
    auto sample=[&](D3DSAMPLERSTATETYPE state) {
        DWORD value=0;
        Require(d->GetSamplerState(0,state,&value),"Read FE video sampler");
        return value;
    };
    if (rs(D3DRS_ZENABLE) && (rs(D3DRS_ZWRITEENABLE) || rs(D3DRS_ZFUNC)!=D3DCMP_ALWAYS)) return skip("Movie has depth test or write");
    if (rs(D3DRS_STENCILENABLE)) return skip("Movie has stencil");
    if (rs(D3DRS_FOGENABLE)) return skip("Movie has fog");
    if (rs(D3DRS_CLIPPLANEENABLE)) return skip("Movie has user clip plane");
    if (rs(D3DRS_SRGBWRITEENABLE)) return skip("Movie has sRGB output");
    if (rs(D3DRS_FILLMODE)!=D3DFILL_SOLID) return skip("Movie uses nonsolid fill");
    s.scissor={LONG(s.viewport.X),LONG(s.viewport.Y),LONG(s.viewport.X+s.viewport.Width),LONG(s.viewport.Y+s.viewport.Height)};
    if (rs(D3DRS_SCISSORTESTENABLE) && FAILED(d->GetScissorRect(&s.scissor))) return skip("Missing movie scissor bounds");
    const DWORD func=rs(D3DRS_ALPHATESTENABLE)?rs(D3DRS_ALPHAFUNC):D3DCMP_ALWAYS;
    const DWORD ref=rs(D3DRS_ALPHAREF)&255;
    std::memcpy(s.constants+24,&func,4);
    std::memcpy(s.constants+25,&ref,4);
    s.constants[20]=1.f/s.viewport.Width;
    s.constants[21]=-1.f/s.viewport.Height;
    s.cull=rs(D3DRS_CULLMODE)==D3DCULL_CCW?D3D12_CULL_MODE_BACK:
           rs(D3DRS_CULLMODE)==D3DCULL_CW?D3D12_CULL_MODE_FRONT:D3D12_CULL_MODE_NONE;
    s.sampleMask=rs(D3DRS_MULTISAMPLEMASK);
    auto& b=s.blend;
    b.BlendEnable=rs(D3DRS_ALPHABLENDENABLE)!=0;
    b.SrcBlend=D3D12_BLEND(rs(D3DRS_SRCBLEND)); b.DestBlend=D3D12_BLEND(rs(D3DRS_DESTBLEND));
    b.BlendOp=D3D12_BLEND_OP(rs(D3DRS_BLENDOP));
    const bool separate=rs(D3DRS_SEPARATEALPHABLENDENABLE)!=0;
    auto forAlpha=[](D3D12_BLEND f) {
        switch (f) {
            case D3D12_BLEND_SRC_COLOR: return D3D12_BLEND_SRC_ALPHA;
            case D3D12_BLEND_INV_SRC_COLOR: return D3D12_BLEND_INV_SRC_ALPHA;
            case D3D12_BLEND_DEST_COLOR: return D3D12_BLEND_DEST_ALPHA;
            case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
            default: return f;
        }
    };
    b.SrcBlendAlpha=forAlpha(separate?D3D12_BLEND(rs(D3DRS_SRCBLENDALPHA)):b.SrcBlend);
    b.DestBlendAlpha=forAlpha(separate?D3D12_BLEND(rs(D3DRS_DESTBLENDALPHA)):b.DestBlend);
    if (b.SrcBlend>D3D12_BLEND_SRC_ALPHA_SAT || b.DestBlend>D3D12_BLEND_SRC_ALPHA_SAT ||
        b.SrcBlendAlpha>D3D12_BLEND_SRC_ALPHA_SAT || b.DestBlendAlpha>D3D12_BLEND_SRC_ALPHA_SAT) return skip("Unsupported movie blend factor");
    b.BlendOpAlpha=separate?D3D12_BLEND_OP(rs(D3DRS_BLENDOPALPHA)):b.BlendOp;
    b.LogicOp=D3D12_LOGIC_OP_NOOP;
    b.RenderTargetWriteMask=UINT8(rs(D3DRS_COLORWRITEENABLE)&15);
    const auto min=sample(D3DSAMP_MINFILTER), mag=sample(D3DSAMP_MAGFILTER);
    if (min<1 || min>3 || mag<1 || mag>3) return skip("Unsupported movie filter");
    s.sampler.Filter=min==3 || mag==3 ? D3D12_FILTER_ANISOTROPIC :
        static_cast<D3D12_FILTER>((min==2?16:0) | (mag==2?4:0));
    s.sampler.MaxAnisotropy=std::clamp<UINT>(sample(D3DSAMP_MAXANISOTROPY),1,16);
    s.sampler.AddressU=D3D12_TEXTURE_ADDRESS_MODE(sample(D3DSAMP_ADDRESSU));
    s.sampler.AddressV=D3D12_TEXTURE_ADDRESS_MODE(sample(D3DSAMP_ADDRESSV));
    s.sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    const auto border=sample(D3DSAMP_BORDERCOLOR);
    for (UINT i=0;i<4;++i) s.sampler.BorderColor[i]=float((border>>(i==3?24:(2-i)*8))&255)/255.f;
    s.sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    s.sampler.MaxLOD=0;
    s.srgbSample=sample(D3DSAMP_SRGBTEXTURE)!=0;
    return true;
}

struct Decoded {
    UINT width=0,height=0;
    std::vector<BYTE> bgra;
    UINT64 stamp=0;
    IDirect3DTexture9* texture=nullptr; // identity only; the engine owns the source
};

} // namespace scene_movie
