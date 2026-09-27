// G-Buffer geometry pass. Three color attachments, matching GBufferPass.hlsl.
// Single draws read PrimitiveUniformBuffer at buffer(1). Instancing stays on DX.

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
    float3 tangentWS;
    float3 bitangentWS;
    float4 color;
    float2 uv;
    float roughness;
    float metallic;
    float hasBase;
    float hasNormal;
    float shadingModel;
};

struct KiwiView
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;
    float4x4 invViewProjection;
    float4 cameraAndMode;
};

struct KiwiObject
{
    float4x4 world;
    float4 color;
    float4 material0; // selected, roughness, metallic, hasBase
    float4 material1; // hasNormal, shadingModel
};

vertex VSOut VSMain(VSIn in [[stage_in]],
                    constant KiwiView& viewUB [[buffer(0)]],
                    constant KiwiObject& objUB [[buffer(1)]])
{
    float4x4 world = objUB.world;
    float3x3 world3 = float3x3(world[0].xyz, world[1].xyz, world[2].xyz);
    float4 worldPos = world * float4(in.position, 1.0);
    float3 n = in.normal;
    if (dot(n, n) < 1e-8)
        n = float3(0.0, 0.0, 1.0);

    VSOut out;
    out.position = viewUB.projection * viewUB.view * worldPos;
    out.positionWS = worldPos.xyz;
    out.normalWS = normalize(world3 * n);
    out.color = in.color * objUB.color;
    out.uv = in.uv;

    float3 N = out.normalWS;
    float3 T = world3 * in.tangent;
    if (dot(T, T) < 0.001)
    {
        if (abs(N.y) < 0.99)
            T = cross(float3(0.0, 1.0, 0.0), N);
        else
            T = cross(float3(1.0, 0.0, 0.0), N);
    }
    T = normalize(T - N * dot(N, T));
    out.tangentWS = T;
    out.bitangentWS = cross(N, T);

    out.roughness = objUB.material0.y;
    out.metallic = objUB.material0.z;
    out.hasBase = objUB.material0.w;
    out.hasNormal = objUB.material1.x;
    out.shadingModel = objUB.material1.y;
    return out;
}

//!FRAGMENT
struct FSIn
{
    float4 position [[position]];
    float3 positionWS;
    float3 normalWS;
    float3 tangentWS;
    float3 bitangentWS;
    float4 color;
    float2 uv;
    float roughness;
    float metallic;
    float hasBase;
    float hasNormal;
    float shadingModel;
};

struct GBufferOut
{
    float4 a [[color(0)]];
    float4 b [[color(1)]];
    float4 c [[color(2)]];
};

float3 EncodeNormal(float3 n)
{
    float3 absN = abs(n);
    float sum = absN.x + absN.y + absN.z;
    float2 oct = n.xy / sum;
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
                           texture2d<float> baseColorTex [[texture(4)]],
                           texture2d<float> normalTex [[texture(5)]])
{
    constexpr sampler linearSampler(filter::linear, address::repeat);

    uint shadingModelId = (uint)(in.shadingModel + 0.5);
    float3 baseColor = in.color.rgb;
    if (in.hasBase > 0.5)
        baseColor *= baseColorTex.sample(linearSampler, in.uv).rgb;

    GBufferOut out;
    if (shadingModelId == 0)
    {
        out.a = float4(EncodeNormal(float3(0.0, 0.0, 1.0)), 0.0);
        out.b = float4(0.0, 0.0, 0.0, EncodeShadingModelId(0));
        out.c = float4(baseColor, 1.0);
        return out;
    }

    float3 normal = in.normalWS;
    if (dot(normal, normal) < 1e-8)
        normal = float3(0.0, 0.0, 1.0);
    else
        normal = normalize(normal);
    if (in.hasNormal > 0.5)
    {
        float3 tangentNormal = normalTex.sample(linearSampler, in.uv).rgb * 2.0 - 1.0;
        float3x3 tbn = float3x3(normalize(in.tangentWS), normalize(in.bitangentWS), normal);
        normal = normalize(tbn * tangentNormal);
    }

    out.a = float4(EncodeNormal(normal), 0.0);
    out.b = float4(in.metallic, 0.5, in.roughness, EncodeShadingModelId(shadingModelId));
    out.c = float4(baseColor, 1.0);
    return out;
}
