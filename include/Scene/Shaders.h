#pragma once

#include <cstdint>

namespace Kiwi
{

    // ============================================================
    // 内嵌 HLSL 着色器源码（运行时编译）
    // ============================================================

    // Maximum number of lights supported
    static constexpr int MAX_LIGHTS = 8;

    // GPU light data (must match HLSL LightData struct)
    struct GPULightData
    {
        float ColorIntensity[3];  // LightColor * Intensity
        int32_t Type;             // 0 = Directional, 1 = Point
        float DirectionOrPos[3];  // Direction (Directional) or Position (Point)
        float Radius;             // Point light radius (0 for directional)
    };

    // ============================================================
    // UE5-style View UniformBuffer (register b0, updated once per frame)
    // Bound automatically to ALL materials — shared across all shaders.
    // Contains camera, lighting, screen, and development override parameters.
    // ============================================================
    struct ViewBufferData
    {
        float ViewMatrix[16];             // 4x4 View matrix
        float ProjectionMatrix[16];       // 4x4 Projection matrix
        float InvViewProjMatrix[16];      // 4x4 Inverse ViewProjection (for deferred/screen-space passes)
        float CameraPos[3];               // Camera world-space position
        float Time;                       // Elapsed time in seconds
        int32_t NumLights;                // Active light count
        float ViewPadding0[3];            // 16-byte alignment
        // UE5-style development override parameters
        float DiffuseOverride[4];         // xyz = override color, w = weight (0=none, 1=full)
        float SpecularOverride[4];        // xyz = override color, w = weight (0=none, 1=full)
        // Screen parameters
        float ScreenSize[2];              // (Width, Height)
        float InvScreenSize[2];           // (1/Width, 1/Height)
        float ViewPadding1[4];            // 16-byte alignment
        GPULightData Lights[MAX_LIGHTS];  // Light array
    };

    // ============================================================
    // Object UniformBuffer (register b1, updated per-object)
    // Contains per-object transform and material parameters.
    // ============================================================
    struct ObjectBufferData
    {
        float WorldMatrix[16];        // 4x4 World matrix
        float ObjectColor[4];         // Object/material tint (RGBA)
        float Selected;               // 1.0 = selected, 0.0 = normal, >1.5 = unlit/gizmo
        float Roughness;              // Material roughness [0, 1]
        float Metallic;               // Material metallic [0, 1]
        float HasBaseColorTex;        // 1.0 if base color texture bound
        float HasNormalTex;           // 1.0 if normal map bound
        float ObjectPadding[3];       // 16-byte alignment
    };

    // ============================================================
    // Legacy alias — kept for backward compatibility during transition
    // TODO: Remove once all call sites are migrated
    // ============================================================
    struct ConstantBufferData
    {
        float WorldMatrix[16];
        float ViewMatrix[16];
        float ProjectionMatrix[16];
        float ObjectColor[4];
        float Selected;
        int32_t NumLights;
        float Padding[2];
        float CameraPos[3];
        float Roughness;
        float Metallic;
        float HasBaseColorTex;
        float HasNormalTex;
        float MaterialPadding;
        GPULightData Lights[MAX_LIGHTS];
    };

    // ============================================================
    // Shadow constant buffer (register b2) — CSM data for shadow mapping
    // ============================================================
    static constexpr int MAX_CSM_CASCADES = 4;

    struct ShadowCBData
    {
        float LightViewProj[MAX_CSM_CASCADES][16];  // 4x Light VP matrices (row-major)
        float CascadeSplits[4];                       // Split distances in view space (z)
        float ShadowBias;
        float NormalBias;
        float ShadowStrength;
        int32_t NumCascades;
        float ShadowMapSize;                          // Shadow map resolution
        float ShadowPadding[3];                       // Pad to 16-byte alignment
    };

    // ---- 顶点着色器 ----
    // This minimal VS is used only to create the shared InputLayout.
    // Actual rendering uses file-based shaders from ShaderLibrary.
    inline const char* g_VertexShaderHLSL = R"hlsl(
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

    VSOutput main(VSInput input)
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
    )hlsl";

    // ---- 像素着色器 ----
    // This minimal PS matches the CB layout but is not used for rendering.
    inline const char* g_PixelShaderHLSL = R"hlsl(
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

    struct PSInput
    {
        float4 PositionCS : SV_POSITION;
        float3 PositionWS : POSITION;
        float3 NormalWS   : NORMAL;
        float4 Color      : COLOR;
        float2 TexCoord   : TEXCOORD;
    };

    float4 main(PSInput input) : SV_TARGET
    {
        float3 lightDir = normalize(float3(0.5, 0.7, 0.3));
        float3 normal = normalize(input.NormalWS);
        float NdotL = max(dot(normal, lightDir), 0.0);
        float ambient = 0.15;
        float3 finalColor = input.Color.rgb * (ambient + NdotL * 0.85);

        if (g_Selected > 1.5)
        {
            return float4(input.Color.rgb * g_ObjectColor.rgb, input.Color.a);
        }

        return float4(finalColor, input.Color.a);
    }
    )hlsl";

} // namespace Kiwi
