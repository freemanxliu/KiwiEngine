// ============================================================
// Default Phong Shader
// Lambert Diffuse + Blinn-Phong Specular
// Supports multiple lights (Directional + Point)
// Supports optional texture sampling
// ============================================================

// Maximum number of lights supported
#define MAX_LIGHTS 8

// Light types
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1

struct LightData
{
    float3 ColorIntensity;  // LightColor * Intensity
    int    Type;            // 0 = Directional, 1 = Point
    float3 DirectionOrPos;  // Direction (Directional) or Position (Point)
    float  Radius;          // Point light radius (0 for directional)
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

// Texture and sampler (optional — used when texture is bound)
Texture2D g_AlbedoTexture : register(t0);
SamplerState g_TextureSampler : register(s1);

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
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
    // Gizmo / unlit mode: if g_Selected > 1.5 we treat it as unlit (pure vertex color)
    if (g_Selected > 1.5)
    {
        return float4(input.Color.rgb * g_ObjectColor.rgb, input.Color.a);
    }

    float3 normal = normalize(input.NormalWS);
    float3 viewDir = normalize(g_CameraPos - input.PositionWS);

    // Base color from vertex color (texture sampling can be added here)
    float3 albedo = input.Color.rgb;

    // Ambient
    float3 ambient = float3(0.15, 0.15, 0.15);
    float3 totalDiffuse = float3(0, 0, 0);
    float3 totalSpecular = float3(0, 0, 0);

    // Accumulate light contributions
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
        else // LIGHT_TYPE_POINT
        {
            float3 toLight = g_Lights[i].DirectionOrPos - input.PositionWS;
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

    // If no lights in scene, use a default directional light (fallback)
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

    return float4(finalColor, input.Color.a);
}
