// ============================================================
// Buffer Visualization Shader
// Displays a single channel from the G-Buffer for debug viewing
// Uses fullscreen triangle (SV_VertexID)
//
// VisualizeMode (passed via g_Selected):
//   0 = BaseColor (Albedo RT2.rgb)
//   1 = Roughness (Normal RT1.a — grayscale)
//   2 = Metallic  (Albedo RT2.a — grayscale)
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
    float  g_Selected;         // Repurposed: 0=BaseColor, 1=Roughness, 2=Metallic
    int    g_NumLights;
    float2 g_Padding;
    float3 g_CameraPos;
    float  g_Roughness;
    float  g_Metallic;
    float3 g_MaterialPadding;
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
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.TexCoord = float2(uv.x, 1.0 - uv.y);
    return output;
}

// ---- Pixel Shader ----
float4 PSMain(VSOutput input) : SV_TARGET
{
    float4 normalData = g_NormalBuffer.Sample(g_GBufferSampler, input.TexCoord);
    float4 albedoData = g_AlbedoBuffer.Sample(g_GBufferSampler, input.TexCoord);

    // Skip pixels with no geometry
    if (albedoData.a < 0.001 && normalData.a < 0.001)
    {
        // Check position buffer for geometry presence
        float4 posData = g_PositionBuffer.Sample(g_GBufferSampler, input.TexCoord);
        if (posData.a < 0.01)
            return float4(0.12, 0.12, 0.18, 1.0); // Background
    }

    int mode = (int)(g_Selected + 0.5); // Round to nearest int

    if (mode == 0)
    {
        // BaseColor: show albedo RGB
        return float4(albedoData.rgb, 1.0);
    }
    else if (mode == 1)
    {
        // Roughness: show as grayscale from Normal buffer alpha
        float roughness = normalData.a;
        return float4(roughness, roughness, roughness, 1.0);
    }
    else // mode == 2
    {
        // Metallic: show as grayscale from Albedo buffer alpha
        float metallic = albedoData.a;
        return float4(metallic, metallic, metallic, 1.0);
    }
}
