#pragma once
#include "movie_d3d.hpp"
#include "legacy_pixel_shader.hpp"
#include "legacy_fixed_pixel.hpp"
#include "legacy_fixed_vertex.hpp"
#include <d3dx9.h>
#include <array>
#include <cmath>

namespace legacy_comparison {

using movie_pass::Com;
using movie_pass::Require;
constexpr UINT Side = 16, TextureSide = 8, StageCount = 4;
using Image = std::array<unsigned char, Side * Side * 4>;
using Constants = std::array<std::array<float, 4>, 8>;

struct FixedGeometry {
    std::vector<unsigned char> bytes;
    std::vector<D3D11_INPUT_ELEMENT_DESC> layout;
    UINT stride = 0;
    explicit FixedGeometry(IDirect3DDevice9* d) {
        Com<IDirect3DVertexDeclaration9> decl;
        Require(d->GetVertexDeclaration(decl.Put()), "Get effective geometry declaration");
        D3DVERTEXELEMENT9 elements[MAX_FVF_DECL_SIZE]{};
        UINT count = MAX_FVF_DECL_SIZE;
        Require(decl->GetDeclaration(elements, &count), "Get geometry elements");
        for (unsigned i = 0; i < count && elements[i].Stream != 0xff; ++i) {
            const auto& e = elements[i];
            const char* semantic = e.Usage == D3DDECLUSAGE_POSITION ? "POSITION" : e.Usage == D3DDECLUSAGE_NORMAL ? "NORMAL" :
                                   e.Usage == D3DDECLUSAGE_COLOR ? "COLOR" : e.Usage == D3DDECLUSAGE_TEXCOORD ? "TEXCOORD" : nullptr;
            if (!semantic || e.Stream != 0) throw std::runtime_error("Unsupported fixture vertex declaration");
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            unsigned size = 0;
            if (e.Type == D3DDECLTYPE_D3DCOLOR) { format = DXGI_FORMAT_B8G8R8A8_UNORM; size = 4; }
            if (e.Type == D3DDECLTYPE_FLOAT3) { format = DXGI_FORMAT_R32G32B32_FLOAT; size = 12; }
            if (e.Type == D3DDECLTYPE_FLOAT2) { format = DXGI_FORMAT_R32G32_FLOAT; size = 8; }
            if (!size) throw std::runtime_error("Unsupported fixture vertex element type");
            stride = std::max(stride, e.Offset + size);
            layout.push_back({semantic, e.UsageIndex, format, 0, e.Offset, D3D11_INPUT_PER_VERTEX_DATA, 0});
        }
        const float pos[][3] = {{-.85f,-.83f,.41f},{.97f,-.68f,.53f},{-.46f,.96f,.59f}};
        const float uv[][2] = {{.011f,.073f},{.91f,.17f},{.21f,1.12f}};
        bytes.resize(3 * stride);
        for (unsigned n = 0; n < 3; ++n) for (const auto& e : layout) {
            auto* dst = bytes.data() + n * stride + e.AlignedByteOffset;
            if (!std::strcmp(e.SemanticName, "POSITION")) std::memcpy(dst, pos[n], 12);
            if (!std::strcmp(e.SemanticName, "TEXCOORD")) {
                const float data[] = {uv[n][0] + .13f*e.SemanticIndex, uv[n][1] - .08f*e.SemanticIndex};
                std::memcpy(dst, data, 8);
            }
            if (!std::strcmp(e.SemanticName, "NORMAL")) {
                const float normal[] = {.22f+.12f*n, .11f-.08f*n, -.81f};
                std::memcpy(dst, normal, 12);
            }
            if (!std::strcmp(e.SemanticName, "COLOR")) {
                DWORD color = D3DCOLOR_ARGB(110+31*n,40+51*n,182-31*n,35+28*n);
                if (e.SemanticIndex) color = D3DCOLOR_ARGB(74,33+12*n,69-4*n,27);
                std::memcpy(dst, &color, 4);
            }
        }
    }
};

struct Vertex { float x, y, z, w, u, v, s, t; };
constexpr Vertex Triangle[] = {{-1, 1, .5f, 1, .013f, .029f, .31f, 1},
                               { 3, 1, .5f, 1, 2.013f, .029f, .31f, 1},
                               {-1,-3, .5f, 1, .013f, 2.029f, .31f, 1}};

inline Constants Inputs(unsigned variant) {
    Constants values{};
    for (unsigned i = 0; i < values.size(); ++i)
        for (unsigned j = 0; j < 4; ++j)
            values[i][j] = static_cast<float>((17 * i + 23 * j + variant * 31 + 5) % 128) / 128;
    return values;
}

inline void VertexShader(bool legacy, std::string& text) {
    text = "struct V { float4 pos : ";
    text += legacy ? "POSITION;" : "SV_Position;";
    text += "float4 a : COLOR0; float4 b : COLOR1;";
    for (unsigned i = 0; i < 6; ++i) text += "float4 t" + std::to_string(i) + " : TEXCOORD" + std::to_string(i) + ";";
    text += R"(};
V main(float4 p : POSITION, float4 uv : TEXCOORD0) {
    V o;
    o.pos=p;
    o.a=saturate(float4(0.25+uv.y*0.5, 0.75, 0.25+uv.x*0.5, 0.375));
    o.b=saturate(float4(0.75, 0.5+uv.y*0.25, 0.125, 0.5+uv.x*0.25));
    o.t0=uv;
    o.t1=float4(0.75-uv.y,uv.x,0.61,1);
    o.t2=float4(uv.y,0.83-uv.x,0.25,1);
    o.t3=float4(0.29+uv.x,uv.y-0.23,0.41,1);
    o.t4=uv.wzyx;
    o.t5=uv.zwxy;
    return o;
})";
}

inline DWORD Texel(unsigned stage, unsigned x, unsigned y) {
    return D3DCOLOR_ARGB(33 + (stage * 17 + x * 11 + y * 5) % 209,
                         12 + (x * 27 + y * 13 + stage * 19) % 218,
                         19 + (y * 31 + stage * 41 + x * 5) % 217,
                         10 + (x * 13 + y * 17 + stage * 57) % 231);
}

inline void Compile(const std::string& hlsl, const char* entry, const char* model, Com<ID3DBlob>& out) {
    Com<ID3DBlob> errors;
    auto hr = D3DCompile(hlsl.data(), hlsl.size(), nullptr, nullptr, nullptr, entry, model, D3DCOMPILE_ENABLE_STRICTNESS,
                         0, out.Put(), errors.Put());
    if (FAILED(hr) && errors.Get()) throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
    Require(hr, "Compile HLSL fixture");
}

inline void CopyRows(Image& out, const void* data, size_t pitch) {
    for (unsigned y = 0; y < Side; ++y)
        std::memcpy(out.data() + y * Side * 4, static_cast<const unsigned char*>(data) + y * pitch, Side * 4);
}

class Legacy {
public:
    explicit Legacy(IDirect3DDevice9* device) : d(device) {
        Require(d->CreateRenderTarget(Side, Side, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                      target.Put(), nullptr), "D3D9 test target");
        Require(d->CreateOffscreenPlainSurface(Side, Side, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, read.Put(), nullptr),
                "D3D9 test staging");
        Require(d->SetRenderTarget(0, target.Get()), "Bind D3D9 test target");
        Require(d->SetDepthStencilSurface(nullptr), "Unbind D3D9 depth");
        D3DVIEWPORT9 viewport{0, 0, Side, Side, 0, 1};
        Require(d->SetViewport(&viewport), "D3D9 test viewport");
        Require(d->SetRenderState(D3DRS_ZENABLE, FALSE), "Disable depth");
        Require(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "Disable culling");
        Require(d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "Disable blending");
        Require(d->SetRenderState(D3DRS_DITHERENABLE, FALSE), "Disable dithering");
        Require(d->SetRenderState(D3DRS_FOGENABLE, FALSE), "Disable fog");

        std::string hlsl;
        VertexShader(true, hlsl);
        Com<ID3DXBuffer> bytecode, message;
        const HRESULT hr = D3DXCompileShader(hlsl.data(), static_cast<UINT>(hlsl.size()), nullptr, nullptr,
                                              "main", "vs_2_0", 0, bytecode.Put(), message.Put(), nullptr);
        if (FAILED(hr) && message.Get()) throw std::runtime_error(static_cast<const char*>(message->GetBufferPointer()));
        Require(hr, "Compile D3D9 test input vertex shader");
        Require(d->CreateVertexShader(static_cast<const DWORD*>(bytecode->GetBufferPointer()), vertex.Put()), "Create D3D9 test VS");
        Require(d->SetVertexShader(vertex.Get()), "Bind D3D9 test VS");
        constexpr D3DVERTEXELEMENT9 decl[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0},
                                              {0, 16, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
        Require(d->CreateVertexDeclaration(decl, layout.Put()), "D3D9 test layout");
        Require(d->SetVertexDeclaration(layout.Get()), "Bind D3D9 test layout");
        for (unsigned s = 0; s < StageCount; ++s) {
            auto& texture = textures[s];
            Require(d->CreateTexture(TextureSide, TextureSide, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                                     D3DPOOL_DEFAULT, texture.Put(), nullptr), "Create D3D9 test texture");
            D3DLOCKED_RECT locked{};
            Require(texture->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD), "Lock D3D9 test texture");
            for (unsigned y = 0; y < TextureSide; ++y) {
                auto row = reinterpret_cast<DWORD*>(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch);
                for (unsigned x = 0; x < TextureSide; ++x) row[x] = Texel(s, x, y);
            }
            Require(texture->UnlockRect(0), "Unlock D3D9 texture");
            Require(d->SetTexture(s, texture.Get()), "Bind D3D9 texture");
            Require(d->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT), "D3D9 point min");
            Require(d->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT), "D3D9 point mag");
            Require(d->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP), "D3D9 U wrap");
            Require(d->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP), "D3D9 V wrap");
        }
    }

    Image Draw(const void* shader, const Constants& values) {
        Com<IDirect3DPixelShader9> ps;
        Require(d->CreatePixelShader(static_cast<const DWORD*>(shader), ps.Put()), "Create original D3D9 PS");
        Require(d->SetPixelShader(ps.Get()), "Bind original D3D9 PS");
        Require(d->SetPixelShaderConstantF(0, values.front().data(), 8), "D3D9 PS constants");
        return DrawBound();
    }

    IDirect3DTexture9* Texture(unsigned stage) const { return textures.at(stage).Get(); }

    Image DrawFixedGeometry(const legacy_fixed::PixelState& state, const FixedGeometry& draw) {
        // Retain the original post-BeginPass fixed vertex and pixel stages.
        // Isolate their output from unrelated framebuffer tests and filtering.
        Require(d->SetRenderState(D3DRS_ZENABLE, FALSE), "Disable fixture depth");
        Require(d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE), "Disable fixture culling");
        Require(d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE), "Disable fixture blending");
        Require(d->SetRenderState(D3DRS_COLORWRITEENABLE, 15), "Enable fixture color channels");
        for (unsigned n = 0; n < StageCount; ++n) {
            Require(d->SetTexture(n, state.stages[n].texture ? Texture(n) : nullptr), "Match full-pass stimulus");
            Require(d->SetSamplerState(n, D3DSAMP_MINFILTER, D3DTEXF_POINT), "Match fixed min filter");
            Require(d->SetSamplerState(n, D3DSAMP_MAGFILTER, D3DTEXF_POINT), "Match fixed mag filter");
            Require(d->SetSamplerState(n, D3DSAMP_MIPFILTER, D3DTEXF_NONE), "Match fixed mip filter");
            Require(d->SetSamplerState(n, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP), "Match fixed U addressing");
            Require(d->SetSamplerState(n, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP), "Match fixed V addressing");
        }
        return DrawGeometry(draw.bytes.data(), draw.stride);
    }

    Image DrawCombiner(const legacy_fixed::PixelState& state) {
        Require(d->SetPixelShader(nullptr), "Bind D3D9 fixed pixel stage");
        Require(d->SetVertexShader(vertex.Get()), "Bind test interpolants");
        Require(d->SetVertexDeclaration(layout.Get()), "Bind test declaration");
        // The fixture vertex shader already produces one post-T&L coordinate
        // per texture stage. NVIDIA/AMD still apply the projective flag in PS.
        for (unsigned n = 0; n < state.stages.size(); ++n) {
            const auto& s = state.stages[n];
            auto set = [&](D3DTEXTURESTAGESTATETYPE type, DWORD value) {
                Require(d->SetTextureStageState(n, type, value), "Set captured fixed-function stage");
            };
            set(D3DTSS_TEXCOORDINDEX, n);
            set(D3DTSS_TEXTURETRANSFORMFLAGS, s.transform & D3DTTFF_PROJECTED ? s.transform : 0);
            set(D3DTSS_COLOROP, s.rgb.code);
            set(D3DTSS_COLORARG0, s.rgb.args[0]);
            set(D3DTSS_COLORARG1, s.rgb.args[1]);
            set(D3DTSS_COLORARG2, s.rgb.args[2]);
            set(D3DTSS_ALPHAOP, s.alpha.code);
            set(D3DTSS_ALPHAARG0, s.alpha.args[0]);
            set(D3DTSS_ALPHAARG1, s.alpha.args[1]);
            set(D3DTSS_ALPHAARG2, s.alpha.args[2]);
            set(D3DTSS_RESULTARG, s.result);
            Require(d->SetTexture(n, s.texture ? Texture(n) : nullptr), "Set matched fixed-function texture");
            set(D3DTSS_CONSTANT, Pack(state.constants.stage[n]));
        }
        Require(d->SetRenderState(D3DRS_TEXTUREFACTOR, Pack(state.constants.factor)), "Set texture factor");
        Require(d->SetRenderState(D3DRS_SPECULARENABLE, state.specular), "Set fixed-function specular");
        Require(d->SetRenderState(D3DRS_ALPHATESTENABLE, state.alphaTest), "Set fixed-function alpha test");
        Require(d->SetRenderState(D3DRS_ALPHAFUNC, state.alphaFunction), "Set fixed-function alpha comparison");
        Require(d->SetRenderState(D3DRS_ALPHAREF, DWORD(state.constants.alphaRef[0])), "Set fixed-function alpha reference");
        return DrawBound();
    }

private:
    static DWORD Pack(const std::array<float,4>& rgba) {
        return D3DCOLOR_ARGB(unsigned(std::lround(rgba[3]*255)), unsigned(std::lround(rgba[0]*255)),
                             unsigned(std::lround(rgba[1]*255)), unsigned(std::lround(rgba[2]*255)));
    }
    Image DrawBound() {
        Vertex geometry[3];
        std::memcpy(geometry, Triangle, sizeof(geometry));
        for (auto& v : geometry) { v.x -= 1.f / Side; v.y += 1.f / Side; }
        return DrawGeometry(geometry, sizeof(Vertex));
    }
    Image DrawGeometry(const void* data, UINT stride) {
        Require(d->Clear(0, nullptr, D3DCLEAR_TARGET, 0xffff00ff, 1, 0), "Clear D3D9 test target");
        Require(d->BeginScene(), "Begin D3D9 test");
        Require(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, data, stride), "Draw original D3D9 pipeline");
        Require(d->EndScene(), "End D3D9 test");
        Require(d->GetRenderTargetData(target.Get(), read.Get()), "Copy D3D9 test output");
        D3DLOCKED_RECT locked{};
        Require(read->LockRect(&locked, nullptr, D3DLOCK_READONLY), "Lock D3D9 test output");
        Image image{};
        CopyRows(image, locked.pBits, locked.Pitch);
        Require(read->UnlockRect(), "Unlock D3D9 test output");
        return image;
    }
private:
    IDirect3DDevice9* d;
    Com<IDirect3DSurface9> target, read;
    Com<IDirect3DVertexShader9> vertex;
    Com<IDirect3DVertexDeclaration9> layout;
    std::array<Com<IDirect3DTexture9>, StageCount> textures;
};

class Modern {
public:
    explicit Modern(ID3D11Device* device) : d(device) {
        d->GetImmediateContext(ctx.Put());
        std::string hlsl;
        VertexShader(false, hlsl);
        Com<ID3DBlob> code;
        Compile(hlsl, "main", "vs_5_0", code);
        Require(d->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, vertex.Put()), "Create native test VS");
        ctx->VSSetShader(vertex.Get(), nullptr, 0);
        constexpr D3D11_INPUT_ELEMENT_DESC decl[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}
        };
        Require(d->CreateInputLayout(decl, 2, code->GetBufferPointer(), code->GetBufferSize(), layout.Put()), "Native test layout");
        ctx->IASetInputLayout(layout.Get());
        D3D11_BUFFER_DESC buf{};
        buf.ByteWidth = sizeof(Triangle);
        buf.Usage = D3D11_USAGE_IMMUTABLE;
        buf.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{Triangle, 0, 0};
        Require(d->CreateBuffer(&buf, &data, geometry.Put()), "Create native test vertices");
        UINT stride = sizeof(Vertex), offset = 0;
        auto* vb = geometry.Get();
        ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D11_TEXTURE2D_DESC image{};
        image.Width = image.Height = Side;
        image.MipLevels = image.ArraySize = image.SampleDesc.Count = 1;
        image.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        image.BindFlags = D3D11_BIND_RENDER_TARGET;
        Require(d->CreateTexture2D(&image, nullptr, target.Put()), "Native test target");
        Require(d->CreateRenderTargetView(target.Get(), nullptr, view.Put()), "Native test view");
        image.BindFlags = 0;
        image.Usage = D3D11_USAGE_STAGING;
        image.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Require(d->CreateTexture2D(&image, nullptr, read.Put()), "Native test staging");
        image.Width = image.Height = TextureSide;
        image.Usage = D3D11_USAGE_IMMUTABLE;
        image.CPUAccessFlags = 0;
        image.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SAMPLER_DESC sampling{};
        sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampling.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sampling.MaxAnisotropy = 1;
        sampling.MaxLOD = D3D11_FLOAT32_MAX;
        Require(d->CreateSamplerState(&sampling, sampler.Put()), "Native point sampler");
        for (unsigned s = 0; s < StageCount; ++s) {
            std::array<DWORD, TextureSide * TextureSide> pixels{};
            for (unsigned y = 0; y < TextureSide; ++y)
                for (unsigned x = 0; x < TextureSide; ++x) pixels[y * TextureSide + x] = Texel(s, x, y);
            Com<ID3D11Texture2D> tex;
            D3D11_SUBRESOURCE_DATA packed{pixels.data(), TextureSide * 4, 0};
            Require(d->CreateTexture2D(&image, &packed, tex.Put()), "Native test texture");
            Require(d->CreateShaderResourceView(tex.Get(), nullptr, textures[s].Put()), "Native test texture view");
            auto* srv = textures[s].Get();
            auto* sample = sampler.Get();
            ctx->PSSetShaderResources(s, 1, &srv);
            ctx->PSSetSamplers(s, 1, &sample);
        }
        const D3D11_VIEWPORT viewport{0, 0, Side, Side, 0, 1};
        ctx->RSSetViewports(1, &viewport);
        D3D11_RASTERIZER_DESC params{};
        params.FillMode = D3D11_FILL_SOLID;
        params.CullMode = D3D11_CULL_NONE;
        params.DepthClipEnable = TRUE;
        Require(d->CreateRasterizerState(&params, raster.Put()), "Native test rasterizer");
        ctx->RSSetState(raster.Get());
    }

    Image Draw(const void* shader, size_t length, const Constants& values) {
        return DrawHlsl(legacy_pixel::Translate(shader, length), values.data(), sizeof(values));
    }

    Image DrawFixedGeometry(const legacy_fixed::VertexState& state, const legacy_fixed::PixelState& pixel, const FixedGeometry& draw) {
        Com<ID3DBlob> code;
        Compile(legacy_fixed::VertexShader(state), legacy_pixel::Entry, "vs_5_0", code);
        Com<ID3D11VertexShader> fixedVS;
        Com<ID3D11InputLayout> fixedLayout;
        Require(d->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, fixedVS.Put()), "Create fixed native VS");
        Require(d->CreateInputLayout(draw.layout.data(), UINT(draw.layout.size()), code->GetBufferPointer(), code->GetBufferSize(),
                                     fixedLayout.Put()), "Create fixed native layout");
        auto buffer = [&](Com<ID3D11Buffer>& out, const void* ptr, UINT bytes, UINT bind) {
            const D3D11_BUFFER_DESC desc{bytes,D3D11_USAGE_IMMUTABLE,bind,0,0,0};
            const D3D11_SUBRESOURCE_DATA data{ptr,0,0};
            Require(d->CreateBuffer(&desc,&data,out.Put()), "Make full-pass stimulus");
        };
        Com<ID3D11Buffer> vb, cb;
        buffer(vb, draw.bytes.data(), UINT(draw.bytes.size()), D3D11_BIND_VERTEX_BUFFER);
        buffer(cb, &state.constants, sizeof(state.constants), D3D11_BIND_CONSTANT_BUFFER);
        UINT offset = 0, stride = draw.stride;
        auto* cbPtr = cb.Get(); auto* vbPtr = vb.Get();
        ctx->VSSetConstantBuffers(0,1,&cbPtr);
        ctx->IASetInputLayout(fixedLayout.Get());
        ctx->IASetVertexBuffers(0,1,&vbPtr,&stride,&offset);
        ctx->VSSetShader(fixedVS.Get(),nullptr,0);
        auto result = DrawHlsl(legacy_fixed::PixelShader(pixel),&pixel.constants,sizeof(pixel.constants));
        stride = sizeof(Vertex);
        vbPtr = geometry.Get();
        ctx->IASetInputLayout(layout.Get());
        ctx->IASetVertexBuffers(0,1,&vbPtr,&stride,&offset);
        ctx->VSSetShader(vertex.Get(),nullptr,0);
        return result;
    }

    Image DrawHlsl(const std::string& hlsl, const void* values, UINT size) {
        Com<ID3DBlob> code;
        Compile(hlsl, legacy_pixel::Entry, "ps_5_0", code);
        Com<ID3D11PixelShader> ps;
        Require(d->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, ps.Put()), "Native test PS");
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        const D3D11_BUFFER_DESC buffer{size, D3D11_USAGE_IMMUTABLE, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
        const D3D11_SUBRESOURCE_DATA initialized{values, 0, 0};
        Com<ID3D11Buffer> constants;
        Require(d->CreateBuffer(&buffer, &initialized, constants.Put()), "Create fixture PS constants");
        auto* cb = constants.Get();
        ctx->PSSetConstantBuffers(0, 1, &cb);
        auto* rtv = view.Get();
        constexpr float clear[] = {1,0,1,1};
        ctx->ClearRenderTargetView(rtv, clear);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ctx->Draw(3, 0);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        ctx->CopyResource(read.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(ctx->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read native test output");
        Image image{};
        CopyRows(image, mapped.pData, mapped.RowPitch);
        ctx->Unmap(read.Get(), 0);
        return image;
    }
private:
    ID3D11Device* d;
    Com<ID3D11DeviceContext> ctx;
    Com<ID3D11VertexShader> vertex;
    Com<ID3D11InputLayout> layout;
    Com<ID3D11Buffer> geometry;
    Com<ID3D11Texture2D> target, read;
    Com<ID3D11RenderTargetView> view;
    std::array<Com<ID3D11ShaderResourceView>, StageCount> textures;
    Com<ID3D11SamplerState> sampler;
    Com<ID3D11RasterizerState> raster;
};

} // namespace legacy_comparison

