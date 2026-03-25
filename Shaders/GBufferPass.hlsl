// ============================================================
// G-Buffer Geometry Pass Shader
// Writes to 3 MRT: Position, Normal(+Roughness), Albedo(+Metallic)
// Used in deferred rendering pipeline
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
    float  g_Roughness;
    float  g_Metallic;
    float3 g_MaterialPadding;
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
    float3 PositionWS : TEXCOORD0;
    float3 NormalWS   : TEXCOORD1;
    float4 Color      : COLOR;
    float2 TexCoord   : TEXCOORD2;
};

// G-Buffer MRT output
struct GBufferOutput
{
    float4 Position : SV_TARGET0;  // World-space position (RGB) + unused (A)
    float4 Normal   : SV_TARGET1;  // World-space normal (RGB) + Roughness (A)
    float4 Albedo   : SV_TARGET2;  // Albedo color (RGB) + Metallic (A)
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
    output.NormalWS = normalize(mul(input.Normal, (float3x3)g_World));
    output.Color = input.Color * g_ObjectColor;
    output.TexCoord = input.TexCoord;

    return output;
}

// ---- Pixel Shader ----
GBufferOutput PSMain(VSOutput input)
{
    GBufferOutput output;

    output.Position = float4(input.PositionWS, 1.0);
    output.Normal = float4(normalize(input.NormalWS) * 0.5 + 0.5, g_Roughness); // Pack normal to [0,1], A = Roughness
    output.Albedo = float4(input.Color.rgb, g_Metallic);                          // Albedo RGB, A = Metallic

    return output;
}
