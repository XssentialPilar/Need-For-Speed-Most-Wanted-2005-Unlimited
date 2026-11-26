sampler2D film : register(s0);
float3 MovieTexel(int2 p) {
    return tex2Dlod(film, float4((p+0.5)/float2(1024,512),0,0)).rgb;
}
#include "movie_reconstruction.hlsli"

float4 MoviePixel9(float2 uv : TEXCOORD0, float4 tint : COLOR0) : COLOR0 {
    float4 color = ReconstructMovie(uv,tex2D(film,uv)) * tint;
    return float4(saturate(color.rgb*2),color.a);
}
