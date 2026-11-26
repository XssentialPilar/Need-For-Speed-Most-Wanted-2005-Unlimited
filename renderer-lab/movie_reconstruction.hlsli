// The stock VP6 decoder produces an anamorphic 1024 x 512 image.
// All coordinates here use coded texels. The FE quad handles display aspect.
static const float2 codedSize = float2(1024, 512);

float4 MovieCubicWeights(float t) {
    float t2 = t*t;
    float t3 = t2*t;
    return float4(-0.5*t + t2 - 0.5*t3, 1 - 2.5*t2 + 1.5*t3,
                   0.5*t + 2*t2 - 1.5*t3, -0.5*t2 + 0.5*t3);
}

float4 ReconstructMovie(float2 uv, float4 original) {
    float2 footprint = max(abs(ddx(uv*codedSize)), abs(ddy(uv*codedSize)));
    if (max(footprint.x, footprint.y) > 1.001 || min(footprint.x, footprint.y) >= 0.999)
        return original;
    float2 texel = uv*codedSize - 0.5;
    int2 anchor = int2(floor(texel));
    float2 f = frac(texel);
    float4 wx = MovieCubicWeights(f.x);
    float4 wy = MovieCubicWeights(f.y);
    float3 sum = 0;
    float3 localMin = 1;
    float3 localMax = 0;
    [unroll] for (int y=0; y<4; ++y) {
        [unroll] for (int x=0; x<4; ++x) {
            int2 sourceTexel = clamp(anchor + int2(x-1,y-1), int2(0,0), int2(1023,511));
            float3 value = MovieTexel(sourceTexel);
            sum += value * (wx[x]*wy[y]);
            if (x >= 1 && x <= 2 && y >= 1 && y <= 2) {
                localMin = min(localMin, value);
                localMax = max(localMax, value);
            }
        }
    }
    // Limit ringing at letterbox boundaries and preserve the game's alpha.
    return float4(clamp(sum,localMin,localMax), original.a);
}
