#include <metal_stdlib>
using namespace metal;

//!VERTEX
struct VSIn
{
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float3 tangent  [[attribute(2)]];
    float4 color    [[attribute(3)]];
    float2 uv       [[attribute(4)]];
};

struct VSOut
{
    float4 position [[position]];
    float4 color;
    float2 uv;
};

vertex VSOut kiwi_vertex(VSIn in [[stage_in]],
                         constant float4* viewData [[buffer(0)]],
                         constant float4* objData [[buffer(1)]])
{
    float4x4 world = float4x4(objData[0], objData[1], objData[2], objData[3]);
    float4x4 view  = float4x4(viewData[0], viewData[1], viewData[2], viewData[3]);
    float4x4 proj  = float4x4(viewData[4], viewData[5], viewData[6], viewData[7]);

    VSOut out;
    out.position = proj * view * (world * float4(in.position, 1.0));
    out.color = in.color * objData[4];
    out.uv = in.uv;
    return out;
}

//!FRAGMENT
struct FSIn
{
    float4 position [[position]];
    float4 color;
    float2 uv;
};

fragment float4 kiwi_fragment(FSIn in [[stage_in]],
                              constant float4* objData [[buffer(1)]],
                              texture2d<float> baseColorTex [[texture(4)]])
{
    constexpr sampler linearSampler(filter::linear, address::repeat);
    float4 color = in.color;
    if (objData[5].w > 0.5)
        color.rgb *= baseColorTex.sample(linearSampler, in.uv).rgb;
    return color;
}
