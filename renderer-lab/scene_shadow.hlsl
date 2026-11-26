#define ShadowRoot "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT),RootConstants(num32BitConstants=18,b0)"
cbuffer Shadow : register(b0) {
    float4 clipX, clipY, clipZ, clipW;
    float2 pixelShift;
};

struct Output {
    float4 position : SV_Position;
    float2 depth : TEXCOORD1;
};
Output ShadowVertex(float3 position : POSITION) {
    float4 p = float4(position, 1);
    Output o;
    o.position = float4(dot(p, clipX), dot(p, clipY), dot(p, clipZ), dot(p, clipW));
    o.depth = o.position.zw;
    o.position.xy += o.position.w * pixelShift;
    return o;
}
float4 ShadowPixel(Output o) : SV_Target {
    return float4((o.depth.x / o.depth.y).xxx, 1);
}
float4 ShadowMarker() : SV_Target { return float4(0, 1, 0, 1); }
