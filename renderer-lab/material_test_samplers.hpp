#pragma once
#include "material_samplers.hpp"
#include <d3dx9.h>

namespace sampler_test {

using movie_pass::Com;
using movie_pass::Require;

class Textures {
    struct Pair {
        Com<IDirect3DBaseTexture9> old;
        Com<ID3D11ShaderResourceView> linear,srgb;
    };
public:
    Textures(IDirect3DDevice9* source, ID3D11Device* dest) {
        for (unsigned i=0;i<pairs.size();++i) {
            auto& p=pairs[i];
            const bool cube=i==1,volume=i==2;
            Com<ID3D11Resource> modern;
            if (volume) {
                Com<IDirect3DVolumeTexture9> t9;
                Require(source->CreateVolumeTexture(8,8,8,2,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,t9.Put(),nullptr),"Create legacy volume material");
                Require(t9->QueryInterface(__uuidof(IDirect3DBaseTexture9),p.old.AsVoid()),"Read base volume texture");
                D3D11_TEXTURE3D_DESC d{};
                d.Width=d.Height=d.Depth=8;d.MipLevels=2;d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                Com<ID3D11Texture3D> t11;
                Require(dest->CreateTexture3D(&d,nullptr,t11.Put()),"Create native volume material");
                Require(t11->QueryInterface(__uuidof(ID3D11Resource),modern.AsVoid()),"Read native volume resource");
            } else {
                if (cube) {
                    Com<IDirect3DCubeTexture9> t9;
                    Require(source->CreateCubeTexture(8,2,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,t9.Put(),nullptr),"Create legacy cubemap");
                    Require(t9->QueryInterface(__uuidof(IDirect3DBaseTexture9),p.old.AsVoid()),"Read base cube texture");
                } else {
                    Com<IDirect3DTexture9> t9;
                    Require(source->CreateTexture(8,8,2,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,t9.Put(),nullptr),"Create legacy material map");
                    Require(t9->QueryInterface(__uuidof(IDirect3DBaseTexture9),p.old.AsVoid()),"Read base material texture");
                }
                D3D11_TEXTURE2D_DESC d{};
                d.Width=d.Height=8;d.ArraySize=cube?6:1;d.MipLevels=2;d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
                d.BindFlags=D3D11_BIND_SHADER_RESOURCE;d.MiscFlags=cube?D3D11_RESOURCE_MISC_TEXTURECUBE:0;
                Com<ID3D11Texture2D> t11;
                Require(dest->CreateTexture2D(&d,nullptr,t11.Put()),"Create native material map");
                Require(t11->QueryInterface(__uuidof(ID3D11Resource),modern.AsVoid()),"Read native material resource");
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};
            view.ViewDimension=volume?D3D11_SRV_DIMENSION_TEXTURE3D:cube?D3D11_SRV_DIMENSION_TEXTURECUBE:D3D11_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels=2;
            view.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            Require(dest->CreateShaderResourceView(modern.Get(),&view,p.linear.Put()),"Create linear material view");
            view.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            Require(dest->CreateShaderResourceView(modern.Get(),&view,p.srgb.Put()),"Create sRGB material view");
        }
    }
    void Verify(material::Samplers11& samplers,ID3D11ShaderReflection* reflect,IDirect3DDevice9* d,ID3D11DeviceContext* context,
                bool vertex,unsigned iteration) {
        D3D11_SHADER_DESC layout{};
        Require(reflect->GetDesc(&layout),"Read reflected texture map");
        struct Binding { UINT index; Pair* texture; bool srgb; };
        std::vector<Binding> units;
        for (UINT i=0;i<layout.BoundResources;++i) {
            D3D11_SHADER_INPUT_BIND_DESC b{};
            Require(reflect->GetResourceBindingDesc(i,&b),"Read reflected material input");
            if (b.Type!=D3D_SIT_TEXTURE) continue;
            const unsigned kind=b.Dimension==D3D_SRV_DIMENSION_TEXTURE2D ? 0 : b.Dimension==D3D_SRV_DIMENSION_TEXTURECUBE ? 1 :
                                b.Dimension==D3D_SRV_DIMENSION_TEXTURE3D ? 2 : 3;
            if (kind>=pairs.size()) throw std::runtime_error("Unexpected reflected texture shape");
            const DWORD slot=b.BindPoint+(vertex?D3DVERTEXTEXTURESAMPLER0:0);
            const bool srgb=(b.BindPoint+iteration)%2!=0;
            auto& pair=pairs[kind];
            Require(d->SetTexture(slot,pair.old.Get()),"Bind live legacy material texture");
            Require(d->SetSamplerState(slot,D3DSAMP_SRGBTEXTURE,srgb),"Set live material transfer function");
            Require(d->SetSamplerState(slot,D3DSAMP_MINFILTER,iteration?D3DTEXF_LINEAR:D3DTEXF_POINT),"Set minification");
            Require(d->SetSamplerState(slot,D3DSAMP_MAGFILTER,iteration?D3DTEXF_POINT:D3DTEXF_LINEAR),"Set magnification");
            Require(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,iteration?D3DTEXF_NONE:D3DTEXF_LINEAR),"Set mip filter");
            Require(d->SetSamplerState(slot,D3DSAMP_MAXMIPLEVEL,iteration),"Set first texture mip");
            Require(d->SetSamplerState(slot,D3DSAMP_ADDRESSU,iteration?D3DTADDRESS_CLAMP:D3DTADDRESS_MIRROR),"Set texture addressing");
            units.push_back({b.BindPoint,&pair,srgb});++shapeCounts[kind];
        }
        if (samplers.Count()!=units.size()) throw std::runtime_error("Missing native texture unit");
        auto resolve=[&](IDirect3DBaseTexture9* original,bool srgb,D3D_SRV_DIMENSION,ID3D11ShaderResourceView** view)->HRESULT {
            for (auto& p : pairs) if (p.old.Get()==original) {
                *view=srgb?p.srgb.Get():p.linear.Get();return S_OK;
            }
            return E_FAIL;
        };
        Require(samplers.Bind(d,context,resolve),"Bind live texture and sampler units");
        for (const auto& u : units) {
            Com<ID3D11SamplerState> sampler;
            Com<ID3D11ShaderResourceView> texture;
            if (vertex) {
                context->VSGetSamplers(u.index,1,sampler.Put());context->VSGetShaderResources(u.index,1,texture.Put());
            } else {
                context->PSGetSamplers(u.index,1,sampler.Put());context->PSGetShaderResources(u.index,1,texture.Put());
            }
            if (!sampler.Get() || texture.Get()!=(u.srgb?u.texture->srgb.Get():u.texture->linear.Get()))
                throw std::runtime_error("Unexpected texture identity or color space");
            D3D11_SAMPLER_DESC bound{};
            sampler->GetDesc(&bound);
            if (bound.Filter!=(iteration?D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT:D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR) ||
                bound.AddressU!=(iteration?D3D11_TEXTURE_ADDRESS_CLAMP:D3D11_TEXTURE_ADDRESS_MIRROR) ||
                bound.MinLOD!=iteration || bound.MaxLOD!=(iteration?1.f:D3D11_FLOAT32_MAX))
                throw std::runtime_error("Incorrect native material sampler");
            ++checks;
        }
        if (!units.empty()) {
            const DWORD samplerSlot=units[0].index+(vertex?D3DVERTEXTEXTURESAMPLER0:0);
            Require(d->SetTexture(samplerSlot,nullptr),"Clear legacy slot");
            Require(samplers.Bind(d,context,resolve),"Update cleared native slot");
            Com<ID3D11ShaderResourceView> unbound;
            if(vertex) context->VSGetShaderResources(units[0].index,1,unbound.Put());
            else context->PSGetShaderResources(units[0].index,1,unbound.Put());
            if(unbound.Get()) throw std::runtime_error("Stale native material texture");
            ++clears;
        }
    }
    void SetTopology(IDirect3DDevice9* device,unsigned kind,UINT count) {
        Require(device->SetTexture(0,pairs.at(kind).old.Get()),"Bind material topology");
        Require(device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,count?D3DTTFF_PROJECTED|count:0),"Set material projection");
    }
    unsigned checks{},clears{};
    std::array<unsigned,3> shapeCounts{};
private:
    std::array<Pair,3> pairs;
};
} // namespace sampler_test
