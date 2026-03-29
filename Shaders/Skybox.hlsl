// ============================================================
// Skybox Shader — Equirectangular Environment Map
// Renders sky behind all geometry (depth == 1.0 pixels only)
// Uses fullscreen triangle + inverse ViewProjection to compute view direction
// ============================================================

cbuffer ViewConstants : register(b0)
{
    row_major float4x4 g_View;
    row_major float4x4 g_Projection;
    row_major float4x4 g_InvViewProj; // For sky direction reconstruction
    float3 g_CameraPos;
    float  g_Time;              // Repurposed: sky intensity multiplier (when > 0.001)
    int    g_NumLights;
    float3 g_ViewPad0;
    float4 g_DiffuseOverride;
    float4 g_SpecularOverride;
    float2 g_ScreenSize;
    float2 g_InvScreenSize;
    float4 g_ViewPad1;
};

Texture2D    g_DepthBuffer : register(t7);
Texture2D    g_EnvMap      : register(t4);  // Equirectangular HDR environment map
SamplerState g_Sampler     : register(s0);

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// ---- Fullscreen Triangle Vertex Shader ----
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.Position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.TexCoord = float2(uv.x, 1.0 - uv.y);
    return output;
}

// ---- Equirectangular UV from direction ----
float2 DirectionToEquirectangular(float3 dir)
{
    // Spherical coordinates: theta (azimuth), phi (elevation)
    float phi = asin(clamp(dir.y, -1.0, 1.0));
    float theta = atan2(dir.z, dir.x);

    // Map to [0,1] UV
    float u = theta / (2.0 * 3.14159265359) + 0.5;
    float v = phi / 3.14159265359 + 0.5;
    return float2(u, 1.0 - v); // Flip V for standard equirectangular layout
}

// ---- Pixel Shader ----
float4 PSMain(VSOutput input) : SV_TARGET
{
    // Only render on sky pixels (depth == 1.0)
    float depth = g_DepthBuffer.Sample(g_Sampler, input.TexCoord).r;
    if (depth < 1.0)
        discard;

    // Reconstruct world-space direction from UV + depth=1
    float2 ndc = input.TexCoord * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, 1.0, 1.0); // depth = 1.0 for far plane
    float4 worldPos = mul(clipPos, g_InvViewProj);
    float3 worldDir = normalize(worldPos.xyz / worldPos.w - g_CameraPos);

    // Sample equirectangular environment map
    float2 envUV = DirectionToEquirectangular(worldDir);
    float3 skyColor = g_EnvMap.Sample(g_Sampler, envUV).rgb;

    // Apply intensity (stored in g_Time slot for reuse of View CB layout)
    float intensity = max(g_Time, 0.0);
    if (intensity < 0.001) intensity = 1.0; // Default if not set

    skyColor *= intensity;

    return float4(skyColor, 1.0);
}
