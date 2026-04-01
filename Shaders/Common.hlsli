// ============================================================
// KiwiEngine Common Shader Definitions
// Shared cbuffer declarations for all shaders
//
// UE5-inspired constant buffer layout:
//   b0 = ViewUB   (per-frame: camera, lights)
//   b1 = ObjectUB (per-draw: transform, material)
//   b2 = ShadowUB (per-frame: CSM data)
// ============================================================

#ifndef KIWI_COMMON_HLSLI
#define KIWI_COMMON_HLSLI

#define MAX_LIGHTS 8

struct LightData
{
    float3 ColorIntensity;
    int    Type;            // 0 = Directional, 1 = Point
    float3 DirectionOrPos;
    float  Radius;
};

// ---- View Uniform Buffer (b0) — updated once per frame ----
cbuffer ViewUB : register(b0)
{
    row_major float4x4 g_View;
    row_major float4x4 g_Projection;
    row_major float4x4 g_ViewProjection;
    row_major float4x4 g_InvViewProj;
    float3 g_CameraPos;
    float  g_ViewPadding1;
    float  g_ScreenWidth;
    float  g_ScreenHeight;
    float  g_NearPlane;
    float  g_FarPlane;
    int    g_NumLights;
    float3 g_ViewPadding2;
    LightData g_Lights[MAX_LIGHTS];
};

// ---- Object Uniform Buffer (b1) — used for non-instanced draws (fullscreen/gizmo) ----
cbuffer ObjectUB : register(b1)
{
    row_major float4x4 g_World;
    float4 g_ObjectColor;
    float  g_Selected;       // 0=normal, 1=selected, 2=unlit/gizmo
    float  g_Roughness;
    float  g_Metallic;
    float  g_HasBaseColorTex;
    float  g_HasNormalTex;
    float  g_VisualizeMode;  // Buffer visualization mode
    float2 g_ObjectPadding;
    float4 g_Reserved[9];    // pad to 256 bytes (112 + 9*16 = 256)
};

// ---- GPU Scene StructuredBuffer (t8) — for instanced draws ----
// Each element = one ObjectUniformBuffer (16 floats4 = 256 bytes)
// Shader reads: g_GPUScene[batchStartIndex + SV_InstanceID]
struct GPUSceneData
{
    row_major float4x4 World;
    float4 ObjectColor;
    float  Selected;
    float  Roughness;
    float  Metallic;
    float  HasBaseColorTex;
    float  HasNormalTex;
    float  VisualizeMode;
    float2 Padding;
    float4 Reserved[9];
};

StructuredBuffer<GPUSceneData> g_GPUScene : register(t8);

// ---- Batch Start Index (b4) — per-batch constant ----
// Tells the shader where this batch starts in g_GPUScene
cbuffer BatchUB : register(b4)
{
    uint g_BatchStartIndex;
    uint3 g_BatchPadding;
};

// Helper: Get per-instance object data from GPU Scene
GPUSceneData GetInstanceData(uint instanceID)
{
    return g_GPUScene[g_BatchStartIndex + instanceID];
}

#endif // KIWI_COMMON_HLSLI
