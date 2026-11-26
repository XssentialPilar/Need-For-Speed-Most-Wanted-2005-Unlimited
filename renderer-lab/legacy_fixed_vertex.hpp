#pragma once

#include "legacy_fixed_pixel.hpp"
#include <d3dx9.h>
#include <cmath>
#include <vector>

namespace legacy_fixed {

using Color = std::array<float,4>;
struct Light {
    Color diffuse{}, specular{}, ambient{}, positionRange{}, directionType{}, attenuation{}, cone{};
};
struct VertexConstants {
    D3DXMATRIX worldView{}, projection{}, normals{};
    std::array<D3DXMATRIX,8> texture{};
    Color viewport{}, depthRange{}, ambient{}, diffuseMaterial{}, ambientMaterial{}, specularMaterial{}, emissiveMaterial{}, power{};
    std::array<Light,8> lights{};
};
struct VertexState {
    bool transformed = false, normal = false, diffuse = false, specular = false;
    std::array<unsigned,8> texcoordWidths{};
    DWORD lighting = 0, colorVertex = 0, normalize = 0, specEnable = 0, localViewer = 0;
    DWORD diffuseSource = 0, specularSource = 0, ambientSource = 0, emissiveSource = 0;
    std::array<Stage,8> stages{};
    unsigned lightCount = 0;
    VertexConstants constants;
};

inline Color Rgba(const D3DCOLORVALUE& v) { return {v.r,v.g,v.b,v.a}; }

// Supply all light slots observed through LightEnable/SetLight, including IDs
// greater than the maximum number of simultaneous active lights.
inline VertexState CaptureVertex(IDirect3DDevice9* d, const PixelState& pixel, const std::vector<DWORD>& lightSlots) {
    VertexState s{};
    s.stages = pixel.stages;
    auto rs = [&](D3DRENDERSTATETYPE key) { DWORD v; Require(d->GetRenderState(key,&v),"Read fixed vertex state"); return v; };
    if (rs(D3DRS_VERTEXBLEND) != D3DVBF_DISABLE || rs(D3DRS_INDEXEDVERTEXBLENDENABLE) ||
        rs(D3DRS_CLIPPLANEENABLE) || rs(D3DRS_FOGENABLE))
        throw std::runtime_error("Unsupported inherited blend, user clipping or fog state");
    s.lighting = rs(D3DRS_LIGHTING);
    s.colorVertex = rs(D3DRS_COLORVERTEX);
    s.normalize = rs(D3DRS_NORMALIZENORMALS);
    s.specEnable = rs(D3DRS_SPECULARENABLE);
    s.localViewer = rs(D3DRS_LOCALVIEWER);
    s.diffuseSource = rs(D3DRS_DIFFUSEMATERIALSOURCE); s.specularSource = rs(D3DRS_SPECULARMATERIALSOURCE);
    s.ambientSource = rs(D3DRS_AMBIENTMATERIALSOURCE); s.emissiveSource = rs(D3DRS_EMISSIVEMATERIALSOURCE);
    auto& c = s.constants;
    c.ambient = Unpack(rs(D3DRS_AMBIENT));
    D3DMATERIAL9 material{};
    Require(d->GetMaterial(&material),"Read fixed material");
    c.diffuseMaterial=Rgba(material.Diffuse); c.specularMaterial=Rgba(material.Specular);
    c.ambientMaterial=Rgba(material.Ambient); c.emissiveMaterial=Rgba(material.Emissive); c.power[0]=material.Power;
    D3DXMATRIX world, view;
    Require(d->GetTransform(D3DTS_WORLD,&world),"Read world matrix");
    Require(d->GetTransform(D3DTS_VIEW,&view),"Read view matrix");
    Require(d->GetTransform(D3DTS_PROJECTION,&c.projection),"Read projection matrix");
    D3DXMatrixMultiply(&c.worldView,&world,&view);
    D3DXMATRIX inverse{};
    if (D3DXMatrixInverse(&inverse,nullptr,&c.worldView)) D3DXMatrixTranspose(&c.normals,&inverse);
    for (unsigned i=0;i<8;++i) Require(d->GetTransform(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0+i), &c.texture[i]),"Read texture matrix");
    D3DVIEWPORT9 v{};
    Require(d->GetViewport(&v),"Read fixed viewport");
    if (!v.Width || !v.Height) throw std::runtime_error("Empty fixed viewport");
    c.viewport={float(v.X),float(v.Y),float(v.Width),float(v.Height)};
    c.depthRange={v.MinZ,v.MaxZ,0,0};
    Com<IDirect3DVertexDeclaration9> decl;
    Require(d->GetVertexDeclaration(decl.Put()), "Read fixed vertex declaration");
    if (!decl.Get()) throw std::runtime_error("No fixed vertex declaration");
    D3DVERTEXELEMENT9 elements[MAX_FVF_DECL_SIZE]{};
    UINT count = MAX_FVF_DECL_SIZE;
    Require(decl->GetDeclaration(elements,&count),"Read fixed vertex elements");
    bool position = false;
    for (unsigned i=0;i<count && elements[i].Stream!=0xff;++i) {
        auto e=elements[i];
        if (e.Usage==D3DDECLUSAGE_POSITION && !e.UsageIndex) position=true;
        if (e.Usage==D3DDECLUSAGE_POSITIONT && !e.UsageIndex) position=s.transformed=true;
        if (e.Usage==D3DDECLUSAGE_NORMAL && !e.UsageIndex) s.normal=true;
        if (e.Usage==D3DDECLUSAGE_COLOR) {
            if (e.UsageIndex==0) s.diffuse=true;
            if (e.UsageIndex==1) s.specular=true;
        }
        if (e.Usage==D3DDECLUSAGE_TEXCOORD && e.UsageIndex<8) {
            switch (e.Type) {
                case D3DDECLTYPE_FLOAT1: case D3DDECLTYPE_FLOAT2: case D3DDECLTYPE_FLOAT3: case D3DDECLTYPE_FLOAT4:
                    s.texcoordWidths[e.UsageIndex]=e.Type+1; break;
                case D3DDECLTYPE_SHORT2: case D3DDECLTYPE_SHORT2N: case D3DDECLTYPE_FLOAT16_2:
                    s.texcoordWidths[e.UsageIndex]=2; break;
                default: throw std::runtime_error("Unsupported fixed-function texture-coordinate encoding");
            }
        }
    }
    if (!position) throw std::runtime_error("No fixed-function position");
    if (s.lighting && !s.transformed) for (auto slot : lightSlots) {
        BOOL active=FALSE;
        if (FAILED(d->GetLightEnable(slot,&active)) || !active) continue;
        if (s.lightCount>=c.lights.size()) throw std::runtime_error("Too many active fixed lights");
        D3DLIGHT9 source{};
        Require(d->GetLight(slot,&source),"Read enabled light");
        if (source.Type!=D3DLIGHT_DIRECTIONAL && source.Type!=D3DLIGHT_POINT && source.Type!=D3DLIGHT_SPOT)
            throw std::runtime_error("Unsupported fixed light");
        auto& light=c.lights[s.lightCount++];
        light.diffuse=Rgba(source.Diffuse); light.specular=Rgba(source.Specular); light.ambient=Rgba(source.Ambient);
        D3DXVECTOR3 p(source.Position.x,source.Position.y,source.Position.z), dir(source.Direction.x,source.Direction.y,source.Direction.z);
        D3DXVec3TransformCoord(&p,&p,&view); D3DXVec3TransformNormal(&dir,&dir,&view); D3DXVec3Normalize(&dir,&dir);
        light.positionRange={p.x,p.y,p.z,source.Range}; light.directionType={dir.x,dir.y,dir.z,float(source.Type)};
        light.attenuation={source.Attenuation0,source.Attenuation1,source.Attenuation2,source.Falloff};
        light.cone={std::cos(source.Theta*0.5f),std::cos(source.Phi*0.5f),0,0};
    }
    return s;
}

inline string MaterialSource(const VertexState& s, DWORD reg, const char* material) {
    if (s.colorVertex && s.diffuse && reg==D3DMCS_COLOR1) return "i.diffuse";
    if (s.colorVertex && s.specular && reg==D3DMCS_COLOR2) return "i.specular";
    return material;
}

inline string VertexShader(const VertexState& s) {
    const string variables = R"(
struct Light { float4 diff,spec,ambient,posRange,dirType,atten,cone; };
cbuffer FixedVertexConstants : register(b0) {
 row_major float4x4 worldView,projection,normalMatrix;
 row_major float4x4 textureMatrices[8];
 float4 viewport,depthRange,ambient,diffuseM,ambientM,specularM,emissiveM,power;
 Light lights[8];
};
float3 safeUnit(float3 v) { return v*rsqrt(max(dot(v,v),1e-30)); }
)";
    string input="float4 position : POSITION0;\n";
    if(s.normal) input+="float4 normal : NORMAL0;\n";
    if(s.diffuse) input+="float4 diffuse : COLOR0;\n";
    if(s.specular) input+="float4 specular : COLOR1;\n";
    string output="float4 position : SV_Position;\nfloat4 diffuse : COLOR0;\nfloat4 specular : COLOR1;\n";
    for(unsigned n=0;n<8;++n) {
        string nstr=to_string(n);
        if(s.texcoordWidths[n]) input+="float4 uv"+nstr+" : TEXCOORD"+nstr+";\n";
        output+="float4 uv"+nstr+" : TEXCOORD"+nstr+";\n";
    }
    string body;
    if (s.transformed) {
        body=R"(float rhw=i.position.w;
float2 ndc=(i.position.xy-viewport.xy+0.5)/viewport.zw*float2(2,-2)+float2(-1,1);
float z=(i.position.z-depthRange.x)/max(depthRange.y-depthRange.x,1e-20);
o.position=float4(ndc,z,1)/rhw;
float3 pos=0,normal=0;
)";
    } else {
        body="float3 pos=mul(float4(i.position.xyz,1),worldView).xyz;\n"
             "o.position=mul(float4(pos,1),projection);\n"
             "o.position.xy+=float2(1/viewport.z,-1/viewport.w)*o.position.w;\n";
        body+=s.normal ? "float3 normal=mul(float4(i.normal.xyz,0),normalMatrix).xyz;\n" : "float3 normal=0;\n";
        if(s.normalize) body+="normal=safeUnit(normal);\n";
    }
    body+=s.localViewer ? "float3 eye=-safeUnit(pos);\n" : "float3 eye=float3(0,0,-1);\n";
    if(!s.transformed && s.lighting) {
        body+="float4 md="+MaterialSource(s,s.diffuseSource,"diffuseM")+", ma="+MaterialSource(s,s.ambientSource,"ambientM")+
              ", ms="+MaterialSource(s,s.specularSource,"specularM")+", me="+MaterialSource(s,s.emissiveSource,"emissiveM")+";\n";
        body+="float3 d=me.rgb+ambient.rgb*ma.rgb, spec=0;\n";
        for(unsigned n=0;n<s.lightCount;++n) {
            body+="{ Light l=lights["+to_string(n)+"]; float3 dir=-l.dirType.xyz; float a=1;\n";
            body+=R"(if(l.dirType.w!=3) {
 float3 delta=l.posRange.xyz-pos; float dist=length(delta); dir=safeUnit(delta);
 a=dist>l.posRange.w?0:rcp(max(l.atten.x+dist*(l.atten.y+dist*l.atten.z),1e-20));
 if(l.dirType.w==2) {
   float angle=dot(-dir,l.dirType.xyz);
   float factor=saturate((angle-l.cone.y)/max(l.cone.x-l.cone.y,1e-20));
   a*=angle>=l.cone.x?1:(angle<=l.cone.y?0:pow(factor,l.atten.w));
 }
}
float nl=max(dot(normal,dir),0);
d+=a*(l.ambient.rgb*ma.rgb+nl*l.diff.rgb*md.rgb);
)";
            if(s.specEnable) body+="if(nl>0) spec+=a*l.spec.rgb*ms.rgb*pow(max(dot(normal,safeUnit(dir+eye)),0),power.x);\n";
            body+="}\n";
        }
        body+="o.diffuse=saturate(float4(d,md.a)); o.specular=saturate(float4(spec,0));\n";
    } else {
        body+=s.diffuse?"o.diffuse=i.diffuse;\n":"o.diffuse=1;\n";
        body+=s.specular?"o.specular=i.specular;\n":"o.specular=0;\n";
    }
    for(unsigned n=0;n<8;++n) {
        const auto& a=s.stages[n];
        const unsigned idx=a.coordinate&0xffff, gen=s.transformed?0:a.coordinate&0xffff0000;
        const string id=to_string(n);
        string uv="float4(0,0,0,1)";
        if(gen==D3DTSS_TCI_CAMERASPACENORMAL) uv="float4(normal,1)";
        else if(gen==D3DTSS_TCI_CAMERASPACEPOSITION) uv="float4(pos,1)";
        else if(gen==D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR) uv="float4(reflect(-eye,normal),1)";
        else if(gen) throw std::runtime_error("Unimplemented fixed-function coordinate generation");
        else if(idx<8 && s.texcoordWidths[idx]) {
            uv="i.uv"+to_string(idx);
            if(a.transform && !s.transformed) {
                if(s.texcoordWidths[idx]==1) uv="float4("+uv+".x,1,0,0)";
                if(s.texcoordWidths[idx]==2) uv="float4("+uv+".xy,1,0)";
                if(s.texcoordWidths[idx]==3) uv="float4("+uv+".xyz,1)";
            }
        }
        if(a.transform && !s.transformed) uv="mul("+uv+",textureMatrices["+id+"])";
        body+="o.uv"+id+"="+uv+";\n";
    }
    return variables+"struct UnlimitedShader_Input {\n"+input+"};\nstruct UnlimitedShader_Output {\n"+output+
           "};\nUnlimitedShader_Output UnlimitedShader(UnlimitedShader_Input i) { UnlimitedShader_Output o;\n"+body+"return o;\n}";
}

} // namespace legacy_fixed

