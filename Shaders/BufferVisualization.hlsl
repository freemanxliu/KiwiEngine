// ============================================================
// Buffer Visualization Shader (UE5-inspired GBuffer layout)
// Displays a single channel from the G-Buffer for debug viewing
// Uses fullscreen triangle (SV_VertexID)
//
// VisualizeMode (g_VisualizeMode in ObjectUB):
//   0 = BaseColor  (GBufferB.rgb)
//   1 = Roughness  (GBufferB.a — grayscale)
//   2 = Metallic   (GBufferA.b — grayscale)
// ============================================================

#include "Common.hlsli"

// G-Buffer textures
Texture2D g_GBufferA : register(t0);
Texture2D g_GBufferB : register(t1);
Texture2D g_GBufferC : register(t2);

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

// ---- Octahedron Normal Decoding ----
float3 OctahedronDecode(float2 oct)
{
    oct = oct * 2.0 - 1.0;
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

    // Skip empty pixels
    if (gbufferA.r == 0.0 && gbufferA.g == 0.0 && gbufferB.r == 0.0 && gbufferB.g == 0.0 && gbufferB.b == 0.0)
    {
        return float4(0.12, 0.12, 0.18, 1.0);
    }

    int mode = (int)(g_VisualizeMode + 0.5);

    if (mode == 0)
    {
        return float4(gbufferB.rgb, 1.0);
    }
    else if (mode == 1)
    {
        float roughness = gbufferB.a;
        return float4(roughness, roughness, roughness, 1.0);
    }
    else // mode == 2
    {
        float metallic = gbufferA.b;
        return float4(metallic, metallic, metallic, 1.0);
    }
}
