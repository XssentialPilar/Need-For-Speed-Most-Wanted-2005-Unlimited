#define FrameRoot "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL), StaticSampler(s0, filter=FILTER_MIN_MAG_LINEAR_MIP_POINT, addressU=TEXTURE_ADDRESS_CLAMP, addressV=TEXTURE_ADDRESS_CLAMP, addressW=TEXTURE_ADDRESS_CLAMP, visibility=SHADER_VISIBILITY_PIXEL)"

struct FramePoint {
    float4 clip : SV_Position;
    float2 uv : TEXCOORD0;
};

FramePoint FrameVertex(uint index : SV_VertexID) {
    FramePoint output;
    output.uv = float2((index << 1) & 2, index & 2);
    output.clip = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

Texture2D sceneColor : register(t0);
SamplerState linearClamp : register(s0);

float4 FramePixel(FramePoint input) : SV_Target {
    return float4(sceneColor.SampleLevel(linearClamp, input.uv, 0).rgb, 1.0);
}
