#pragma once

namespace Kiwi
{

    // MSL shaders use buffer(0)/buffer(1) as tightly packed float4 arrays so the
    // layout matches the CPU constant buffers (row-major matrices consumed as
    // Metal columns). Entry points are kiwi_vertex / kiwi_fragment.

    inline const char* g_VertexShaderMSL = R"msl(
#include <metal_stdlib>
using namespace metal;

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
    float3 positionWS;
    float3 normalWS;
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
    float4 objectColor = objUB.color;

    float4 worldPos = world * float4(in.position, 1.0);
    VSOut out;
    out.position = proj * view * worldPos;
    out.positionWS = worldPos.xyz;
    out.normalWS = float3x3(world[0].xyz, world[1].xyz, world[2].xyz) * in.normal;
    out.color = in.color * objectColor;
    out.uv = in.uv;
    return out;
}
)msl";

    inline const char* g_PixelShaderMSL = R"msl(
#include <metal_stdlib>
using namespace metal;

struct KiwiObject
{
    float4x4 world;
    float4 color;
    float4 material0;
};

struct FSIn
{
    float4 position [[position]];
    float3 positionWS;
    float3 normalWS;
    float4 color;
    float2 uv;
};

fragment float4 kiwi_fragment(FSIn in [[stage_in]],
                              constant KiwiObject& objUB [[buffer(1)]])
{
    float4 objectColor = objUB.color;
    float selected = objUB.material0.x;
    if (selected > 1.5)
        return float4(in.color.rgb * objectColor.rgb, in.color.a);

    float3 normal = normalize(in.normalWS);
    float3 lightDir = normalize(float3(0.5, 0.7, 0.3));
    float ndotl = max(dot(normal, lightDir), 0.0);
    float3 finalColor = in.color.rgb * (0.15 + ndotl * 0.85);
    return float4(finalColor, in.color.a);
}
)msl";

    inline const char* g_PostProcessVS_MSL = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VSOut
{
    float4 position [[position]];
    float2 uv;
};

vertex VSOut kiwi_vertex(uint vid [[vertex_id]])
{
    float2 uv = float2(float((vid << 1) & 2), float(vid & 2));
    VSOut out;
    out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    out.uv = float2(uv.x, 1.0 - uv.y);
    return out;
}
)msl";

    inline const char* g_PostProcessPassthroughPS_MSL = R"msl(
#include <metal_stdlib>
using namespace metal;

struct FSIn
{
    float4 position [[position]];
    float2 uv;
};

fragment float4 kiwi_fragment(FSIn in [[stage_in]],
                              texture2d<float> inputTex [[texture(0)]],
                              sampler inputSampler [[sampler(0)]])
{
    return inputTex.sample(inputSampler, in.uv);
}
)msl";

}
