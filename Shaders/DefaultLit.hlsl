// ============================================================
// DefaultLit — Standard Phong Material
// Lambert Diffuse + Blinn-Phong Specular
// Supports multiple lights (Directional + Point)
// This is the default material for new mesh objects.
// ============================================================
//
// @Properties {
//   _Color ("Base Color", Color) = (0.8, 0.8, 0.8, 1.0)
//   _Roughness ("Roughness", Range(0, 1)) = 0.5
//   _Metallic ("Metallic", Range(0, 1)) = 0.0
//   _BaseColorTex ("Albedo", Texture2D) = "white"
//   _NormalTex ("Normal Map", Texture2D) = "normal"
//   _MetallicRoughnessTex ("Metallic/Roughness", Texture2D) = "white"
// }

#include "Common.hlsli"

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1

Texture2D g_AlbedoTexture : register(t0);
SamplerState g_TextureSampler : register(s1);

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
    float3 PositionWS : POSITION;
    float3 NormalWS   : NORMAL;
    float4 Color      : COLOR;
    float2 TexCoord   : TEXCOORD;
};

// ---- Vertex Shader ----
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0), g_World);
    float4 viewPos = mul(worldPos, g_View);
    float4 projPos = mul(viewPos, g_Projection);

    output.PositionCS = projPos;
    output.PositionWS = worldPos.xyz;
    output.NormalWS = mul(input.Normal, (float3x3)g_World);
    output.Color = input.Color * g_ObjectColor;
    output.TexCoord = input.TexCoord;

    return output;
}

// ---- Pixel Shader ----
float4 PSMain(VSOutput input) : SV_TARGET
{
    // Gizmo / unlit mode
    if (g_Selected > 1.5)
    {
        return float4(input.Color.rgb * g_ObjectColor.rgb, input.Color.a);
    }

    float3 normal = normalize(input.NormalWS);
    float3 viewDir = normalize(g_CameraPos - input.PositionWS);

    float3 albedo = input.Color.rgb;

    float3 ambient = float3(0.15, 0.15, 0.15);
    float3 totalDiffuse = float3(0, 0, 0);
    float3 totalSpecular = float3(0, 0, 0);

    int numLights = min(g_NumLights, MAX_LIGHTS);
    for (int i = 0; i < numLights; i++)
    {
        float3 lightColor = g_Lights[i].ColorIntensity;
        float3 lightDir;
        float attenuation = 1.0;

        if (g_Lights[i].Type == LIGHT_TYPE_DIRECTIONAL)
        {
            lightDir = normalize(g_Lights[i].DirectionOrPos);
        }
        else // Point light
        {
            float3 toLight = g_Lights[i].DirectionOrPos - input.PositionWS;
            float dist = length(toLight);
            lightDir = toLight / max(dist, 0.0001);

            float radius = max(g_Lights[i].Radius, 0.001);
            float normalizedDist = dist / radius;
            attenuation = saturate(1.0 - normalizedDist * normalizedDist);
            attenuation *= attenuation;
        }

        float NdotL = max(dot(normal, lightDir), 0.0);
        totalDiffuse += lightColor * NdotL * attenuation;

        float3 halfVec = normalize(lightDir + viewDir);
        float shininess = lerp(256.0, 8.0, g_Roughness);
        float spec = pow(max(dot(normal, halfVec), 0.0), shininess);
        float specIntensity = lerp(0.04, 0.8, g_Metallic);
        totalSpecular += lightColor * spec * specIntensity * attenuation;
    }

    if (numLights == 0)
    {
        float3 defaultLightDir = normalize(float3(0.5, 0.7, 0.3));
        float NdotL = max(dot(normal, defaultLightDir), 0.0);
        totalDiffuse = float3(1, 1, 1) * NdotL * 0.85;

        float3 halfVec = normalize(defaultLightDir + viewDir);
        float spec = pow(max(dot(normal, halfVec), 0.0), 32.0);
        totalSpecular = float3(0.3, 0.3, 0.3) * spec * 0.3;
    }

    float3 finalColor = albedo * (ambient + totalDiffuse) + totalSpecular;

    return float4(finalColor, input.Color.a);
}
