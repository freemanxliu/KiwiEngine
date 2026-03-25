// ============================================================
// Deferred Lighting Pass Shader
// Reads G-Buffer (Position, Normal, Albedo) and computes lighting
// Uses fullscreen triangle (SV_VertexID)
// ============================================================

#define MAX_LIGHTS 8

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
    float  g_Padding2;
    LightData g_Lights[MAX_LIGHTS];
};

// G-Buffer textures
Texture2D g_PositionBuffer : register(t0);
Texture2D g_NormalBuffer   : register(t1);
Texture2D g_AlbedoBuffer   : register(t2);

SamplerState g_GBufferSampler : register(s0); // Linear clamp

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
