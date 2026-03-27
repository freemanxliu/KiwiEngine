// ============================================================
// Buffer Visualization Shader (UE5-inspired GBuffer layout)
// Displays a single channel from the G-Buffer for debug viewing
// Uses fullscreen triangle (SV_VertexID)
//
// GBuffer layout:
//   t0 GBufferA: Normal Octahedron(RG) + Metallic(B) + ShadingModelID(A)
//   t1 GBufferB: BaseColor(RGB) + Roughness(A)
//   t2 GBufferC: Emissive(RGB) + Specular(A)
//
// VisualizeMode (passed via g_Selected):
//   0 = BaseColor  (GBufferB.rgb)
//   1 = Roughness  (GBufferB.a — grayscale)
//   2 = Metallic   (GBufferA.b — grayscale)
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
    row_major float4x4 g_World;       // InvViewProj (repurposed)
    row_major float4x4 g_View;
    row_major float4x4 g_Projection;
    float4 g_ObjectColor;
    float  g_Selected;         // Repurposed: 0=BaseColor, 1=Roughness, 2=Metallic
    int    g_NumLights;
    float2 g_Padding;
    float3 g_CameraPos;
    float  g_Roughness;
    float  g_Metallic;
    float  g_HasBaseColorTex;
    float  g_HasNormalTex;
    float  g_MaterialPadding;
    LightData g_Lights[MAX_LIGHTS];
};

// G-Buffer textures (UE5-inspired layout)
Texture2D g_GBufferA : register(t0); // Normal(RG) + Metallic(B) + ShadingModelID(A)
Texture2D g_GBufferB : register(t1); // BaseColor(RGB) + Roughness(A)
Texture2D g_GBufferC : register(t2); // Emissive(RGB) + Specular(A)

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

// ---- Octahedron Normal Decoding (for potential WorldNormal visualization) ----
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

    // Skip pixels with no geometry (check if normal is zero — encoded as (0.5, 0.5) in octahedron)
    // A zero-length encoded normal with zero metallic and zero roughness means no geometry
    if (gbufferA.r == 0.0 && gbufferA.g == 0.0 && gbufferB.r == 0.0 && gbufferB.g == 0.0 && gbufferB.b == 0.0)
    {
        return float4(0.12, 0.12, 0.18, 1.0); // Background
    }

    int mode = (int)(g_Selected + 0.5); // Round to nearest int

    if (mode == 0)
    {
        // BaseColor: GBufferB.rgb
        return float4(gbufferB.rgb, 1.0);
    }
    else if (mode == 1)
    {
        // Roughness: GBufferB.a as grayscale
        float roughness = gbufferB.a;
        return float4(roughness, roughness, roughness, 1.0);
    }
    else // mode == 2
    {
        // Metallic: GBufferA.b as grayscale
        float metallic = gbufferA.b;
        return float4(metallic, metallic, metallic, 1.0);
    }
}
