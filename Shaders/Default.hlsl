// ============================================================
// Default Phong Shader
// Lambert Diffuse + Blinn-Phong Specular
// ============================================================

cbuffer Constants : register(b0)
{
    row_major float4x4 g_World;
    row_major float4x4 g_View;
    row_major float4x4 g_Projection;
    float4 g_ObjectColor;
    float  g_Selected;
    float3 g_Padding;
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

// ---- Vertex Shader ----
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0), g_World);
    float4 viewPos = mul(worldPos, g_View);
    float4 projPos = mul(viewPos, g_Projection);

    output.PositionCS = projPos;
    output.PositionWS = worldPos.xyz;
    output.NormalWS = mul(input.Normal, (float3x3)g_World);
    output.Color = input.Color * g_ObjectColor;

    return output;
}

// ---- Pixel Shader ----
float4 PSMain(VSOutput input) : SV_TARGET
{
    // Directional light
    float3 lightDir = normalize(float3(0.5, 0.7, 0.3));
    float3 normal = normalize(input.NormalWS);

    // Lambertian diffuse
    float NdotL = max(dot(normal, lightDir), 0.0);

    // Ambient
    float ambient = 0.15;

    // Final color
    float3 finalColor = input.Color.rgb * (ambient + NdotL * 0.85);

    // Blinn-Phong specular
    float3 viewDir = normalize(float3(0, 1, -2) - input.PositionWS);
    float3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfVec), 0.0), 32.0);
    finalColor += float3(0.3, 0.3, 0.3) * spec * 0.5;

    // Gizmo / unlit mode: if g_Selected > 1.5 we treat it as unlit (pure vertex color)
    if (g_Selected > 1.5)
    {
        return float4(input.Color.rgb * g_ObjectColor.rgb, input.Color.a);
    }

    return float4(finalColor, input.Color.a);
}
