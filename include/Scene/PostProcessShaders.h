#pragma once

#include <cstdint>

namespace Kiwi
{

    // ============================================================
    // Post-Process Constant Buffer (must match HLSL)
    // ============================================================
    struct PostProcessCBData
    {
        float ScreenWidth;
        float ScreenHeight;
        float Intensity;       // Generic intensity parameter
        float Time;            // Elapsed time in seconds
    };

    // ============================================================
    // Full-Screen Triangle Vertex Shader
    // Uses SV_VertexID (0,1,2) to generate a full-screen triangle.
    // No vertex buffer or input layout required.
    // ============================================================
    inline const char* g_PostProcessVS = R"hlsl(

    struct VSOutput
    {
        float4 Position : SV_POSITION;
        float2 TexCoord : TEXCOORD0;
    };

    VSOutput VSMain(uint vertexID : SV_VertexID)
    {
        VSOutput output;

        // Generate full-screen triangle from vertex ID:
        // ID 0: (-1, -1) -> (0, 1)
        // ID 1: ( 3, -1) -> (2, 1)
        // ID 2: (-1,  3) -> (0,-1)
        float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
        output.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
        output.TexCoord = float2(uv.x, 1.0 - uv.y); // Flip Y for texture coords
        return output;
    }

    )hlsl";

    // ============================================================
    // Post-Process Pixel Shader: Passthrough (copies input to output)
    // Used as identity/default shader when no effects are active.
    // ============================================================
    inline const char* g_PostProcessPassthroughPS = R"hlsl(

    Texture2D g_InputTexture : register(t0);
    SamplerState g_Sampler : register(s0);

    struct PSInput
    {
        float4 Position : SV_POSITION;
        float2 TexCoord : TEXCOORD0;
    };

    float4 PSMain(PSInput input) : SV_TARGET
    {
        return g_InputTexture.Sample(g_Sampler, input.TexCoord);
    }

    )hlsl";

} // namespace Kiwi
