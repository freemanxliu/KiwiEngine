#include <metal_stdlib>
using namespace metal;

struct FSIn
{
    float4 position [[position]];
    float2 uv;
};

fragment float4 kiwi_fragment(FSIn in [[stage_in]],
                              constant float4& params [[buffer(0)]],
                              texture2d<float> inputTex [[texture(0)]],
                              sampler inputSampler [[sampler(0)]])
{
    float4 color = inputTex.sample(inputSampler, in.uv);
    float2 center = in.uv - float2(0.5, 0.5);
    float dist = length(center);
    float vignette = 1.0 - smoothstep(0.3, 0.9, dist * params.z * 1.5);
    return float4(color.rgb * vignette, color.a);
}
