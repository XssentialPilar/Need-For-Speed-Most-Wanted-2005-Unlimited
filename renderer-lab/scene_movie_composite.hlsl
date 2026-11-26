#define CompositeRoot "RootConstants(num32BitConstants=3,b0), DescriptorTable(SRV(t0, numDescriptors=2), visibility=SHADER_VISIBILITY_PIXEL)"
cbuffer LayerDimensions : register(b0) {
    uint2 frameSize;
    uint tileColumns;
};

float4 CompositeVertex(uint id : SV_VertexID) : SV_Position {
    float2 corner = float2((id << 1) & 2, id & 2);
    return float4(corner * float2(2,-2) + float2(-1,1), 0, 1);
}

#ifdef UNORM16_LAYER
#ifdef MULTISAMPLE
Texture2DMS<float4> filmLayer : register(t0);
float4 CompositeFilm(float4 p : SV_Position, uint sampleId : SV_SampleIndex) : SV_Target {
    float4 pixel=filmLayer.Load(int2(p.xy),sampleId);
#else
Texture2D<float4> filmLayer : register(t0);
float4 CompositeFilm(float4 p : SV_Position) : SV_Target {
    float4 pixel=filmLayer.Load(int3(p.xy,0));
#endif
    clip(pixel.a-0.25);
    pixel.a=saturate(pixel.a*2-1);
    return pixel;
}
#elif defined(EXPORT_SAMPLES)
Texture2DMS<float4> high : register(t0);
Texture2DMS<float4> low : register(t1);
struct Samples { float4 high : SV_Target0; float4 low : SV_Target1; };
Samples ExportSamples(float4 p : SV_Position) {
    uint w,h,count;
    high.GetDimensions(w,h,count);
    uint2 pos=uint2(p.xy);
    uint2 tile=pos/uint2(w,h);
    uint sampleId=tile.x+tile.y*tileColumns;
    Samples outSample;
    outSample.high=high.Load(pos%uint2(w,h),sampleId);
    outSample.low=low.Load(pos%uint2(w,h),sampleId);
    return outSample;
}
#else
Texture2D<float4> high : register(t0);
Texture2D<float4> low : register(t1);
#ifdef MULTISAMPLE
float4 CompositePixel(float4 p : SV_Position, uint sampleId : SV_SampleIndex) : SV_Target {
    int3 index=int3(uint2(p.xy)+frameSize*uint2(sampleId%tileColumns,sampleId/tileColumns),0);
#else
float4 CompositePixel(float4 p : SV_Position) : SV_Target {
    int3 index=int3(int2(p.xy),0);
#endif
    float4 pixel=(high.Load(index)*256+low.Load(index))/257;
    clip(pixel.a-0.25);
    pixel.a=saturate(pixel.a*2-1);
    return pixel;
}
#endif
