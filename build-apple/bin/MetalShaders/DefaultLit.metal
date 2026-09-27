#include <metal_stdlib>
using namespace metal;

// DefaultLit — forward Phong + optional base color. ViewPadding1 carries EViewMode.

//!VERTEX
struct VSIn
{
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float3 tangent  [[attribute(2)]];
    float4 color    [[attribute(3)]];
    float2 uv       [[attribute(4)]];
};

struct VSOut
{
    float4 position [[position]];
    float3 positionWS;
    float3 normalWS;
    float4 color;
    float2 uv;
};

vertex VSOut kiwi_vertex(VSIn in [[stage_in]],
                         constant float4* viewData [[buffer(0)]],
                         constant float4* objData [[buffer(1)]])
{
    float4x4 world = float4x4(objData[0], objData[1], objData[2], objData[3]);
    float4x4 view  = float4x4(viewData[0], viewData[1], viewData[2], viewData[3]);
    float4x4 proj  = float4x4(viewData[4], viewData[5], viewData[6], viewData[7]);

    float4 worldPos = world * float4(in.position, 1.0);
    VSOut out;
    out.position = proj * view * worldPos;
    out.positionWS = worldPos.xyz;
    out.normalWS = float3x3(world[0].xyz, world[1].xyz, world[2].xyz) * in.normal;
    out.color = in.color * objData[4];
    out.uv = in.uv;
    return out;
}

//!FRAGMENT
struct FSIn
{
    float4 position [[position]];
    float3 positionWS;
    float3 normalWS;
    float4 color;
    float2 uv;
};

fragment float4 kiwi_fragment(FSIn in [[stage_in]],
                              constant float4* viewData [[buffer(0)]],
                              constant float4* objData [[buffer(1)]],
                              texture2d<float> baseColorTex [[texture(4)]])
{
    constexpr sampler linearSampler(filter::linear, address::repeat);

    float selected = objData[5].x;
    float roughness = objData[5].y;
    float metallic = objData[5].z;
    float hasBaseColor = objData[5].w;
    float viewMode = viewData[16].w;

    float3 albedo = in.color.rgb;
    if (hasBaseColor > 0.5)
        albedo *= baseColorTex.sample(linearSampler, in.uv).rgb;

    if (selected > 1.5)
        return float4(albedo, in.color.a);

    // EViewMode: BaseColor=2, Roughness=3, Metallic=4
    if (viewMode > 3.5)
        return float4(float3(metallic), 1.0);
    if (viewMode > 2.5)
        return float4(float3(roughness), 1.0);
    if (viewMode > 1.5)
        return float4(albedo, 1.0);

    float3 normal = normalize(in.normalWS);
    float3 viewDir = normalize(viewData[16].xyz - in.positionWS);
    float3 lightDir = normalize(float3(0.5, 0.7, 0.3));
    float ndotl = max(dot(normal, lightDir), 0.0);

    float3 ambient = float3(0.15);
    float3 diffuse = float3(ndotl * 0.85);
    float3 halfVec = normalize(lightDir + viewDir);
    float shininess = mix(256.0, 8.0, roughness);
    float spec = pow(max(dot(normal, halfVec), 0.0), shininess);
    float specIntensity = mix(0.04, 0.8, metallic);
    float3 finalColor = albedo * (ambient + diffuse) + float3(spec * specIntensity);
    return float4(finalColor, in.color.a);
}
