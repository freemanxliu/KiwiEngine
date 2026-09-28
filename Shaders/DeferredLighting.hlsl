// ============================================================
// Deferred Lighting Pass — instanced, additive blend
//
// Two draws per frame, one instance per light:
//   VSMain       — directional lights, fullscreen triangle
//   VSPointLight — point lights, icosahedron light volume drawn with
//                  front faces culled so only pixels the volume covers are shaded
// Instances index g_Lights in ViewUB. Lights are sorted so that directional
// lights occupy [0, g_NumDirectionalLights) and point lights follow.
//
// G-Buffer layout (UE5, all R8G8B8A8_UNORM):
//   t0 GBufferA: Normal(RG octahedron + B=z) + PerObjectData(A)
//   t1 GBufferB: Metallic(R) + Specular(G) + Roughness(B) + ShadingModelID(A)
//   t2 GBufferC: BaseColor(RGB) + AO(A)
//   t3 ShadowAtlas: CSM depth atlas
//   t7 DepthBuffer: Hardware depth (R32_FLOAT)
//
// Constant Buffers:
//   b0 = ViewUB   (camera, screen, lights)
//   b2 = ShadowUB (CSM data — only used by directional lights)
// ============================================================

#include "Common.hlsli"

#define MAX_CSM_CASCADES 4
#define PI 3.14159265359

// Shadow Uniform Buffer (b2)
cbuffer ShadowUB : register(b2)
{
    row_major float4x4 g_LightViewProj[MAX_CSM_CASCADES];
    float4 g_CascadeSplits;
    float  g_ShadowBias;
    float  g_NormalBias;
    float  g_ShadowStrength;
    int    g_NumCascades;
    float  g_ShadowMapSize;
    float3 g_ShadowPadding;
};

// G-Buffer textures
Texture2D g_GBufferA    : register(t0);
Texture2D g_GBufferB    : register(t1);
Texture2D g_GBufferC    : register(t2);
Texture2D g_ShadowAtlas : register(t3);
Texture2D g_DepthBuffer : register(t7);

SamplerState g_Sampler                 : register(s0);
SamplerComparisonState g_ShadowSampler : register(s2);

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 ClipPos  : TEXCOORD0; // unclamped clip position, divided per pixel for the G-Buffer UV
    nointerpolation uint LightIndex : TEXCOORD1;
};

// ---- Directional lights: fullscreen triangle ----
VSOutput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.ClipPos = output.Position;
    output.LightIndex = min(instanceID, (uint)(MAX_LIGHTS - 1));
    return output;
}

// Unit-circumradius icosahedron. Winding matches the engine's clockwise front faces.
static const float3 kIcosahedron[12] = {
    float3(-0.5257311,  0.8506508,  0.0), float3( 0.5257311,  0.8506508,  0.0),
    float3(-0.5257311, -0.8506508,  0.0), float3( 0.5257311, -0.8506508,  0.0),
    float3( 0.0, -0.5257311,  0.8506508), float3( 0.0,  0.5257311,  0.8506508),
    float3( 0.0, -0.5257311, -0.8506508), float3( 0.0,  0.5257311, -0.8506508),
    float3( 0.8506508,  0.0, -0.5257311), float3( 0.8506508,  0.0,  0.5257311),
    float3(-0.8506508,  0.0, -0.5257311), float3(-0.8506508,  0.0,  0.5257311),
};
// Scale by radius / inradius so the faces enclose the whole light sphere.
static const float kIcosahedronInradius = 0.7946545;

// ---- Point lights: light volume ----
VSOutput VSPointLight(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;
    uint lightIndex = min((uint)g_NumDirectionalLights + instanceID, (uint)(MAX_LIGHTS - 1));
    LightData light = g_Lights[lightIndex];
    float scale = max(light.Radius, 0.001) / kIcosahedronInradius;
    float3 worldPos = light.DirectionOrPos + kIcosahedron[vertexID % 12] * scale;
    float4 clip = mul(float4(worldPos, 1.0), g_ViewProjection);
    output.ClipPos = clip;
    // Depth testing is off, so clamping to the far plane only keeps volumes that cross it from being clipped.
    clip.z = min(clip.z, clip.w);
    output.Position = clip;
    output.LightIndex = lightIndex;
    return output;
}

// ---- Normal Decode ----
float3 DecodeNormal(float4 gbufferA)
{
    float2 oct = gbufferA.rg * 2.0 - 1.0;
    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0)
    {
        float2 s = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * s;
    }
    return normalize(n);
}

// ---- World Position from Depth ----
float3 ReconstructWorldPos(float2 uv, float depth)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clip = float4(ndc, depth, 1.0);
    float4 world = mul(clip, g_InvViewProj);
    return world.xyz / world.w;
}

// ---- CSM Shadow ----
float SampleShadowAtlas(float2 atlasUV, float depth, float bias)
{
    float texelSize = 1.0 / g_ShadowMapSize;
    float d = depth - bias;
    float shadow = 0.0;
    shadow += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, atlasUV, d);
    shadow += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, atlasUV + float2( texelSize, 0), d);
    shadow += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, atlasUV + float2(-texelSize, 0), d);
    shadow += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, atlasUV + float2(0,  texelSize), d);
    shadow += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, atlasUV + float2(0, -texelSize), d);
    return shadow / 5.0;
}

float ComputeShadow(float3 worldPos, float viewZ)
{
    if (g_NumCascades <= 0) return 1.0;

    int ci = 0;
    float splits[4] = { g_CascadeSplits.x, g_CascadeSplits.y, g_CascadeSplits.z, g_CascadeSplits.w };
    for (int i = 0; i < g_NumCascades - 1; i++)
        if (viewZ > splits[i]) ci = i + 1;

    float4 sp = mul(float4(worldPos, 1.0), g_LightViewProj[ci]);
    float3 sc;
    sc.xy = sp.xy / sp.w * 0.5 + 0.5;
    sc.y = 1.0 - sc.y;
    sc.z = sp.z / sp.w;

    if (sc.x < 0 || sc.x > 1 || sc.y < 0 || sc.y > 1 || sc.z < 0 || sc.z > 1)
        return 1.0;

    static const float2 offsets[4] = {
        float2(0,0), float2(0.5,0), float2(0,0.5), float2(0.5,0.5)
    };
    float2 atlasUV = sc.xy * 0.5 + offsets[ci];
    return lerp(1.0, SampleShadowAtlas(atlasUV, sc.z, g_ShadowBias), g_ShadowStrength);
}

// ============================================================
// BRDF (UE5 reference)
// ============================================================
float Pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
    float a = sqrt(a2);
    float v = NoL * (NoV * (1.0 - a) + a);
    float l = NoV * (NoL * (1.0 - a) + a);
    return 0.5 * rcp(v + l);
}

float3 F_Schlick(float3 F0, float VoH)
{
    float Fc = Pow5(1.0 - VoH);
    return saturate(50.0 * F0.g) * Fc + (1.0 - Fc) * F0;
}

float3 Diffuse_Burley(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
    float FD90 = 0.5 + 2.0 * VoH * VoH * Roughness;
    float FdV = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoV);
    float FdL = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoL);
    return DiffuseColor * ((1.0 / PI) * FdV * FdL);
}

float3 SpecularGGX(float Roughness, float3 F0, float NoV, float NoL, float NoH, float VoH)
{
    float a2 = max(Roughness * Roughness, 0.001);
    return D_GGX(a2, NoH) * Vis_SmithJointApprox(a2, NoV, NoL) * F_Schlick(F0, VoH);
}

// ============================================================
// Per-Light Pixel Shader
// Output is ADDITIVE — accumulated into scene color RT
// ============================================================
float4 PSMain(VSOutput input) : SV_TARGET
{
    float2 ndc = input.ClipPos.xy / input.ClipPos.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    LightData light = g_Lights[input.LightIndex];

    float depth = g_DepthBuffer.Sample(g_Sampler, uv).r;
    if (depth >= 1.0) return float4(0, 0, 0, 0); // Sky — no contribution

    float3 worldPos = ReconstructWorldPos(uv, depth);

    // ---- Light direction and distance attenuation ----
    float3 L;
    float attenuation = 1.0;

    if (light.Type == 0) // Directional
    {
        L = normalize(light.DirectionOrPos);
    }
    else // Point
    {
        float3 toLight = light.DirectionOrPos - worldPos;
        float distSq = dot(toLight, toLight);
        float radius = max(light.Radius, 0.001);
        // The volume is a coarse icosahedron, so reject pixels outside the sphere before reading the G-Buffer.
        if (distSq >= radius * radius) return float4(0, 0, 0, 0);

        float dist = sqrt(distSq);
        L = toLight / max(dist, 0.0001);

        // UE5 inverse-square + windowing: 1/d^2 * (1 - (d/r)^4)^2
        float invSq = 1.0 / max(distSq, 0.0001);
        float nd = dist / radius;
        float nd4 = nd * nd * nd * nd;
        float window = saturate(1.0 - nd4);
        attenuation = invSq * window * window;
    }

    // Decode G-Buffer
    float4 gA = g_GBufferA.Sample(g_Sampler, uv);
    float4 gB = g_GBufferB.Sample(g_Sampler, uv);
    float4 gC = g_GBufferC.Sample(g_Sampler, uv);

    // Decode ShadingModel from GBufferB.a
    uint shadingModelId = (uint(gB.a * 255.0 + 0.5)) >> 4;

    // Unlit surfaces receive no direct lighting
    if (shadingModelId == 0) return float4(0, 0, 0, 0);

    float3 N         = DecodeNormal(gA);
    float  metallic  = gB.r;
    float  specular  = gB.g;
    float  roughness = max(gB.b, 0.04);
    float3 baseColor = gC.rgb;
    float  ao        = gC.a;

    float NoL = saturate(dot(N, L));
    if (NoL <= 0.0) return float4(0, 0, 0, 0); // Back-facing — no contribution

    if (light.Type == 0)
    {
        // CSM shadow
        float4 viewPos = mul(float4(worldPos, 1.0), g_View);
        attenuation *= ComputeShadow(worldPos, viewPos.z);
    }

    float3 V   = normalize(g_CameraPos - worldPos);
    float  NoV = saturate(abs(dot(N, V)) + 1e-5);

    // Material properties (UE5 metallic workflow)
    float3 diffuseColor  = baseColor * (1.0 - metallic);
    float3 specularColor = lerp(float3(0.08, 0.08, 0.08) * specular, baseColor, metallic);

    float3 H   = normalize(V + L);
    float  NoH = saturate(dot(N, H));
    float  VoH = saturate(dot(V, H));

    // BRDF
    float3 diffBRDF = Diffuse_Burley(diffuseColor, roughness, NoV, NoL, VoH);
    float3 specBRDF = SpecularGGX(roughness, specularColor, NoV, NoL, NoH, VoH);

    float3 radiance = light.ColorIntensity * NoL * attenuation;
    float3 result = (diffBRDF + specBRDF) * radiance * ao;

    return float4(result, 0.0);
}
