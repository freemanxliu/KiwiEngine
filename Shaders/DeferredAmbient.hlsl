// ============================================================
// Deferred Ambient Pass — Hemisphere ambient + optional IBL
// Drawn FIRST (opaque), per-light passes ADD on top.
// ============================================================

#include "Common.hlsli"

#define PI 3.14159265359

Texture2D g_GBufferA    : register(t0);
Texture2D g_GBufferB    : register(t1);
Texture2D g_GBufferC    : register(t2);
Texture2D g_DepthBuffer : register(t7);

SamplerState g_Sampler : register(s0);

struct VSOutput { float4 Position : SV_POSITION; float2 TexCoord : TEXCOORD0; };

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput o;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    o.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.TexCoord = float2(uv.x, 1.0 - uv.y);
    return o;
}

float3 DecodeNormal(float4 gA)
{
    float2 oct = gA.rg * 2.0 - 1.0;
    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0) { float2 s = float2(n.x >= 0 ? 1 : -1, n.y >= 0 ? 1 : -1); n.xy = (1.0 - abs(n.yx)) * s; }
    return normalize(n);
}

float Pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

float3 EnvBRDFApprox(float3 F0, float R, float NoV)
{
    float4 c0 = float4(-1, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1, 0.0425, 1.04, -0.04);
    float4 r = R * c0 + c1;
    float a = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a + r.zw;
    AB.y *= saturate(50.0 * F0.g);
    return F0 * AB.x + AB.y;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float depth = g_DepthBuffer.Sample(g_Sampler, input.TexCoord).r;
    if (depth >= 1.0) return float4(0.05, 0.05, 0.08, 1.0);

    float4 gA = g_GBufferA.Sample(g_Sampler, input.TexCoord);
    float4 gB = g_GBufferB.Sample(g_Sampler, input.TexCoord);
    float4 gC = g_GBufferC.Sample(g_Sampler, input.TexCoord);

    // Decode ShadingModel from GBufferB.a
    uint shadingModelId = (uint(gB.a * 255.0 + 0.5)) >> 4;

    float3 baseColor = gC.rgb;

    // Unlit: output BaseColor directly as emissive (no ambient/lighting)
    if (shadingModelId == 0)
        return float4(baseColor, 1.0);

    float3 N = DecodeNormal(gA);
    float metallic = gB.r, specular = gB.g, roughness = max(gB.b, 0.04);
    float ao = gC.a;

    float3 V = normalize(g_CameraPos);
    float NoV = saturate(abs(dot(N, V)) + 1e-5);

    float3 diffuseColor = baseColor * (1.0 - metallic);
    float3 specularColor = lerp(float3(0.08, 0.08, 0.08) * specular, baseColor, metallic);

    // Hemisphere ambient
    float3 sky = float3(0.15, 0.15, 0.25);
    float3 ground = float3(0.10, 0.08, 0.05);
    float3 ambient = lerp(ground, sky, N.y * 0.5 + 0.5);

    float3 ambDiffuse = diffuseColor * ambient * 0.3 * ao;
    float3 ambSpecular = EnvBRDFApprox(specularColor, roughness, NoV) * ambient * 0.15;

    return float4(ambDiffuse + ambSpecular, 1.0);
}
