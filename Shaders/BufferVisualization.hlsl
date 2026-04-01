// ============================================================
// Buffer Visualization Shader (UE5 GBuffer Layout)
// Displays individual G-Buffer channels for debug viewing
// Uses fullscreen triangle (SV_VertexID)
//
// VisualizeMode (g_VisualizeMode in ObjectUB):
//   0 = BaseColor   (GBufferC.rgb)
//   1 = Roughness   (GBufferB.b — grayscale)
//   2 = Metallic    (GBufferB.r — grayscale)
//   3 = Normal      (decoded from GBufferA — remapped to [0,1])
//   4 = Specular    (GBufferB.g — grayscale)
//   5 = AO          (GBufferC.a — grayscale)
// ============================================================

#include "Common.hlsli"

// G-Buffer textures (UE5 layout)
Texture2D g_GBufferA : register(t0);  // Normal + PerObjectData
Texture2D g_GBufferB : register(t1);  // Metallic + Specular + Roughness + ShadingModelID
Texture2D g_GBufferC : register(t2);  // BaseColor + AO

SamplerState g_GBufferSampler : register(s0);

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// ---- Fullscreen Triangle Vertex Shader ----
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.TexCoord = float2(uv.x, 1.0 - uv.y);
    return output;
}

// ---- Decode Normal from UE5 GBufferA ----
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

// ---- Pixel Shader ----
float4 PSMain(VSOutput input) : SV_TARGET
{
    float4 gbufferA = g_GBufferA.Sample(g_GBufferSampler, input.TexCoord);
    float4 gbufferB = g_GBufferB.Sample(g_GBufferSampler, input.TexCoord);
    float4 gbufferC = g_GBufferC.Sample(g_GBufferSampler, input.TexCoord);

    // Skip empty pixels (background)
    if (gbufferC.r == 0.0 && gbufferC.g == 0.0 && gbufferC.b == 0.0 && gbufferB.b == 0.0)
    {
        return float4(0.05, 0.05, 0.08, 1.0);
    }

    int mode = (int)(g_VisualizeMode + 0.5);

    if (mode == 0) // BaseColor
    {
        return float4(gbufferC.rgb, 1.0);
    }
    else if (mode == 1) // Roughness
    {
        float r = gbufferB.b;
        return float4(r, r, r, 1.0);
    }
    else if (mode == 2) // Metallic
    {
        float m = gbufferB.r;
        return float4(m, m, m, 1.0);
    }
    else if (mode == 3) // WorldNormal (remapped to [0,1])
    {
        float3 normal = DecodeNormal(gbufferA);
        return float4(normal * 0.5 + 0.5, 1.0);
    }
    else if (mode == 4) // Specular
    {
        float s = gbufferB.g;
        return float4(s, s, s, 1.0);
    }
    else // mode == 5: AO
    {
        float a = gbufferC.a;
        return float4(a, a, a, 1.0);
    }
}
