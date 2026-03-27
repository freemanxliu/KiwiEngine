// ============================================================
// Shadow Pass Shader — Depth-only rendering from light's perspective
// Used for Cascaded Shadow Mapping (CSM)
// ============================================================

// We reuse the main CB (b0) for World matrix,
// but View/Projection are the light's VP for this cascade
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
    float  g_Roughness;
    float  g_Metallic;
    float  g_HasBaseColorTex;
    float  g_HasNormalTex;
    float  g_MaterialPadding;
    LightData g_Lights[MAX_LIGHTS];
};

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
};

// ---- Vertex Shader ----
// Transform vertex by World * LightView * LightProjection
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0), g_World);
    float4 viewPos = mul(worldPos, g_View);
    float4 projPos = mul(viewPos, g_Projection);

    output.PositionCS = projPos;
    return output;
}

// No pixel shader needed — depth-only pass
// DX11 can use a null PS (outputs nothing)
// DX12 PSO can be created with nullptr PS
