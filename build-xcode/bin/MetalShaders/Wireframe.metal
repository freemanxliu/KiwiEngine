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

vertex VSOut kiwi_vertex(VSIn in [[stage_in]],
                         constant float4* viewData [[buffer(0)]],
                         constant float4* objData [[buffer(1)]])
{
    float4x4 world = float4x4(objData[0], objData[1], objData[2], objData[3]);
    float4x4 view  = float4x4(viewData[0], viewData[1], viewData[2], viewData[3]);
    float4x4 proj  = float4x4(viewData[4], viewData[5], viewData[6], viewData[7]);

    VSOut out;
    out.position = proj * view * (world * float4(in.position, 1.0));
    out.normalWS = float3x3(world[0].xyz, world[1].xyz, world[2].xyz) * in.normal;
    out.color = in.color * objData[4];
    return out;
}

//!FRAGMENT
struct FSIn
{
    float4 position [[position]];
    float3 normalWS;
    float4 color;
};

fragment float4 kiwi_fragment(FSIn in [[stage_in]],
                              constant float4* objData [[buffer(1)]])
{
    if (objData[5].x > 1.5)
        return float4(in.color.rgb * objData[4].rgb, in.color.a);

    float3 normal = normalize(in.normalWS);
    return float4(normal * 0.5 + 0.5, 1.0);
}
