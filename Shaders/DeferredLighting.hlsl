// ============================================================
// Deferred Lighting Pass Shader (UE5-inspired GBuffer layout)
// Reads G-Buffer + Depth and computes PBR-style lighting
// Includes Cascaded Shadow Mapping (CSM)
// Uses fullscreen triangle (SV_VertexID)
//
// G-Buffer layout (all R8G8B8A8_UNORM):
//   t0 GBufferA: Normal Octahedron(RG) + Metallic(B) + ShadingModelID(A)
//   t1 GBufferB: BaseColor(RGB) + Roughness(A)
//   t2 GBufferC: Emissive(RGB) + Specular(A)
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

// Shadow maps (one per cascade)
Texture2D g_ShadowMap0 : register(t3);
Texture2D g_ShadowMap1 : register(t4);
Texture2D g_ShadowMap2 : register(t5);
Texture2D g_ShadowMap3 : register(t6);

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

// ---- PCF Shadow Sampling ----
float SampleShadowMap(Texture2D shadowMap, float3 shadowCoord, float bias)
{
    float texelSize = 1.0 / g_ShadowMapSize;
    float d = shadowCoord.z - bias;

    float shadow = 0.0;
    shadow += shadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowCoord.xy, d);
    shadow += shadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowCoord.xy + float2(texelSize, 0), d);
    shadow += shadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowCoord.xy + float2(-texelSize, 0), d);
    shadow += shadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowCoord.xy + float2(0, texelSize), d);
    shadow += shadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowCoord.xy + float2(0, -texelSize), d);

    return shadow / 5.0;
}

// ---- Compute shadow factor ----
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

    if (shadowCoord.x < 0 || shadowCoord.x > 1 ||
        shadowCoord.y < 0 || shadowCoord.y > 1 ||
        shadowCoord.z < 0 || shadowCoord.z > 1)
        return 1.0;

    float shadow = 1.0;
    if (cascadeIndex == 0)
        shadow = SampleShadowMap(g_ShadowMap0, shadowCoord, g_ShadowBias);
    else if (cascadeIndex == 1)
        shadow = SampleShadowMap(g_ShadowMap1, shadowCoord, g_ShadowBias);
    else if (cascadeIndex == 2)
        shadow = SampleShadowMap(g_ShadowMap2, shadowCoord, g_ShadowBias);
    else
        shadow = SampleShadowMap(g_ShadowMap3, shadowCoord, g_ShadowBias);

    return lerp(1.0, shadow, g_ShadowStrength);
}

// ---- Fresnel Schlick approximation ----
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// ---- GGX Normal Distribution Function ----
float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 0.0001);
}

// ---- Smith's geometry function (Schlick-GGX) ----
float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
    float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
    return ggx1 * ggx2;
}

// ---- Lighting Pixel Shader ----
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

    // Decode G-Buffer
    float3 normal    = OctahedronDecode(gbufferA.rg);
    float  metallic  = gbufferA.b;
    float3 baseColor = gbufferB.rgb;
    float  roughness = gbufferB.a;
    float3 emissive  = gbufferC.rgb;
    float  specular  = gbufferC.a;

    // Reconstruct world-space position from depth
    float3 worldPos = ReconstructWorldPos(input.TexCoord, depth);

    float3 viewDir = normalize(g_CameraPos - worldPos);
    float NdotV = max(dot(normal, viewDir), 0.0001);

    // Compute view-space Z for cascade selection
    float4 viewSpacePos = mul(float4(worldPos, 1.0), g_View);
    float viewZ = viewSpacePos.z;

    // PBR: compute F0 (base reflectivity)
    // Dielectric: F0 = 0.04 * specular_scale, Metallic: F0 = baseColor
    float3 F0 = lerp(float3(0.04, 0.04, 0.04) * (specular * 2.0), baseColor, metallic);

    // Ambient
    float3 ambient = float3(0.03, 0.03, 0.03) * baseColor;

    float3 Lo = float3(0, 0, 0); // Outgoing radiance

    int numLights = min(g_NumLights, MAX_LIGHTS);
    for (int i = 0; i < numLights; i++)
    {
        float3 lightColor = g_Lights[i].ColorIntensity;
        float3 lightDir;
        float attenuation = 1.0;

        if (g_Lights[i].Type == 0) // Directional
        {
            lightDir = normalize(g_Lights[i].DirectionOrPos);
            float shadowFactor = ComputeShadow(worldPos, viewZ);
            attenuation *= shadowFactor;
        }
        else // Point
        {
            float3 toLight = g_Lights[i].DirectionOrPos - worldPos;
            float dist = length(toLight);
            lightDir = toLight / max(dist, 0.0001);

            float radius = max(g_Lights[i].Radius, 0.001);
            float normalizedDist = dist / radius;
            attenuation = saturate(1.0 - normalizedDist * normalizedDist);
            attenuation *= attenuation;
        }

        float NdotL = max(dot(normal, lightDir), 0.0);
        if (NdotL <= 0.0) continue;

        float3 halfVec = normalize(lightDir + viewDir);
        float NdotH = max(dot(normal, halfVec), 0.0);
        float HdotV = max(dot(halfVec, viewDir), 0.0);

        // Cook-Torrance BRDF
        float  D = DistributionGGX(NdotH, roughness);
        float  G = GeometrySmith(NdotV, NdotL, roughness);
        float3 F = FresnelSchlick(HdotV, F0);

        float3 specularBRDF = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

        // Energy conservation: diffuse = 1 - specular (metals have no diffuse)
        float3 kD = (1.0 - F) * (1.0 - metallic);
        float3 diffuse = kD * baseColor / PI;

        Lo += (diffuse + specularBRDF) * lightColor * NdotL * attenuation;
    }

    // Fallback light if no lights in scene
    if (numLights == 0)
    {
        float3 defaultLightDir = normalize(float3(0.5, 0.7, 0.3));
        float NdotL = max(dot(normal, defaultLightDir), 0.0);

        float3 halfVec = normalize(defaultLightDir + viewDir);
        float NdotH = max(dot(normal, halfVec), 0.0);
        float HdotV = max(dot(halfVec, viewDir), 0.0);

        float  D = DistributionGGX(NdotH, roughness);
        float  G = GeometrySmith(NdotV, NdotL, roughness);
        float3 F = FresnelSchlick(HdotV, F0);

        float3 specBRDF = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);
        float3 kD = (1.0 - F) * (1.0 - metallic);
        float3 diff = kD * baseColor / PI;

        Lo = (diff + specBRDF) * float3(1, 1, 1) * NdotL * 0.85;
    }

    float3 finalColor = ambient + Lo + emissive;

    // Simple tone mapping (Reinhard)
    finalColor = finalColor / (finalColor + 1.0);

    return float4(finalColor, 1.0);
}
