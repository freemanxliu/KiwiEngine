// ============================================================
// Shadow Pass Shader — Depth-only rendering from light's perspective
// Used for Cascaded Shadow Mapping (CSM)
// ============================================================

// We reuse the View CB (b0) for View matrix,
// and Object CB (b1) for World matrix.
// For shadow pass, the View matrix is the light's VP (baked by C++ code).
#define MAX_LIGHTS 8

struct LightData
{
    float3 ColorIntensity;
    int    Type;
    float3 DirectionOrPos;
    float  Radius;
};

cbuffer ViewConstants : register(b0)
{
    row_major float4x4 g_View;       // Light's View*Projection (baked by C++)
    row_major float4x4 g_Projection; // Identity (VP already combined in g_View)
    row_major float4x4 g_InvViewProj;
    float3 g_CameraPos;
    float  g_Time;
    int    g_NumLights;
    float3 g_ViewPad0;
    float4 g_DiffuseOverride;
    float4 g_SpecularOverride;
    float2 g_ScreenSize;
    float2 g_InvScreenSize;
    float4 g_ViewPad1;
    LightData g_Lights[MAX_LIGHTS];
};

cbuffer ObjectConstants : register(b1)
{
    row_major float4x4 g_World;
    float4 g_ObjectColor;
    float  g_Selected;
    float  g_Roughness;
    float  g_Metallic;
    float  g_HasBaseColorTex;
    float  g_HasNormalTex;
    float3 g_ObjPad;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
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
