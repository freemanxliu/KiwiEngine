// ============================================================
// Tonemap Post-Process Shader
// Converts HDR scene color to LDR for display
//
// Operators:
//   - Reinhard (simple)
//   - ACES Filmic (UE5 default)
//
// Input:  HDR scene color (R16G16B16A16_FLOAT)
// Output: LDR color (R8G8B8A8_UNORM backbuffer)
// ============================================================

Texture2D    g_SceneColor : register(t0);
SamplerState g_Sampler    : register(s0);

// PostProcess CB (b0) — shared with other post-process shaders
cbuffer PostProcessCB : register(b0)
{
    float g_PPParam1;  // Exposure (default 1.0)
    float g_PPParam2;  // unused
    float g_PPParam3;  // unused
    float g_PPParam4;  // unused
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.TexCoord = float2(uv.x, 1.0 - uv.y);
    return output;
}

// ---- ACES Filmic Tone Mapping (UE5 reference) ----
// Approximation by Krzysztof Narkowicz
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ---- sRGB Gamma ----
float3 LinearToSRGB(float3 color)
{
    return pow(max(color, 0.0), 1.0 / 2.2);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 hdrColor = g_SceneColor.Sample(g_Sampler, input.TexCoord).rgb;

    // Exposure
    float exposure = max(g_PPParam1, 0.01);
    hdrColor *= exposure;

    // ACES Filmic tone mapping
    float3 ldrColor = ACESFilm(hdrColor);

    // Gamma correction (linear -> sRGB)
    ldrColor = LinearToSRGB(ldrColor);

    return float4(ldrColor, 1.0);
}
