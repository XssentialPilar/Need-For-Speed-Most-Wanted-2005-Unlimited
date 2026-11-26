#pragma once

#include "movie_d3d.hpp"
#include <d3d9.h>
#include <array>
#include <string>

namespace legacy_fixed {
using std::string;
using std::to_string;
using movie_pass::Com;
using movie_pass::Require;

struct Operation { DWORD code; std::array<DWORD, 3> args; };
struct Stage {
    Operation rgb{}, alpha{};
    DWORD result = D3DTA_CURRENT;
    DWORD coordinate = 0, transform = 0;
    D3DRESOURCETYPE texture{};
};
struct Constants {
    std::array<float, 4> factor{};
    std::array<std::array<float, 4>, 8> stage{};
    std::array<float, 4> alphaRef{};
};
struct PixelState {
    std::array<Stage, 8> stages{};
    Constants constants;
    DWORD specular = 0, alphaTest = 0, alphaFunction = D3DCMP_ALWAYS;
};

inline std::array<float, 4> Unpack(DWORD argb) {
    return {((argb >> 16) & 255) / 255.f, ((argb >> 8) & 255) / 255.f,
             (argb & 255) / 255.f, ((argb >> 24) & 255) / 255.f};
}

inline PixelState CapturePixel(IDirect3DDevice9* device) {
    PixelState state{};
    auto rs = [&](D3DRENDERSTATETYPE key) {
        DWORD value = 0;
        Require(device->GetRenderState(key, &value), "Read fixed-function render state");
        return value;
    };
    state.constants.factor = Unpack(rs(D3DRS_TEXTUREFACTOR));
    state.specular = rs(D3DRS_SPECULARENABLE);
    state.alphaTest = rs(D3DRS_ALPHATESTENABLE);
    state.alphaFunction = rs(D3DRS_ALPHAFUNC);
    state.constants.alphaRef[0] = static_cast<float>(rs(D3DRS_ALPHAREF) & 255);
    for (unsigned i = 0; i < state.stages.size(); ++i) {
        auto tss = [&](D3DTEXTURESTAGESTATETYPE key) {
            DWORD value = 0;
            Require(device->GetTextureStageState(i, key, &value), "Read effective fixed-function stage");
            return value;
        };
        auto& s = state.stages[i];
        s.rgb = {tss(D3DTSS_COLOROP), {tss(D3DTSS_COLORARG0), tss(D3DTSS_COLORARG1), tss(D3DTSS_COLORARG2)}};
        s.alpha = {tss(D3DTSS_ALPHAOP), {tss(D3DTSS_ALPHAARG0), tss(D3DTSS_ALPHAARG1), tss(D3DTSS_ALPHAARG2)}};
        s.coordinate = tss(D3DTSS_TEXCOORDINDEX);
        s.transform = tss(D3DTSS_TEXTURETRANSFORMFLAGS);
        s.result = tss(D3DTSS_RESULTARG);
        state.constants.stage[i] = Unpack(tss(D3DTSS_CONSTANT));
        Com<IDirect3DBaseTexture9> tex;
        Require(device->GetTexture(i, tex.Put()), "Read effective fixed-function texture");
        if (tex.Get()) s.texture = tex->GetType();
    }
    return state;
}

inline bool Enabled(const Stage& stage) {
    return stage.rgb.code != D3DTOP_DISABLE &&
           (stage.texture || ((stage.rgb.args[1] & D3DTA_SELECTMASK) != D3DTA_TEXTURE));
}

inline string Arg(DWORD source) {
    string text;
    switch (source & D3DTA_SELECTMASK) {
        case D3DTA_CURRENT: text = "current"; break;
        case D3DTA_TEMP: text = "temp"; break;
        case D3DTA_TEXTURE: text = "texel"; break;
        case D3DTA_DIFFUSE: text = "i.diffuse"; break;
        case D3DTA_SPECULAR: text = "i.specular"; break;
        case D3DTA_TFACTOR: text = "factor"; break;
        case D3DTA_CONSTANT: text = "stageValue"; break;
        default: throw std::runtime_error("Unimplemented fixed-function texture argument");
    }
    if (source & D3DTA_ALPHAREPLICATE) text += ".wwww";
    return (source & D3DTA_COMPLEMENT) ? "(1-" + text + ")" : text;
}

inline string Combine(const Operation& op) {
    const string a = Arg(op.args[1]), b = Arg(op.args[2]), c = Arg(op.args[0]);
    switch (op.code) {
        case D3DTOP_SELECTARG1: return a;
        case D3DTOP_SELECTARG2: return b;
        case D3DTOP_MODULATE: return a + "*" + b;
        case D3DTOP_MODULATE2X: return "2*" + a + "*" + b;
        case D3DTOP_MODULATE4X: return "4*" + a + "*" + b;
        case D3DTOP_ADD: return a + "+" + b;
        case D3DTOP_ADDSIGNED: return a + "+" + b + "-0.5";
        case D3DTOP_ADDSIGNED2X: return "2*(" + a + "+" + b + "-0.5)";
        case D3DTOP_SUBTRACT: return a + "-" + b;
        case D3DTOP_ADDSMOOTH: return a + "+(1-" + a + ")*" + b;
        case D3DTOP_BLENDDIFFUSEALPHA: return "lerp(" + b + "," + a + ",i.diffuse.a)";
        case D3DTOP_BLENDTEXTUREALPHA: return "lerp(" + b + "," + a + ",texel.a)";
        case D3DTOP_BLENDFACTORALPHA: return "lerp(" + b + "," + a + ",factor.a)";
        case D3DTOP_BLENDCURRENTALPHA: return "lerp(" + b + "," + a + ",current.a)";
        case D3DTOP_BLENDTEXTUREALPHAPM: return a + "+(1-texel.a)*" + b;
        case D3DTOP_MODULATEALPHA_ADDCOLOR: return a + "+" + a + ".a*" + b;
        case D3DTOP_MODULATECOLOR_ADDALPHA: return a + "*" + b + "+" + a + ".a";
        case D3DTOP_MODULATEINVALPHA_ADDCOLOR: return a + "+(1-" + a + ".a)*" + b;
        case D3DTOP_MODULATEINVCOLOR_ADDALPHA: return "(1-" + a + ")*" + b + "+" + a + ".a";
        case D3DTOP_DOTPRODUCT3: return "dot(2*" + a + ".rgb-1,2*" + b + ".rgb-1)";
        case D3DTOP_MULTIPLYADD: return a + "*" + b + "+" + c;
        case D3DTOP_LERP: return "lerp(" + b + "," + a + "," + c + ")";
        default: throw std::runtime_error("Unimplemented fixed-function combiner " + to_string(op.code));
    }
}

// The fixed-function vertex stage supplies a transformed/generated coordinate
// per active stage. Projected divides happen per fragment after interpolation.
inline string PixelShader(const PixelState& state) {
    string resources = "cbuffer FixedPixelConstants : register(b0) { float4 factor; float4 stageConstants[8]; float4 alphaRef; };\n";
    string fields = "float4 position : SV_Position;\nfloat4 diffuse : COLOR0;\nfloat4 specular : COLOR1;\n";
    string body = "float4 current=i.diffuse, temp=0;\n";
    for (unsigned n = 0; n < state.stages.size(); ++n) {
        const string id = to_string(n);
        fields += "float4 uv" + id + " : TEXCOORD" + id + ";\n";
    }
    for (unsigned n = 0; n < state.stages.size() && Enabled(state.stages[n]); ++n) {
        const auto& s = state.stages[n];
        const string id = to_string(n);
        body += "{\nfloat4 stageValue=stageConstants[" + id + "];\n";
        if (s.texture) {
            const char* type = s.texture == D3DRTYPE_TEXTURE ? "Texture2D" :
                               s.texture == D3DRTYPE_CUBETEXTURE ? "TextureCube" :
                               s.texture == D3DRTYPE_VOLUMETEXTURE ? "Texture3D" : nullptr;
            if (!type) throw std::runtime_error("Unsupported fixed-function texture dimension");
            resources += string(type) + "<float4> texture" + id + " : register(t" + id + ");\n";
            resources += "SamplerState sampler" + id + " : register(s" + id + ");\n";
            string coord = "i.uv" + id + (s.texture == D3DRTYPE_TEXTURE ? ".xy" : ".xyz");
            if (s.transform & D3DTTFF_PROJECTED) {
                const auto count = s.transform & 255;
                if (count < 2 || count > 4) throw std::runtime_error("Invalid projected texture transform");
                coord += "/i.uv" + id + "." + "xyzw"[count - 1];
            }
            body += "float4 texel=texture" + id + ".Sample(sampler" + id + "," + coord + ");\n";
        } else {
            body += "float4 texel=i.diffuse;\n";
        }
        body += "float4 rgb=saturate(" + Combine(s.rgb) + ");\n";
        // Dot-product writes a scalar into all four channels.
        if (s.rgb.code == D3DTOP_DOTPRODUCT3) body += "float4 alpha=rgb;\n";
        else body += "float4 alpha=saturate(" + Combine(s.alpha) + ");\n";
        if (s.result != D3DTA_TEMP && s.result != D3DTA_CURRENT) throw std::runtime_error("Invalid fixed-function result register");
        body += string(s.result == D3DTA_TEMP ? "temp" : "current") + "=float4(rgb.rgb,alpha.a);\n}\n";
    }
    if (state.specular) body += "current.rgb=saturate(current.rgb+i.specular.rgb);\n";
    if (state.alphaTest) {
        static constexpr const char* comparisons[]{"", "false", "a<r", "a==r", "a<=r", "a>r", "a!=r", "a>=r", "true"};
        if (state.alphaFunction < D3DCMP_NEVER || state.alphaFunction > D3DCMP_ALWAYS)
            throw std::runtime_error("Invalid alpha test function");
        body += "float a=round(saturate(current.a)*255),r=alphaRef.x; if (!(" +
                string(comparisons[state.alphaFunction]) + ")) discard;\n";
    }
    return resources + "struct Interpolants {\n" + fields + "};\nfloat4 UnlimitedShader(Interpolants i) : SV_Target {\n" +
           body + "return current;\n}";
}

} // namespace legacy_fixed

