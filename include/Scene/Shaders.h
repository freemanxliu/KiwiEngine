#pragma once

namespace Kiwi
{

    // ============================================================
    // 内嵌 HLSL 着色器源码（运行时编译）
    // ============================================================

    // 常量缓冲区结构（必须与 HLSL 匹配）
    struct ConstantBufferData
    {
        float WorldMatrix[16];      // 4x4 matrix
        float ViewMatrix[16];       // 4x4 matrix
        float ProjectionMatrix[16]; // 4x4 matrix
    };

    // ---- 顶点着色器 ----
    inline const char* g_VertexShaderHLSL = R"hlsl(
    cbuffer Constants : register(b0)
    {
        row_major float4x4 g_World;
        row_major float4x4 g_View;
        row_major float4x4 g_Projection;
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

    VSOutput main(VSInput input)
    {
        VSOutput output;

        float4 worldPos = mul(float4(input.Position, 1.0), g_World);
        float4 viewPos = mul(worldPos, g_View);
        float4 projPos = mul(viewPos, g_Projection);

        output.PositionCS = projPos;
        output.PositionWS = worldPos.xyz;
        output.NormalWS = mul(input.Normal, (float3x3)g_World);
        output.Color = input.Color;

        return output;
    }
    )hlsl";

    // ---- 像素着色器 ----
    inline const char* g_PixelShaderHLSL = R"hlsl(
    cbuffer Constants : register(b0)
    {
        row_major float4x4 g_World;
        row_major float4x4 g_View;
        row_major float4x4 g_Projection;
    };

    struct PSInput
    {
        float4 PositionCS : SV_POSITION;
        float3 PositionWS : POSITION;
        float3 NormalWS   : NORMAL;
        float4 Color      : COLOR;
    };

    float4 main(PSInput input) : SV_TARGET
    {
        // 简单的方向光
        float3 lightDir = normalize(float3(0.5, 0.7, 0.3));
        float3 normal = normalize(input.NormalWS);

        // Lambertian diffuse
        float NdotL = max(dot(normal, lightDir), 0.0);

        // 环境光
        float ambient = 0.15;

        // 最终颜色
        float3 finalColor = input.Color.rgb * (ambient + NdotL * 0.85);

        // 轻微的高光
        float3 viewDir = normalize(float3(0, 1, -2) - input.PositionWS);
        float3 halfVec = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfVec), 0.0), 32.0);
        finalColor += float3(0.3, 0.3, 0.3) * spec * 0.5;

        return float4(finalColor, input.Color.a);
    }
    )hlsl";

} // namespace Kiwi
