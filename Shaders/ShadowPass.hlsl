// ============================================================
// Shadow Pass Shader — Depth-only rendering from light's perspective
// Used for Cascaded Shadow Mapping (CSM)
// ============================================================

#include "Common.hlsli"

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
// Note: For shadow pass, g_View/g_Projection in ViewUB are set to light's VP,
// and g_World in ObjectUB is the object's world transform.
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
