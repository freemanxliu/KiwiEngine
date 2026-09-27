// Grayscale Post-Process Shader
// Converts the scene to grayscale using luminance weights.

cbuffer PostProcessCB : register(b0)
{
    float ScreenWidth;
    float ScreenHeight;
    float Intensity;   // 0 = original, 1 = full grayscale
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
    
    // Standard luminance weights (BT.709)
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 gray = float3(luminance, luminance, luminance);
    
    // Lerp between original and grayscale based on Intensity
    float3 result = lerp(color.rgb, gray, Intensity);
    
    return float4(result, color.a);
}
