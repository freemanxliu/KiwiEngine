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
    float  g_ShadingModelID;  // EShadingModel: 0=Unlit, 1=DefaultLit
    float3 g_ObjectPadding;
    float4 g_Reserved[8];    // pad to 256 bytes
    float3 g_Reserved2;      // (116 + 128 + 12 = 256)
};

// GPU Scene. SV_InstanceID selects a slot in this draw; that slot stores the scene InstanceId.
struct PrimitiveSceneData
{
    float4 ObjectColor;
    float4 Material0; // selected, roughness, metallic, hasBaseColorTex
    float4 Material1; // hasNormalTex, shadingModel, emissive.r, emissive.g
    float4 Material2; // emissive.b
    uint4  Ids;       // x = InstanceSceneDataOffset, y = NumInstances
};

struct InstanceSceneData
{
    row_major float4x4 World;
    uint4 PrimitiveId; // x
};

struct GPUObject
{
    float4x4 World;
    float4 Color;
    float Selected;
    float Roughness;
    float Metallic;
    float HasBaseColorTex;
    float HasNormalTex;
    float ShadingModelID;
    float3 Emissive;
};

StructuredBuffer<InstanceSceneData>  g_InstanceSceneData  : register(t8);
StructuredBuffer<PrimitiveSceneData> g_PrimitiveSceneData : register(t9);
StructuredBuffer<uint4>              g_DrawInstanceIds    : register(t10);

cbuffer DrawInstanceUB : register(b4)
{
    uint g_DrawInstanceOffset;
    uint3 g_DrawInstancePad;
};

GPUObject GetGPUObject(uint svInstanceId)
{
    uint instanceId = g_DrawInstanceIds[g_DrawInstanceOffset + svInstanceId].x;
    InstanceSceneData inst = g_InstanceSceneData[instanceId];
    PrimitiveSceneData prim = g_PrimitiveSceneData[inst.PrimitiveId.x];
    GPUObject obj;
    obj.World = inst.World;
    obj.Color = prim.ObjectColor;
    obj.Selected = prim.Material0.x;
    obj.Roughness = prim.Material0.y;
    obj.Metallic = prim.Material0.z;
    obj.HasBaseColorTex = prim.Material0.w;
    obj.HasNormalTex = prim.Material1.x;
    obj.ShadingModelID = prim.Material1.y;
    obj.Emissive = float3(prim.Material1.z, prim.Material1.w, prim.Material2.x);
    return obj;
}

#endif // KIWI_COMMON_HLSLI
