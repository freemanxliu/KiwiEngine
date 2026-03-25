// ============================================================
// Deferred Lighting Pass Shader (UE5 DefaultLitBxDF reference)
// Reads G-Buffer + Depth and computes physically-based lighting
// Includes Cascaded Shadow Mapping (CSM) with single atlas
// Uses fullscreen triangle (SV_VertexID)
//
// BRDF (matching UE5 BRDF.ush / ShadingModels.ush):
//   NDF:     D_GGX (Trowbridge-Reitz)
//   Vis:     Vis_SmithJointApprox (joint Smith, denominator baked in)
//   Fresnel: F_Schlick (with 2% shadow threshold)
//   Diffuse: Diffuse_Burley (Disney diffuse, roughness-dependent)
//   Env:     EnvBRDFApprox (Lazarov 2013, no LUT needed)
//
// G-Buffer layout (all R8G8B8A8_UNORM):
//   t0 GBufferA: Normal Octahedron(RG) + Metallic(B) + ShadingModelID(A)
//   t1 GBufferB: BaseColor(RGB) + Roughness(A)
//   t2 GBufferC: Emissive(RGB) + Specular(A)
//   t3 ShadowAtlas: 2x2 cascade layout (R32_FLOAT depth)
//   t7 DepthBuffer: Hardware depth (R32_FLOAT)
//
// CB slot g_World repurposed as InvViewProj matrix
// ============================================================

#define MAX_LIGHTS 8
#define MAX_CSM_CASCADES 4
#define PI 3.14159265359

struct LightData
{
    float3 ColorIntensity;
    int    Type;
    float3 DirectionOrPos;
    float  Radius;
};

cbuffer Constants : register(b0)
{
    row_major float4x4 g_InvViewProj; // Repurposed: inverse ViewProjection for depth reconstruction
    row_major float4x4 g_View;
    row_major float4x4 g_Projection;
    float4 g_ObjectColor;
    float  g_Selected;
    int    g_NumLights;
    float2 g_Padding;
    float3 g_CameraPos;
    float  g_Roughness;
    float  g_Metallic;
    float3 g_MaterialPadding;
    LightData g_Lights[MAX_LIGHTS];
};

cbuffer ShadowConstants : register(b1)
{
    row_major float4x4 g_LightViewProj[MAX_CSM_CASCADES];
    float4 g_CascadeSplits;       // Split distances in view-space Z
    float  g_ShadowBias;
    float  g_NormalBias;
    float  g_ShadowStrength;
    int    g_NumCascades;
    float  g_ShadowMapSize;
    float3 g_ShadowPadding;
};

// G-Buffer textures (UE5-inspired layout)
Texture2D g_GBufferA     : register(t0); // Normal(RG) + Metallic(B) + ShadingModelID(A)
Texture2D g_GBufferB     : register(t1); // BaseColor(RGB) + Roughness(A)
Texture2D g_GBufferC     : register(t2); // Emissive(RGB) + Specular(A)

// Shadow atlas (single texture, 2x2 cascade layout)
Texture2D g_ShadowAtlas : register(t3);

// Depth buffer for position reconstruction
Texture2D g_DepthBuffer : register(t7);

SamplerState g_GBufferSampler        : register(s0); // Linear clamp
SamplerComparisonState g_ShadowSampler : register(s2); // Comparison sampler for PCF

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
    // Map from [0,1] to [-1,1]
    oct = oct * 2.0 - 1.0;

    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));

    // Reflect lower hemisphere
    if (n.z < 0.0)
    {
        float2 signNotZero = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * signNotZero;
    }

    return normalize(n);
}

// ---- Reconstruct world-space position from depth ----
float3 ReconstructWorldPos(float2 uv, float depth)
{
    // Convert UV to NDC: x in [-1,1], y in [-1,1], z = depth [0,1]
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // Flip Y (UV y=0 is top, NDC y=1 is top)

    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(clipPos, g_InvViewProj);
    return worldPos.xyz / worldPos.w;
}

// ---- PCF Shadow Sampling (on atlas) ----
float SampleShadowAtlas(float2 atlasUV, float depth, float bias)
{
    float texelSize = 1.0 / g_ShadowMapSize; // g_ShadowMapSize = atlas total size
    float d = depth - bias;

    float shadow = 0.0;
    shadow += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, atlasUV, d);
    shadow += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, atlasUV + float2(texelSize, 0), d);
    shadow += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, atlasUV + float2(-texelSize, 0), d);
    shadow += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, atlasUV + float2(0, texelSize), d);
    shadow += g_ShadowAtlas.SampleCmpLevelZero(g_ShadowSampler, atlasUV + float2(0, -texelSize), d);

    return shadow / 5.0;
}

// ---- Compute shadow factor (atlas-based CSM) ----
float ComputeShadow(float3 worldPos, float viewZ)
{
    if (g_NumCascades <= 0)
        return 1.0;

    int cascadeIndex = 0;
    float splits[MAX_CSM_CASCADES] = {
        g_CascadeSplits.x, g_CascadeSplits.y, g_CascadeSplits.z, g_CascadeSplits.w
    };

    for (int i = 0; i < g_NumCascades - 1; i++)
    {
        if (viewZ > splits[i])
            cascadeIndex = i + 1;
    }

    float4 shadowPos = mul(float4(worldPos, 1.0), g_LightViewProj[cascadeIndex]);
    float3 shadowCoord;
    shadowCoord.xy = shadowPos.xy / shadowPos.w * 0.5 + 0.5;
    shadowCoord.y = 1.0 - shadowCoord.y;
    shadowCoord.z = shadowPos.z / shadowPos.w;

    // Out-of-range check (in local cascade UV space [0,1])
    if (shadowCoord.x < 0 || shadowCoord.x > 1 ||
        shadowCoord.y < 0 || shadowCoord.y > 1 ||
        shadowCoord.z < 0 || shadowCoord.z > 1)
        return 1.0;

    // Atlas 2x2 layout: [0]=top-left, [1]=top-right, [2]=bottom-left, [3]=bottom-right
    // Scale cascade UV [0,1] to atlas UV [0,0.5] and offset
    static const float2 atlasOffsets[MAX_CSM_CASCADES] = {
        float2(0.0, 0.0),  // Cascade 0: top-left
        float2(0.5, 0.0),  // Cascade 1: top-right
        float2(0.0, 0.5),  // Cascade 2: bottom-left
        float2(0.5, 0.5)   // Cascade 3: bottom-right
    };

    float2 atlasUV = shadowCoord.xy * 0.5 + atlasOffsets[cascadeIndex];

    float shadow = SampleShadowAtlas(atlasUV, shadowCoord.z, g_ShadowBias);

    return lerp(1.0, shadow, g_ShadowStrength);
}

// ============================================================
// UE5-style BRDF functions
// Reference: Engine/Shaders/Private/BRDF.ush, ShadingModels.ush
// ============================================================

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

// ---- D_GGX: GGX / Trowbridge-Reitz NDF ----
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1.0; // = NoH^2 * (a2 - 1) + 1
    return a2 / (PI * d * d);
}

// ---- Vis_SmithJointApprox: Approximate joint Smith visibility ----
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
// Returns G / (4 * NoV * NoL) — the denominator is baked in
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

// ---- F_Schlick: Fresnel Schlick approximation ----
// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
// Anything less than 2% is physically impossible and is instead considered to be shadowing
float3 F_Schlick(float3 SpecularColor, float VoH)
{
    float Fc = Pow5(1.0 - VoH);
    return saturate(50.0 * SpecularColor.g) * Fc + (1.0 - Fc) * SpecularColor;
}

// ---- Diffuse_Burley: Disney diffuse model ----
// [Burley 2012, "Physically-Based Shading at Disney"]
// More accurate than Lambert at grazing angles, roughness-dependent
float3 Diffuse_Burley(float3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH)
{
    float FD90 = 0.5 + 2.0 * VoH * VoH * Roughness;
    float FdV = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoV);
    float FdL = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoL);
    return DiffuseColor * ((1.0 / PI) * FdV * FdL);
}

// ---- EnvBRDFApprox: Environment BRDF approximation (no LUT needed) ----
// [Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II"]
float3 EnvBRDFApprox(float3 SpecularColor, float Roughness, float NoV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = Roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;

    // Anything less than 2% is physically impossible → shadow
    AB.y *= saturate(50.0 * SpecularColor.g);

    return SpecularColor * AB.x + AB.y;
}

// ---- SpecularGGX: Combined specular BRDF (D * Vis * F) ----
// Note: Vis_SmithJointApprox already includes the 1/(4*NoV*NoL) denominator
float3 SpecularGGX(float Roughness, float3 SpecularColor, float NoV, float NoL, float NoH, float VoH)
{
    float a2 = Roughness * Roughness;
    // Clamp minimum roughness to avoid NaN/Inf from D_GGX and Vis
    a2 = max(a2, 0.001);

    float  D   = D_GGX(a2, NoH);
    float  Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    float3 F   = F_Schlick(SpecularColor, VoH);

    return D * Vis * F;
}

// ---- Lighting Pixel Shader (UE5 DefaultLitBxDF style) ----
float4 PSMain(VSOutput input) : SV_TARGET
{
    // Sample G-Buffer
    float4 gbufferA = g_GBufferA.Sample(g_GBufferSampler, input.TexCoord);
    float4 gbufferB = g_GBufferB.Sample(g_GBufferSampler, input.TexCoord);
    float4 gbufferC = g_GBufferC.Sample(g_GBufferSampler, input.TexCoord);

    // Sample depth and check for sky/empty pixels
    float depth = g_DepthBuffer.Sample(g_GBufferSampler, input.TexCoord).r;
    if (depth >= 1.0)
    {
        return float4(0.12, 0.12, 0.18, 1.0); // Background color
    }

    // Decode G-Buffer (UE5 layout)
    float3 N         = OctahedronDecode(gbufferA.rg);
    float  metallic  = gbufferA.b;
    float3 baseColor = gbufferB.rgb;
    float  roughness = max(gbufferB.a, 0.04); // UE5 clamps min roughness to ~0.04
    float3 emissive  = gbufferC.rgb;
    float  specular  = gbufferC.a;

    // Reconstruct world-space position from depth
    float3 worldPos = ReconstructWorldPos(input.TexCoord, depth);

    float3 V = normalize(g_CameraPos - worldPos);
    float NoV = saturate(abs(dot(N, V)) + 1e-5); // UE5: abs + epsilon to avoid /0

    // Compute view-space Z for cascade selection
    float4 viewSpacePos = mul(float4(worldPos, 1.0), g_View);
    float viewZ = viewSpacePos.z;

    // ---- Material parameters (UE5 metallic workflow) ----
    // DiffuseColor: metals have no diffuse, dielectrics use baseColor
    float3 diffuseColor = baseColor * (1.0 - metallic);

    // SpecularColor (F0): dielectric = 0.08 * specular, metal = baseColor
    // UE5 uses 0.08 * Specular as the dielectric F0 (default Specular = 0.5 → F0 = 0.04)
    float3 specularColor = lerp(float3(0.08, 0.08, 0.08) * specular, baseColor, metallic);

    // ---- Direct Lighting ----
    float3 directDiffuse  = float3(0, 0, 0);
    float3 directSpecular = float3(0, 0, 0);

    int numLights = min(g_NumLights, MAX_LIGHTS);
    for (int i = 0; i < numLights; i++)
    {
        float3 lightColor = g_Lights[i].ColorIntensity;
        float3 L;
        float attenuation = 1.0;

        if (g_Lights[i].Type == 0) // Directional
        {
            L = normalize(g_Lights[i].DirectionOrPos);
            float shadowFactor = ComputeShadow(worldPos, viewZ);
            attenuation *= shadowFactor;
        }
        else // Point
        {
            float3 toLight = g_Lights[i].DirectionOrPos - worldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 0.0001);

            // UE5-style inverse square falloff with smooth radius attenuation
            float radius = max(g_Lights[i].Radius, 0.001);
            float normalizedDist = dist / radius;
            float falloff = saturate(1.0 - normalizedDist * normalizedDist);
            attenuation = falloff * falloff;
        }

        float NoL = saturate(dot(N, L));
        if (NoL <= 0.0) continue;

        float3 H = normalize(V + L);
        float NoH = saturate(dot(N, H));
        float VoH = saturate(dot(V, H));

        // UE5 DefaultLitBxDF: Diffuse_Burley + SpecularGGX
        float3 diffBRDF = Diffuse_Burley(diffuseColor, roughness, NoV, NoL, VoH);
        float3 specBRDF = SpecularGGX(roughness, specularColor, NoV, NoL, NoH, VoH);

        float3 radiance = lightColor * NoL * attenuation;
        directDiffuse  += diffBRDF * radiance;
        directSpecular += specBRDF * radiance;
    }

    // Fallback light if no lights in scene
    if (numLights == 0)
    {
        float3 L = normalize(float3(0.5, 0.7, 0.3));
        float NoL = saturate(dot(N, L));

        float3 H = normalize(V + L);
        float NoH = saturate(dot(N, H));
        float VoH = saturate(dot(V, H));

        float3 diffBRDF = Diffuse_Burley(diffuseColor, roughness, NoV, NoL, VoH);
        float3 specBRDF = SpecularGGX(roughness, specularColor, NoV, NoL, NoH, VoH);

        float3 radiance = float3(1, 1, 1) * NoL * 0.85;
        directDiffuse  = diffBRDF * radiance;
        directSpecular = specBRDF * radiance;
    }

    // ---- Indirect / Ambient Lighting (UE5 EnvBRDFApprox) ----
    // Diffuse ambient: simple hemisphere approximation
    float3 ambientDiffuse = diffuseColor * float3(0.03, 0.03, 0.03);

    // Specular ambient: UE5 EnvBRDFApprox (approximates split-sum IBL without LUT)
    float3 ambientSpecular = EnvBRDFApprox(specularColor, roughness, NoV)
                           * float3(0.05, 0.05, 0.05); // Approximate sky irradiance

    // ---- Final Composition ----
    float3 finalColor = directDiffuse + directSpecular + ambientDiffuse + ambientSpecular + emissive;

    // Tone mapping (Reinhard)
    finalColor = finalColor / (finalColor + 1.0);

    return float4(finalColor, 1.0);
}
