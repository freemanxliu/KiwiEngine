// ============================================================
// Deferred Lighting Pass Shader
// Reads G-Buffer (Position, Normal, Albedo) and computes lighting
// Includes Cascaded Shadow Mapping (CSM)
// Uses fullscreen triangle (SV_VertexID)
// ============================================================

#define MAX_LIGHTS 8
#define MAX_CSM_CASCADES 4

struct LightData
{
    float3 ColorIntensity;
    int    Type;
    float3 DirectionOrPos;
    float  Radius;
};

cbuffer Constants : register(b0)
{
    row_major float4x4 g_World;
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

// G-Buffer textures
Texture2D g_PositionBuffer : register(t0);
Texture2D g_NormalBuffer   : register(t1);
Texture2D g_AlbedoBuffer   : register(t2);

// Shadow maps (one per cascade)
Texture2D g_ShadowMap0 : register(t3);
Texture2D g_ShadowMap1 : register(t4);
Texture2D g_ShadowMap2 : register(t5);
Texture2D g_ShadowMap3 : register(t6);

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

    // Generate full-screen triangle from vertex ID
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.TexCoord = float2(uv.x, 1.0 - uv.y);
    return output;
}

// ---- PCF Shadow Sampling ----
float SampleShadowMap(Texture2D shadowMap, float3 shadowCoord, float bias)
{
    // 5-tap PCF (center + 4 neighbors)
    float texelSize = 1.0 / g_ShadowMapSize;
    float depth = shadowCoord.z - bias;
    
    float shadow = 0.0;
    shadow += shadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowCoord.xy, depth);
    shadow += shadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowCoord.xy + float2(texelSize, 0), depth);
    shadow += shadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowCoord.xy + float2(-texelSize, 0), depth);
    shadow += shadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowCoord.xy + float2(0, texelSize), depth);
    shadow += shadowMap.SampleCmpLevelZero(g_ShadowSampler, shadowCoord.xy + float2(0, -texelSize), depth);
    
    return shadow / 5.0;
}

// ---- Compute shadow factor for a world-space position ----
float ComputeShadow(float3 worldPos, float viewZ)
{
    if (g_NumCascades <= 0)
        return 1.0; // No shadow

    // Select cascade based on view-space Z distance
    int cascadeIndex = 0;
    float splits[MAX_CSM_CASCADES] = {
        g_CascadeSplits.x, g_CascadeSplits.y, g_CascadeSplits.z, g_CascadeSplits.w
    };
    
    for (int i = 0; i < g_NumCascades - 1; i++)
    {
        if (viewZ > splits[i])
            cascadeIndex = i + 1;
    }
    
    // Project world position into light space
    float4 shadowPos = mul(float4(worldPos, 1.0), g_LightViewProj[cascadeIndex]);
    float3 shadowCoord;
    shadowCoord.xy = shadowPos.xy / shadowPos.w * 0.5 + 0.5;
    shadowCoord.y = 1.0 - shadowCoord.y; // Flip Y for UV
    shadowCoord.z = shadowPos.z / shadowPos.w;
    
    // Check if outside shadow map bounds
    if (shadowCoord.x < 0 || shadowCoord.x > 1 || 
        shadowCoord.y < 0 || shadowCoord.y > 1 ||
        shadowCoord.z < 0 || shadowCoord.z > 1)
        return 1.0;
    
    // Sample the correct cascade shadow map
    float shadow = 1.0;
    if (cascadeIndex == 0)
        shadow = SampleShadowMap(g_ShadowMap0, shadowCoord, g_ShadowBias);
    else if (cascadeIndex == 1)
        shadow = SampleShadowMap(g_ShadowMap1, shadowCoord, g_ShadowBias);
    else if (cascadeIndex == 2)
        shadow = SampleShadowMap(g_ShadowMap2, shadowCoord, g_ShadowBias);
    else
        shadow = SampleShadowMap(g_ShadowMap3, shadowCoord, g_ShadowBias);
    
    // Apply shadow strength
    return lerp(1.0, shadow, g_ShadowStrength);
}

// ---- Lighting Pixel Shader ----
float4 PSMain(VSOutput input) : SV_TARGET
{
    // Sample G-Buffer
    float4 positionData = g_PositionBuffer.Sample(g_GBufferSampler, input.TexCoord);
    float4 normalData   = g_NormalBuffer.Sample(g_GBufferSampler, input.TexCoord);
    float4 albedoData   = g_AlbedoBuffer.Sample(g_GBufferSampler, input.TexCoord);

    // Skip pixels with no geometry (alpha == 0 in albedo)
    if (albedoData.a < 0.01)
    {
        return float4(0.12, 0.12, 0.18, 1.0); // Background color
    }

    float3 worldPos = positionData.xyz;
    float3 normal = normalize(normalData.xyz * 2.0 - 1.0); // Unpack from [0,1] to [-1,1]
    float3 albedo = albedoData.rgb;

    float3 viewDir = normalize(g_CameraPos - worldPos);
    
    // Compute view-space Z for cascade selection
    float4 viewSpacePos = mul(float4(worldPos, 1.0), g_View);
    float viewZ = viewSpacePos.z;

    // Ambient
    float3 ambient = float3(0.15, 0.15, 0.15);
    float3 totalDiffuse = float3(0, 0, 0);
    float3 totalSpecular = float3(0, 0, 0);

    int numLights = min(g_NumLights, MAX_LIGHTS);
    for (int i = 0; i < numLights; i++)
    {
        float3 lightColor = g_Lights[i].ColorIntensity;
        float3 lightDir;
        float attenuation = 1.0;

        if (g_Lights[i].Type == 0) // Directional
        {
            lightDir = normalize(g_Lights[i].DirectionOrPos);
            
            // Apply CSM shadow for directional lights
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

        // Lambertian diffuse
        float NdotL = max(dot(normal, lightDir), 0.0);
        totalDiffuse += lightColor * NdotL * attenuation;

        // Blinn-Phong specular
        float3 halfVec = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfVec), 0.0), 32.0);
        totalSpecular += lightColor * spec * 0.3 * attenuation;
    }

    // Fallback light if no lights in scene
    if (numLights == 0)
    {
        float3 defaultLightDir = normalize(float3(0.5, 0.7, 0.3));
        float NdotL = max(dot(normal, defaultLightDir), 0.0);
        totalDiffuse = float3(1, 1, 1) * NdotL * 0.85;

        float3 halfVec = normalize(defaultLightDir + viewDir);
        float spec = pow(max(dot(normal, halfVec), 0.0), 32.0);
        totalSpecular = float3(0.3, 0.3, 0.3) * spec * 0.5;
    }

    float3 finalColor = albedo * (ambient + totalDiffuse) + totalSpecular;

    return float4(finalColor, 1.0);
}
