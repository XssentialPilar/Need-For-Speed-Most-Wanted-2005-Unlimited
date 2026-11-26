#define MovieRoot "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), RootConstants(num32BitConstants=26,b0), DescriptorTable(SRV(t0),visibility=SHADER_VISIBILITY_PIXEL), DescriptorTable(Sampler(s0),visibility=SHADER_VISIBILITY_PIXEL)"
cbuffer Frontend : register(b0) {
    float4 clipX, clipY, clipZ, clipW;
    float4 offset;
    float2 pixelShift;
    float2 uvScale;
    uint alphaFunc;
    uint alphaRef;
};
struct Output { float4 pos:SV_Position; float2 uv:TEXCOORD0; float4 tint:COLOR0; };
Output MovieVertex(float3 xyz:POSITION, float4 bgra:COLOR0, float2 uv:TEXCOORD0) {
    float4 p=float4(xyz,1);
    Output o;
    o.pos=float4(dot(p,clipX),dot(p,clipY),dot(p,clipZ),dot(p,clipW));
    o.pos.xy+=pixelShift * o.pos.w;
    o.uv=(uv+offset.xy)*uvScale;
    o.tint=bgra.bgra;
    return o;
}
Texture2D frame:register(t0);
SamplerState filter:register(s0);
float3 MovieTexel(int2 p) { return frame.Load(int3(p,0)).rgb; }
#include "movie_reconstruction.hlsli"
float4 MoviePixel(Output i):SV_Target0 {
    uint width, height;
    frame.GetDimensions(width,height);
    float4 sampleColor=frame.Sample(filter,i.uv);
    if (width==1024 && height==512) sampleColor=ReconstructMovie(i.uv,sampleColor);
    float4 outColor=sampleColor*i.tint;
    float a=outColor.a, reference=alphaRef/255.0;
    if (alphaFunc==1 || (alphaFunc==2 && a>=reference) || (alphaFunc==3 && a!=reference) ||
        (alphaFunc==4 && a>reference) || (alphaFunc==5 && a<=reference) || (alphaFunc==6 && a==reference) ||
        (alphaFunc==7 && a<reference)) discard;
    return float4(saturate(outColor.rgb*2),outColor.a);
}

// Two shareable BGRA8 surfaces keep 16-bit precision before the game blends.
struct LayerOutput {float4 high:SV_Target0; float4 low:SV_Target1;};
LayerOutput MovieLayer(Output i) {
    float4 color=MoviePixel(i);
    color.a=color.a*0.5+0.5;
    float4 encoded=floor(saturate(color)*65535+0.5);
    float4 high=floor(encoded/256);
    LayerOutput result;
    result.high=high/255;
    result.low=(encoded-high*256)/255;
    return result;
}
