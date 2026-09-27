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
};

struct KiwiObject
{
    float4x4 world;
    float4 color;
    float4 material0;
    float4 material1;
    float4 material2;
};

vertex VSOut VSMain(VSIn in [[stage_in]],
                    constant KiwiView& viewUB [[buffer(0)]],
                    constant KiwiObject& objUB [[buffer(1)]])
{
    float3 n = in.normal;
    if (dot(n, n) < 1e-8)
        n = float3(0.0, 0.0, 1.0);
    float4x4 world = objUB.world;
    float3x3 world3 = float3x3(world[0].xyz, world[1].xyz, world[2].xyz);
    float4 worldPos = world * float4(in.position, 1.0);
    VSOut out;
    out.position = viewUB.projection * viewUB.view * worldPos;
    out.normalWS = normalize(world3 * n);
    out.color = in.color * objUB.color;
    out.uv = in.uv;
    out.roughness = objUB.material0.y;
    out.metallic = objUB.material0.z;
    out.hasBase = objUB.material0.w;
    out.shadingModel = objUB.material1.y;
    out.emissive = float3(objUB.material1.z, objUB.material1.w, objUB.material2.x);
    return out;
}

//!FRAGMENT
struct FSIn
{
    float4 position [[position]];
    float3 normalWS;
    float4 color;
    float2 uv;
    float roughness;
    float metallic;
    float hasBase;
    float shadingModel;
    float3 emissive;
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

struct GBufferOut
{
    float4 a [[color(0)]];
    float4 b [[color(1)]];
    float4 c [[color(2)]];
    float4 d [[color(3)]];
};

/*%SURFACE%*/

float3 EncodeNormal(float3 n)
{
    float3 absN = abs(n);
    float sum = absN.x + absN.y + absN.z;
    float2 oct = n.xy / max(sum, 1e-6);
    if (n.z < 0.0)
    {
        float2 signNotZero = float2(oct.x >= 0.0 ? 1.0 : -1.0, oct.y >= 0.0 ? 1.0 : -1.0);
        oct = (1.0 - abs(oct.yx)) * signNotZero;
    }
    return float3(oct * 0.5 + 0.5, n.z * 0.5 + 0.5);
}

float EncodeShadingModelId(uint shadingModelId)
{
    return float(shadingModelId << 4) / 255.0;
}

fragment GBufferOut PSMain(FSIn in [[stage_in]],
                           texture2d<float> baseColorTex [[texture(4)]])
{
    constexpr sampler linearSampler(filter::linear, address::repeat);
    MaterialAttributes attr = EvaluateMaterial(in, baseColorTex, linearSampler);
    uint shadingModelId = (uint)(in.shadingModel + 0.5);
    float3 encoded = EncodeNormal(normalize(attr.normal));
    GBufferOut out;
    out.a = float4(encoded, 0.0);
    out.b = float4(attr.metallic, attr.specular, attr.roughness, EncodeShadingModelId(shadingModelId));
    out.c = float4(attr.baseColor, attr.ao);
    out.d = float4(attr.emissive, 0.0);
    return out;
}
