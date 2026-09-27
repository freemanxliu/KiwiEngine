// Hemisphere ambient. Drawn before the additive per-light passes.

//!VERTEX
struct VSOut
{
    float4 position [[position]];
    float2 uv;
};

vertex VSOut VSMain(uint vertexID [[vertex_id]])
{
    float2 uv = float2(float((vertexID << 1) & 2), float(vertexID & 2));
    VSOut out;
    out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    out.uv = float2(uv.x, 1.0 - uv.y);
    return out;
}

//!FRAGMENT
struct FSIn
{
    float4 position [[position]];
    float2 uv;
};

struct KiwiView
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;
    float4x4 invViewProjection;
    float4 cameraAndMode;
};

float3 DecodeNormal(float4 gA)
{
    float2 oct = gA.rg * 2.0 - 1.0;
    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0)
    {
        float2 s = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * s;
    }
    return normalize(n);
}

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float3 EnvBRDFApprox(float3 F0, float roughness, float NoV)
{
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a + r.zw;
    AB.y *= saturate(50.0 * F0.g);
    return F0 * AB.x + AB.y;
}

fragment float4 PSMain(FSIn in [[stage_in]],
                       constant KiwiView& viewUB [[buffer(0)]],
                       texture2d<float> gbufferA [[texture(0)]],
                       texture2d<float> gbufferB [[texture(1)]],
                       texture2d<float> gbufferC [[texture(2)]],
                       depth2d<float> depthTex [[texture(7)]],
                       sampler linearSampler [[sampler(0)]])
{
    float depth = depthTex.sample(linearSampler, in.uv);
    if (depth >= 1.0)
        return float4(0.05, 0.05, 0.08, 1.0);

    float4 gA = gbufferA.sample(linearSampler, in.uv);
    float4 gB = gbufferB.sample(linearSampler, in.uv);
    float4 gC = gbufferC.sample(linearSampler, in.uv);
    uint shadingModelId = (uint(gB.a * 255.0 + 0.5)) >> 4;
    float3 baseColor = gC.rgb;
    if (shadingModelId == 0)
        return float4(baseColor, 1.0);

    float3 N = DecodeNormal(gA);
    float metallic = gB.r;
    float specular = gB.g;
    float roughness = max(gB.b, 0.04);
    float ao = gC.a;
    float NoV = saturate(abs(dot(N, normalize(viewUB.cameraAndMode.xyz))) + 1e-5);

    float3 diffuseColor = baseColor * (1.0 - metallic);
    float3 specularColor = mix(float3(0.08) * specular, baseColor, metallic);
    float3 sky = float3(0.15, 0.15, 0.25);
    float3 ground = float3(0.10, 0.08, 0.05);
    float3 ambient = mix(ground, sky, N.y * 0.5 + 0.5);
    float3 ambDiffuse = diffuseColor * ambient * 0.3 * ao;
    float3 ambSpecular = EnvBRDFApprox(specularColor, roughness, NoV) * ambient * 0.15;
    return float4(ambDiffuse + ambSpecular, 1.0);
}
