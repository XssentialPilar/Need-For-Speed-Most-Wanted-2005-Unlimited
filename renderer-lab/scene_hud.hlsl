// PC HUD vertices use pretransformed XYZRHW and packed D3DCOLOR (BGRA).
#define HudRoot "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), RootConstants(num32BitConstants=4, b0, visibility=SHADER_VISIBILITY_VERTEX)"
cbuffer Viewport : register(b0) {
    float2 origin;
    float2 extent;
};
struct Output {
    float4 position : SV_Position;
    float4 color : COLOR0;
};
Output HudVertex(float2 position : POSITION, float4 bgra : COLOR0) {
    Output result;
    // The D3D9 vertex already includes -0.5 to place its edges on pixel centers.
    const float2 uv = (position + 0.5 - origin) / extent;
    result.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    result.color = bgra.bgra;
    return result;
}
float4 HudPixel(Output input) : SV_Target0 {
    return input.color;
}
