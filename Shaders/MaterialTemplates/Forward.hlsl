#include "Common.hlsli"

Texture2D    g_BaseColorTex : register(t4);
SamplerState g_LinearWrap   : register(s1);

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD;
};

struct VSOutput
{
    float4 PositionCS : SV_POSITION;
    float3 PositionWS : TEXCOORD0;
    float3 NormalWS   : TEXCOORD1;
    float4 Color      : COLOR;
    float2 TexCoord   : TEXCOORD4;
    nointerpolation float Roughness : TEXCOORD5;
    nointerpolation float Metallic : TEXCOORD6;
    nointerpolation float HasBaseColorTex : TEXCOORD7;
    nointerpolation float ShadingModelID : TEXCOORD10;
    nointerpolation float3 Emissive : TEXCOORD11;
};

struct MaterialAttributes
{
    float3 BaseColor;
    float Metallic;
    float Specular;
    float Roughness;
    float3 Emissive;
    float3 Normal;
    float Opacity;
    float AO;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float3 n = input.Normal;
    if (dot(n, n) < 1e-8)
        n = float3(0.0, 0.0, 1.0);
    float4 worldPos = mul(float4(input.Position, 1.0), g_World);
    output.PositionCS = mul(mul(worldPos, g_View), g_Projection);
    output.PositionWS = worldPos.xyz;
    output.NormalWS = normalize(mul(n, (float3x3)g_World));
    output.Color = input.Color * g_ObjectColor;
    output.TexCoord = input.TexCoord;
    output.Roughness = g_Roughness;
    output.Metallic = g_Metallic;
    output.HasBaseColorTex = g_HasBaseColorTex;
    output.ShadingModelID = g_ShadingModelID;
    output.Emissive = g_ObjectPadding;
    return output;
}

/*%SURFACE%*/

float4 PSMain(VSOutput input) : SV_TARGET
{
    if (g_Selected > 1.5)
        return float4(input.Color.rgb, input.Color.a);

    MaterialAttributes attr = EvaluateMaterial(input);
    if (input.ShadingModelID < 0.5)
        return float4(attr.Emissive, 1.0);

    float3 N = normalize(attr.Normal);
    float3 V = normalize(g_CameraPos - input.PositionWS);
    float3 L = normalize(float3(0.5, 0.7, 0.3));
    float ndotl = saturate(dot(N, L));
    float3 lit = attr.BaseColor * (0.15 + ndotl) * (1.0 - attr.Metallic * 0.5);
    return float4(lit + attr.Emissive, 1.0);
}
