cbuffer ShadowObject : register(b0) {
    row_major float4x4 clipFromObject;
};

float4 ShadowVertex(float3 position : POSITION) : SV_Position {
    return mul(float4(position, 1.0), clipFromObject);
}

float4 ShadowDepth(float4 position : SV_Position) : SV_Target0 {
    // Rasterization has already performed the perspective divide.
    return float4(position.zzz, 1.0);
}

float4 ShadowMarker() : SV_Target0 {
    return float4(0.0, 1.0, 0.0, 1.0);
}
