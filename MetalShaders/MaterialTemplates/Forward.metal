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
    float3 positionWS;
    float3 normalWS;
    float4 color;
    float2 uv;
    float roughness;
    float metallic;
    float hasBase;
    float shadingModel;
    float3 emissive;
};

struct KiwiView
{
    float4x4 view;
    float4x4 projection;
    float4 cameraAndMode;
};

struct PrimitiveSceneData
{
    float4 objectColor;
    float4 material0;
    float4 material1;
    float4 material2;
    uint4 ids;
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
                    const device PrimitiveSceneData* primitives [[buffer(9)]],
                    const device uint4* drawIds [[buffer(10)]],
                    uint svInstanceId [[instance_id]])
{
    uint instanceId = drawIds[drawInfo.x + svInstanceId].x;
    InstanceSceneData inst = instances[instanceId];
    PrimitiveSceneData prim = primitives[inst.primitiveId.x];
    float3 n = in.normal;
    if (dot(n, n) < 1e-8)
        n = float3(0.0, 0.0, 1.0);
    float4x4 world = inst.world;
    float3x3 world3 = float3x3(world[0].xyz, world[1].xyz, world[2].xyz);
    float4 worldPos = world * float4(in.position, 1.0);
    VSOut out;
    out.position = viewUB.projection * viewUB.view * worldPos;
    out.positionWS = worldPos.xyz;
    out.normalWS = normalize(world3 * n);
    out.color = in.color * prim.objectColor;
    out.uv = in.uv;
    out.roughness = prim.material0.y;
    out.metallic = prim.material0.z;
    out.hasBase = prim.material0.w;
    out.shadingModel = prim.material1.y;
    out.emissive = float3(prim.material1.z, prim.material1.w, prim.material2.x);
    return out;
}

//!FRAGMENT
struct FSIn
{
    float4 position [[position]];
    float3 positionWS;
    float3 normalWS;
    float4 color;
    float2 uv;
    float roughness;
    float metallic;
    float hasBase;
    float shadingModel;
    float3 emissive;
};

struct KiwiView
{
    float4x4 view;
    float4x4 projection;
    float4 cameraAndMode;
};

struct MaterialAttributes
{
    float3 baseColor;
    float metallic;
    float specular;
    float roughness;
    float3 emissive;
    float3 normal;
    float opacity;
    float ao;
};

/*%SURFACE%*/

fragment float4 PSMain(FSIn in [[stage_in]],
                       constant KiwiView& viewUB [[buffer(0)]],
                       texture2d<float> baseColorTex [[texture(4)]])
{
    constexpr sampler linearSampler(filter::linear, address::repeat);
    if (in.color.a > 1.5)
        return float4(in.color.rgb, 1.0);

    MaterialAttributes attr = EvaluateMaterial(in, baseColorTex, linearSampler);
    if (in.shadingModel < 0.5)
        return float4(attr.emissive, 1.0);

    float3 N = normalize(attr.normal);
    float3 L = normalize(float3(0.5, 0.7, 0.3));
    float ndotl = saturate(dot(N, L));
    float3 lit = attr.baseColor * (0.15 + ndotl);
    return float4(lit + attr.emissive, 1.0);
}
