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

    // 常量缓冲区结构（必须与 HLSL 匹配）
    struct ConstantBufferData
    {
        float WorldMatrix[16];       // 4x4 matrix
        float ViewMatrix[16];        // 4x4 matrix
        float ProjectionMatrix[16];  // 4x4 matrix
        float ObjectColor[4];        // Object color (RGBA)
        float Selected;              // 1.0 if selected, 0.0 otherwise
        int32_t NumLights;           // Number of active lights
        float Padding[2];            // Pad to 16-byte alignment
        float CameraPos[3];          // Camera position in world space
        float Padding2;              // Pad to 16-byte alignment
        GPULightData Lights[MAX_LIGHTS]; // Light array
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
        float  g_Padding2;
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
        float  g_Padding2;
        LightData g_Lights[MAX_LIGHTS];
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
