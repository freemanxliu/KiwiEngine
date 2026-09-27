// G-buffer channel debug view. Mode comes from ObjectUB material1.y (shading model slot).

//!VERTEX
struct VSOut
{
    float4 position [[position]];
    float2 uv;
};

vertex VSOut VSMain(uint vertexID [[vertex_id]])
{
    float2 uv = float2(float((vertexID << 1) & 2), float(vertexID & 2));
    VSOut out;
    out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    out.uv = float2(uv.x, 1.0 - uv.y);
    return out;
}

//!FRAGMENT
struct FSIn
{
    float4 position [[position]];
    float2 uv;
};

struct KiwiObject
{
    float4x4 world;
    float4 color;
    float4 material0;
    float4 material1; // x hasNormal, y visualize mode
};

float3 DecodeNormal(float4 gbufferA)
{
    float2 oct = gbufferA.rg * 2.0 - 1.0;
    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0)
    {
        float2 signNotZero = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * signNotZero;
    }
    return normalize(n);
}

fragment float4 PSMain(FSIn in [[stage_in]],
                       constant KiwiObject& objUB [[buffer(1)]],
                       texture2d<float> gbufferA [[texture(0)]],
                       texture2d<float> gbufferB [[texture(1)]],
                       texture2d<float> gbufferC [[texture(2)]],
                       sampler linearSampler [[sampler(0)]])
{
    float4 gA = gbufferA.sample(linearSampler, in.uv);
    float4 gB = gbufferB.sample(linearSampler, in.uv);
    float4 gC = gbufferC.sample(linearSampler, in.uv);
    if (gC.r == 0.0 && gC.g == 0.0 && gC.b == 0.0 && gB.b == 0.0)
        return float4(0.05, 0.05, 0.08, 1.0);

    int mode = (int)(objUB.material1.y + 0.5);
    if (mode == 0)
        return float4(gC.rgb, 1.0);
    if (mode == 1)
        return float4(float3(gB.b), 1.0);
    if (mode == 2)
        return float4(float3(gB.r), 1.0);
    if (mode == 3)
        return float4(DecodeNormal(gA) * 0.5 + 0.5, 1.0);
    if (mode == 4)
        return float4(float3(gB.g), 1.0);
    return float4(float3(gC.a), 1.0);
}
