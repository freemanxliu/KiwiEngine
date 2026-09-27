#include "Common.hlsli"

Texture2D    g_BaseColorTex : register(t4);
Texture2D    g_NormalTex    : register(t5);
SamplerState g_LinearWrap   : register(s1);

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD;
    uint   InstanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 PositionCS  : SV_POSITION;
    float3 NormalWS    : TEXCOORD1;
    float4 Color       : COLOR;
    float2 TexCoord    : TEXCOORD4;
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

struct GBufferOutput
{
    float4 GBufferA : SV_TARGET0;
    float4 GBufferB : SV_TARGET1;
    float4 GBufferC : SV_TARGET2;
    float4 GBufferD : SV_TARGET3;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float3 n = input.Normal;
    if (dot(n, n) < 1e-8)
        n = float3(0.0, 0.0, 1.0);

    GPUObject obj = GetGPUObject(input.InstanceID);
    float4 worldPos = mul(float4(input.Position, 1.0), obj.World);
    output.PositionCS = mul(mul(worldPos, g_View), g_Projection);
    output.NormalWS = normalize(mul(n, (float3x3)obj.World));
    output.Color = input.Color * obj.Color;
    output.TexCoord = input.TexCoord;
    output.Roughness = obj.Roughness;
    output.Metallic = obj.Metallic;
    output.HasBaseColorTex = obj.HasBaseColorTex;
    output.ShadingModelID = obj.ShadingModelID;
    output.Emissive = obj.Emissive;
    return output;
}

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

GBufferOutput PSMain(VSOutput input)
{
    GBufferOutput output;
    MaterialAttributes attr = EvaluateMaterial(input);
    uint shadingModelId = (uint)(input.ShadingModelID + 0.5);
    float3 encodedNormal = EncodeNormal(normalize(attr.Normal));
    output.GBufferA = float4(encodedNormal, 0.0);
    output.GBufferB = float4(attr.Metallic, attr.Specular, attr.Roughness, EncodeShadingModelId(shadingModelId));
    output.GBufferC = float4(attr.BaseColor, attr.AO);
    output.GBufferD = float4(attr.Emissive, 0.0);
    return output;
}
