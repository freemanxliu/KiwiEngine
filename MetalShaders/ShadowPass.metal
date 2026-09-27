// Depth-only cascade shadow pass. The shadow atlas is a depth texture.
// Single draws read the world matrix at buffer(1).
// USE_GPU_SCENE_INSTANCING reads the same 256-byte records from buffer(8).

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
};

struct KiwiView
{
    float4x4 view;
    float4x4 projection;
};

struct InstanceSceneData
{
    float4x4 world;
    uint4 primitiveId;
};

vertex VSOut VSMain(VSIn in [[stage_in]],
                    constant KiwiView& viewUB [[buffer(0)]],
                    constant uint4& drawInfo [[buffer(4)]],
                    const device InstanceSceneData* instances [[buffer(8)]],
                    const device uint4* drawIds [[buffer(10)]],
                    uint svInstanceId [[instance_id]])
{
    uint instanceId = drawIds[drawInfo.x + svInstanceId].x;
    float4x4 world = instances[instanceId].world;
    VSOut out;
    out.position = viewUB.projection * viewUB.view * (world * float4(in.position, 1.0));
    return out;
}

//!FRAGMENT
fragment void PSMain()
{
}
