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

struct KiwiView
{
    float4x4 view;
    float4x4 projection;
};

struct KiwiObject
{
    float4x4 world;
    float4 color;
    float4 material0;
};

vertex VSOut kiwi_vertex(VSIn in [[stage_in]],
                         constant KiwiView& viewUB [[buffer(0)]],
                         constant KiwiObject& objUB [[buffer(1)]])
{
    float4x4 world = objUB.world;
    float4x4 view  = viewUB.view;
    float4x4 proj  = viewUB.projection;

    VSOut out;
    out.position = proj * view * (world * float4(in.position, 1.0));
    out.color = in.color * objUB.color;
    out.uv = in.uv;
    return out;
}

//!FRAGMENT
struct KiwiObject
{
    float4x4 world;
    float4 color;
    float4 material0;
};

struct FSIn
{
    float4 position [[position]];
    float4 color;
    float2 uv;
};

fragment float4 kiwi_fragment(FSIn in [[stage_in]],
                              constant KiwiObject& objUB [[buffer(1)]],
                              texture2d<float> baseColorTex [[texture(4)]])
{
    constexpr sampler linearSampler(filter::linear, address::repeat);
    float4 color = in.color;
    if (objUB.material0.w > 0.5)
        color.rgb *= baseColorTex.sample(linearSampler, in.uv).rgb;
    return color;
}
