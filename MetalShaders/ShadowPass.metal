// Depth-only cascade shadow pass. The shadow atlas is a depth texture.

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
};

vertex VSOut VSMain(VSIn in [[stage_in]],
                    constant KiwiView& viewUB [[buffer(0)]],
                    constant KiwiObject& objUB [[buffer(1)]])
{
    VSOut out;
    float4 worldPos = objUB.world * float4(in.position, 1.0);
    out.position = viewUB.projection * viewUB.view * worldPos;
    return out;
}

//!FRAGMENT
fragment void PSMain()
{
}
