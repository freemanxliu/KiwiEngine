// Vignette Post-Process Shader
// Darkens the edges of the screen for a cinematic look.

cbuffer PostProcessCB : register(b0)
{
    float ScreenWidth;
    float ScreenHeight;
    float Intensity;   // Vignette strength (0 = none, 1 = strong)
    float Time;
};

Texture2D g_InputTexture : register(t0);
SamplerState g_Sampler : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET
{
    float4 color = g_InputTexture.Sample(g_Sampler, input.TexCoord);
    
    // Distance from center (0 at center, ~0.7 at corners)
    float2 center = input.TexCoord - float2(0.5, 0.5);
    float dist = length(center);
    
    // Smooth vignette falloff
    float vignette = 1.0 - smoothstep(0.3, 0.9, dist * Intensity * 1.5);
    
    return float4(color.rgb * vignette, color.a);
}
