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
    float3 normalWS;
    float4 color;
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
    out.normalWS = float3x3(world[0].xyz, world[1].xyz, world[2].xyz) * in.normal;
    out.color = in.color * objUB.color;
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
    float3 normalWS;
    float4 color;
};

fragment float4 kiwi_fragment(FSIn in [[stage_in]],
                              constant KiwiObject& objUB [[buffer(1)]])
{
    if (objUB.material0.x > 1.5)
        return float4(in.color.rgb * objUB.color.rgb, in.color.a);

    float3 normal = normalize(in.normalWS);
    return float4(normal * 0.5 + 0.5, 1.0);
}
