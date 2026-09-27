#include <metal_stdlib>
using namespace metal;

struct FSIn
{
    float4 position [[position]];
    float2 uv;
};

float3 ACESFilm(float3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

fragment float4 kiwi_fragment(FSIn in [[stage_in]],
                              constant float4& params [[buffer(0)]],
                              texture2d<float> sceneColor [[texture(0)]],
                              sampler sceneSampler [[sampler(0)]])
{
    float3 hdr = sceneColor.sample(sceneSampler, in.uv).rgb;
    float exposure = max(params.x, 0.01);
    hdr *= exposure;
    float3 ldr = ACESFilm(hdr);
    ldr = pow(max(ldr, float3(0.0)), float3(1.0 / 2.2));
    return float4(ldr, 1.0);
}
