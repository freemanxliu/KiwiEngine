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

struct InstanceSceneData { float4x4 world; uint4 primitiveId; };
struct PrimitiveSceneData { float4 objectColor; float4 material0; float4 material1; float4 material2; uint4 ids; };

vertex VSOut kiwi_vertex(VSIn in [[stage_in]],
                         constant KiwiView& viewUB [[buffer(0)]],
                         constant uint4& drawInfo [[buffer(4)]],
                         const device InstanceSceneData* instances [[buffer(8)]],
                         const device PrimitiveSceneData* primitives [[buffer(9)]],
                         const device uint4* drawIds [[buffer(10)]],
                         uint svInstanceId [[instance_id]])
{
    uint instanceId = drawIds[drawInfo.x + svInstanceId].x;
    float4x4 world = instances[instanceId].world;
    float4 color = primitives[instances[instanceId].primitiveId.x].objectColor;
    VSOut out;
    out.position = viewUB.projection * viewUB.view * (world * float4(in.position, 1.0));
    out.color = in.color * color;
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
