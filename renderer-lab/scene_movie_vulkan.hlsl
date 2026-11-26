#include "scene_movie.hlsl"

float4 VulkanFilm(Output input) : SV_Target {
    float4 color = MoviePixel(input);
    color.a = 0.5 + 0.5 * saturate(color.a);
    return color;
}
