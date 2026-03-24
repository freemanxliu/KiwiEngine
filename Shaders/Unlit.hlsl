// ============================================================
// Unlit Shader
// Pure color output — no lighting calculations
// ============================================================

cbuffer Constants : register(b0)
{
    row_major float4x4 g_World;
    row_major float4x4 g_View;
    row_major float4x4 g_Projection;
    float4 g_ObjectColor;
    float  g_Selected;
    float3 g_Padding;
};

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
};

struct VSOutput
{
    float4 PositionCS : SV_POSITION;
    float3 PositionWS : POSITION;
    float3 NormalWS   : NORMAL;
    float4 Color      : COLOR;
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

    return output;
}

// ---- Pixel Shader ----
float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(input.Color.rgb, input.Color.a);
}
