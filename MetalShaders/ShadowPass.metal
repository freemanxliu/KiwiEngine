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

struct KiwiObject
{
    float4x4 world;
    float4 pad[12]; // PrimitiveUniformBuffer is 256 bytes
};

static VSOut TransformShadow(VSIn in, constant KiwiView& viewUB, float4x4 world)
{
    VSOut out;
    float4 worldPos = world * float4(in.position, 1.0);
    out.position = viewUB.projection * viewUB.view * worldPos;
    return out;
}

#ifdef USE_GPU_SCENE_INSTANCING
vertex VSOut VSMain(VSIn in [[stage_in]],
                    constant KiwiView& viewUB [[buffer(0)]],
                    const device KiwiObject* scene [[buffer(8)]],
                    constant uint4& batch [[buffer(4)]],
                    uint instanceId [[instance_id]])
{
    return TransformShadow(in, viewUB, scene[batch.x + instanceId].world);
}
#else
vertex VSOut VSMain(VSIn in [[stage_in]],
                    constant KiwiView& viewUB [[buffer(0)]],
                    constant KiwiObject& objUB [[buffer(1)]])
{
    return TransformShadow(in, viewUB, objUB.world);
}
#endif

//!FRAGMENT
fragment void PSMain()
{
}
