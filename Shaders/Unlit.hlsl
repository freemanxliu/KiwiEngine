// ============================================================
// Unlit Shader
// Pure color output — no lighting calculations
// ============================================================

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
    row_major float4x4 g_View;
    row_major float4x4 g_Projection;
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
    return float4(input.Color.rgb, input.Color.a);
}
