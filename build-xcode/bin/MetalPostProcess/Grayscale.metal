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
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 gray = float3(luminance);
    float3 result = mix(color.rgb, gray, params.z);
    return float4(result, color.a);
}
