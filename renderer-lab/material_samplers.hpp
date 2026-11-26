#pragma once

#include "movie_d3d.hpp"
#include "legacy_pixel_shader.hpp"
#include <d3d9.h>
#include <d3d11shader.h>
#include <algorithm>
#include <cmath>

namespace material {

inline HRESULT ReadPixel1TextureStages(IDirect3DDevice9* device, legacy_pixel::Stages& stages) {
    if (!device) return E_INVALIDARG;
    legacy_pixel::Stages next{};
    for (unsigned i = 0; i < next.size(); ++i) {
        movie_pass::Com<IDirect3DBaseTexture9> texture;
        auto result = device->GetTexture(i, texture.Put());
        if (FAILED(result)) return result;
        if (texture.Get()) {
            switch (texture->GetType()) {
                case D3DRTYPE_TEXTURE: break;
                case D3DRTYPE_CUBETEXTURE: next[i].shape = legacy_pixel::TextureShape::Cube; break;
                case D3DRTYPE_VOLUMETEXTURE: next[i].shape = legacy_pixel::TextureShape::Volume; break;
                default: return E_NOTIMPL;
            }
        }
        DWORD flags{};
        result = device->GetTextureStageState(i, D3DTSS_TEXTURETRANSFORMFLAGS, &flags);
        if (FAILED(result)) return result;
        if (flags & D3DTTFF_PROJECTED) {
            next[i].projectionCount = flags & ~D3DTTFF_PROJECTED;
            if (next[i].projectionCount < 2 || next[i].projectionCount > 4) return E_NOTIMPL;
        }
    }
    stages = next;
    return S_OK;
}

struct Sampling {
    D3D11_SAMPLER_DESC state{};
    bool srgb = false;
};

inline HRESULT CaptureSampling(IDirect3DDevice9* source, DWORD slot, Sampling& sample) {
    if (!source) return E_INVALIDARG;
    std::array<DWORD, D3DSAMP_SRGBTEXTURE+1> values{};
    for (DWORD i = D3DSAMP_ADDRESSU; i <= D3DSAMP_SRGBTEXTURE; ++i) {
        auto result = source->GetSamplerState(slot, static_cast<D3DSAMPLERSTATETYPE>(i), &values[i]);
        if (FAILED(result)) return result;
    }
    for (DWORD i = D3DSAMP_ADDRESSU; i <= D3DSAMP_ADDRESSW; ++i)
        if (values[i] < D3DTADDRESS_WRAP || values[i] > D3DTADDRESS_MIRRORONCE) return E_NOTIMPL;
    auto min = values[D3DSAMP_MINFILTER], mag = values[D3DSAMP_MAGFILTER], mip = values[D3DSAMP_MIPFILTER];
    // NONE is accepted by the legacy effect runtime for underspecified samplers.
    // Rendering with such a MIN/MAG state has no reliable reference value.
    if (min < D3DTEXF_POINT || mag < D3DTEXF_POINT ||
        min > D3DTEXF_ANISOTROPIC || mag > D3DTEXF_ANISOTROPIC || mip > D3DTEXF_LINEAR) return E_NOTIMPL;
    Sampling next{};
    auto& d = next.state;
    d.AddressU = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(values[D3DSAMP_ADDRESSU]);
    d.AddressV = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(values[D3DSAMP_ADDRESSV]);
    d.AddressW = static_cast<D3D11_TEXTURE_ADDRESS_MODE>(values[D3DSAMP_ADDRESSW]);
    d.Filter = (min == D3DTEXF_ANISOTROPIC || mag == D3DTEXF_ANISOTROPIC)
        ? D3D11_FILTER_ANISOTROPIC : static_cast<D3D11_FILTER>(((min == D3DTEXF_LINEAR) << 4) |
                                      ((mag == D3DTEXF_LINEAR) << 2) | (mip == D3DTEXF_LINEAR));
    d.MaxAnisotropy = std::clamp<UINT>(values[D3DSAMP_MAXANISOTROPY], 1, 16);
    std::memcpy(&d.MipLODBias, &values[D3DSAMP_MIPMAPLODBIAS], sizeof(float));
    if (!std::isfinite(d.MipLODBias) || d.MipLODBias < -16.f || d.MipLODBias > 15.99f) return E_NOTIMPL;
    d.ComparisonFunc = D3D11_COMPARISON_NEVER;
    const auto color = values[D3DSAMP_BORDERCOLOR];
    d.BorderColor[0] = float((color >> 16) & 255) / 255;
    d.BorderColor[1] = float((color >> 8) & 255) / 255;
    d.BorderColor[2] = float(color & 255) / 255;
    d.BorderColor[3] = float(color >> 24) / 255;
    d.MinLOD = float(values[D3DSAMP_MAXMIPLEVEL]);
    d.MaxLOD = mip == D3DTEXF_NONE ? d.MinLOD : D3D11_FLOAT32_MAX;
    next.srgb = values[D3DSAMP_SRGBTEXTURE] != 0;
    sample = next;
    return S_OK;
}

// The host owns the texture cache and resource synchronization. Its resolver
// returns a borrowed SRV for a specific source resource, mip range and color space.
class Samplers11 {
    struct Slot {
        UINT index{};
        D3D_SRV_DIMENSION dimension{};
        D3D11_SAMPLER_DESC desc{};
        movie_pass::Com<ID3D11SamplerState> sampler;
    };
public:
    Samplers11(ID3D11Device* owner, ID3DBlob* stage) : device(owner) {
        movie_pass::Com<ID3D11ShaderReflection> shader;
        movie_pass::Require(D3DReflect(stage->GetBufferPointer(), stage->GetBufferSize(),
                                      IID_ID3D11ShaderReflection, shader.AsVoid()), "Reflect texture units");
        D3D11_SHADER_DESC desc{};
        movie_pass::Require(shader->GetDesc(&desc), "Read texture unit layout");
        const UINT type=desc.Version >> 16; // D3D11 shader version token program type.
        if (type>1) throw std::runtime_error("Invalid material sampler shader stage");
        vertex = type==1;
        for (UINT i = 0; i < desc.BoundResources; ++i) {
            D3D11_SHADER_INPUT_BIND_DESC binding{};
            movie_pass::Require(shader->GetResourceBindingDesc(i, &binding), "Read texture unit");
            if (binding.Type != D3D_SIT_TEXTURE) continue;
            if (binding.BindCount != 1 || binding.BindPoint >= (vertex ? 4u : 16u))
                throw std::runtime_error("Invalid legacy texture unit");
            slots[count].index = binding.BindPoint;
            slots[count++].dimension = binding.Dimension;
        }
    }
    UINT Count() const { return count; }
    template<class Resolver>
    HRESULT Bind(IDirect3DDevice9* legacy, ID3D11DeviceContext* context, Resolver&& resolve) {
        if (!legacy || !context) return E_INVALIDARG;
        std::array<ID3D11ShaderResourceView*,16> views{};
        for (UINT i = 0; i < count; ++i) {
            auto& slot = slots[i];
            const DWORD stage = slot.index + (vertex ? D3DVERTEXTEXTURESAMPLER0 : 0);
            Sampling sample{};
            auto result = CaptureSampling(legacy, stage, sample);
            if (FAILED(result)) return result;
            movie_pass::Com<IDirect3DBaseTexture9> texture;
            result = legacy->GetTexture(stage, texture.Put());
            if (FAILED(result)) return result;
            if (texture.Get()) {
                result = resolve(texture.Get(), sample.srgb, slot.dimension, &views[i]);
                if (FAILED(result) || !views[i]) return FAILED(result) ? result : E_FAIL;
                D3D11_SHADER_RESOURCE_VIEW_DESC view{};
                views[i]->GetDesc(&view);
                if (view.ViewDimension != slot.dimension) return E_INVALIDARG;
            }
            if (!slot.sampler.Get() || std::memcmp(&sample.state, &slot.desc, sizeof(slot.desc))) {
                movie_pass::Com<ID3D11SamplerState> next;
                result = device->CreateSamplerState(&sample.state, next.Put());
                if (FAILED(result)) return result;
                next.Swap(slot.sampler);
                slot.desc = sample.state;
            }
        }
        for (UINT i = 0; i < count; ++i) {
            auto* sampler = slots[i].sampler.Get();
            if (vertex) {
                context->VSSetSamplers(slots[i].index, 1, &sampler);
                context->VSSetShaderResources(slots[i].index, 1, &views[i]);
            } else {
                context->PSSetSamplers(slots[i].index, 1, &sampler);
                context->PSSetShaderResources(slots[i].index, 1, &views[i]);
            }
        }
        return S_OK;
    }
private:
    movie_pass::Com<ID3D11Device> device;
    std::array<Slot, 16> slots;
    UINT count = 0;
    bool vertex = false;
};
} // namespace material
