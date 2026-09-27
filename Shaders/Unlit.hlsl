// ============================================================
// Unlit Shader
// Pure color output — no lighting calculations
// ============================================================
//
// @Properties {
//   _Color ("Base Color", Color) = (1.0, 1.0, 1.0, 1.0)
//   _BaseColorTex ("Albedo", Texture2D) = "white"
// }

#include "Common.hlsli"

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
    float4 PositionCS : SV_POSITION;
    float3 PositionWS : POSITION;
    float3 NormalWS   : NORMAL;
    float4 Color      : COLOR;
    float2 TexCoord   : TEXCOORD;
};

// ---- Vertex Shader ----
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    GPUObject obj = GetGPUObject(input.InstanceID);
    float4 worldPos = mul(float4(input.Position, 1.0), obj.World);
    float4 viewPos = mul(worldPos, g_View);
    float4 projPos = mul(viewPos, g_Projection);

    output.PositionCS = projPos;
    output.PositionWS = worldPos.xyz;
    output.NormalWS = mul(input.Normal, (float3x3)obj.World);
    output.Color = input.Color * obj.Color;
    output.TexCoord = input.TexCoord;

    return output;
}

// ---- Pixel Shader ----
float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(input.Color.rgb, input.Color.a);
}
