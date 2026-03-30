#pragma once

#include <cstdint>

namespace Kiwi
{

    // ============================================================
    // Constant Buffer Structures (UE5-inspired layered design)
    //
    // b0 = ViewUniformBuffer   — per-frame view/camera/lights data
    // b1 = ObjectUniformBuffer — per-draw object/material data
    // b2 = ShadowUniformBuffer — per-frame CSM shadow data
    //
    // This separation minimizes GPU uploads:
    //   - View CB uploaded once per frame
    //   - Object CB uploaded once per draw call (small, ~112 bytes)
    //   - Shadow CB uploaded once per frame
    // ============================================================

    static constexpr int MAX_LIGHTS = 8;
    static constexpr int MAX_CSM_CASCADES = 4;

    // GPU light data (must match HLSL LightData struct)
    struct GPULightData
    {
        float ColorIntensity[3];  // LightColor * Intensity
        int32_t Type;             // 0 = Directional, 1 = Point
        float DirectionOrPos[3];  // Direction (Directional) or Position (Point)
        float Radius;             // Point light radius (0 for directional)
    };

    // ---- View Uniform Buffer (b0) — per-frame, uploaded once ----
    // Reference: UE5 FViewUniformShaderParameters
    struct ViewUniformBuffer
    {
        float ViewMatrix[16];           // g_View           — camera view matrix
        float ProjectionMatrix[16];     // g_Projection     — camera projection matrix
        float ViewProjectionMatrix[16]; // g_ViewProjection — View * Projection
        float InvViewProjectionMatrix[16]; // g_InvViewProj — inverse of ViewProjection
        float CameraPos[3];             // g_CameraPos      — world-space camera position
        float ViewPadding1;             // pad to 16-byte
        float ScreenWidth;              // g_ScreenWidth
        float ScreenHeight;             // g_ScreenHeight
        float NearPlane;                // g_NearPlane
        float FarPlane;                 // g_FarPlane
        int32_t NumLights;              // g_NumLights
        float ViewPadding2[3];          // pad to 16-byte
        GPULightData Lights[MAX_LIGHTS]; // g_Lights[8]
    };
    // Size: 4*64 + 16 + 16 + 16 + 8*32 = 560 bytes

    // ---- Object Uniform Buffer (b1) — per-draw call ----
    // Reference: UE5 FPrimitiveUniformShaderData
    struct ObjectUniformBuffer
    {
        float WorldMatrix[16];       // g_World         — object world transform
        float ObjectColor[4];        // g_ObjectColor   — object tint color (RGBA)
        float Selected;              // g_Selected      — 0.0=normal, 1.0=selected, 2.0=unlit/gizmo
        float Roughness;             // g_Roughness     — PBR roughness [0,1]
        float Metallic;              // g_Metallic      — PBR metallic [0,1]
        float HasBaseColorTex;       // g_HasBaseColorTex — 1.0 if texture bound
        float HasNormalTex;          // g_HasNormalTex    — 1.0 if normal map bound
        float VisualizeMode;         // g_VisualizeMode   — buffer vis mode (0=BaseColor,1=Roughness,2=Metallic)
        float ObjectPadding[2];      // pad to 16-byte alignment
    };
    // Size: 64 + 16 + 16 + 16 = 112 bytes

    // ---- Shadow Uniform Buffer (b2) — per-frame CSM data ----
    struct ShadowUniformBuffer
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

    // ---- Legacy aliases (keep old name working during transition) ----
    using ConstantBufferData = ViewUniformBuffer;  // TODO: remove after full migration
    using ShadowCBData = ShadowUniformBuffer;

    // ============================================================
    // Inline HLSL shader source (for InputLayout creation only)
    // Uses the new split CB layout
    // ============================================================

    inline const char* g_VertexShaderHLSL = R"hlsl(
    #define MAX_LIGHTS 8

    struct LightData
    {
        float3 ColorIntensity;
        int    Type;
        float3 DirectionOrPos;
        float  Radius;
    };

    // View Uniform Buffer (b0) — per-frame
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

    // Object Uniform Buffer (b1) — per-draw
    cbuffer ObjectUB : register(b1)
    {
        row_major float4x4 g_World;
        float4 g_ObjectColor;
        float  g_Selected;
        float  g_Roughness;
        float  g_Metallic;
        float  g_HasBaseColorTex;
        float  g_HasNormalTex;
        float  g_VisualizeMode;
        float2 g_ObjectPadding;
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

    inline const char* g_PixelShaderHLSL = R"hlsl(
    #define MAX_LIGHTS 8

    struct LightData
    {
        float3 ColorIntensity;
        int    Type;
        float3 DirectionOrPos;
        float  Radius;
    };

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

    cbuffer ObjectUB : register(b1)
    {
        row_major float4x4 g_World;
        float4 g_ObjectColor;
        float  g_Selected;
        float  g_Roughness;
        float  g_Metallic;
        float  g_HasBaseColorTex;
        float  g_HasNormalTex;
        float  g_VisualizeMode;
        float2 g_ObjectPadding;
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
