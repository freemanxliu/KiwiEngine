// ============================================================
// Shadow Pass Shader - Depth-only rendering from light's perspective
// Used for Cascaded Shadow Mapping (CSM)
//
// Two vertex shader paths:
//   Default:                   Reads world matrix from ObjectUB (b1) via CB offset.
//   USE_GPU_SCENE_INSTANCING:  Reads world matrix from StructuredBuffer (t8) via SV_InstanceID.
// ============================================================

#include "Common.hlsli"

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD;
    uint   InstanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 PositionCS : SV_POSITION;
};

// ---- Vertex Shader ----
VSOutput VSMain(VSInput input)
{
    VSOutput output;

#ifdef USE_GPU_SCENE_INSTANCING
    GPUSceneData inst = GetInstanceData(input.InstanceID);
    float4 worldPos = mul(float4(input.Position, 1.0), inst.World);
#else
    float4 worldPos = mul(float4(input.Position, 1.0), g_World);
#endif

    float4 viewPos = mul(worldPos, g_View);
    float4 projPos = mul(viewPos, g_Projection);

    output.PositionCS = projPos;
    return output;
}

// No pixel shader needed - depth-only pass
