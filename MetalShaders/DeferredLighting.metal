// Instanced deferred lighting. Output is additively blended.
// VSMain draws directional lights as a fullscreen triangle; VSPointLight draws
// point lights as icosahedron volumes with front faces culled. Instances index
// viewUB.lights, which holds directional lights first.
// Scene depth and the shadow atlas are depth2d textures on Metal.

//!VERTEX
struct KiwiLight
{
    float4 colorAndType; // xyz color, w = light type bits
    float4 dirAndRadius; // xyz direction or position, w = radius
};

struct KiwiView
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;
    float4x4 invViewProjection;
    float4 cameraAndMode;
    float4 screenParams;
    float4 lightHeader; // x = numLights, y = numDirectionalLights (int bits)
    KiwiLight lights[8];
};

struct LightVSOut
{
    float4 position [[position]];
    float4 clipPos; // unclamped clip position, divided per pixel for the G-Buffer UV
    uint lightIndex [[flat]];
};

vertex LightVSOut VSMain(uint vertexID [[vertex_id]], uint instanceID [[instance_id]])
{
    float2 uv = float2(float((vertexID << 1) & 2), float(vertexID & 2));
    LightVSOut out;
    out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    out.clipPos = out.position;
    out.lightIndex = min(instanceID, 7u);
    return out;
}

// Unit-circumradius icosahedron. Winding matches the engine's clockwise front faces.
constant float3 kIcosahedron[12] = {
    float3(-0.5257311,  0.8506508,  0.0), float3( 0.5257311,  0.8506508,  0.0),
    float3(-0.5257311, -0.8506508,  0.0), float3( 0.5257311, -0.8506508,  0.0),
    float3( 0.0, -0.5257311,  0.8506508), float3( 0.0,  0.5257311,  0.8506508),
    float3( 0.0, -0.5257311, -0.8506508), float3( 0.0,  0.5257311, -0.8506508),
    float3( 0.8506508,  0.0, -0.5257311), float3( 0.8506508,  0.0,  0.5257311),
    float3(-0.8506508,  0.0, -0.5257311), float3(-0.8506508,  0.0,  0.5257311),
};
// Scale by radius / inradius so the faces enclose the whole light sphere.
constant float kIcosahedronInradius = 0.7946545;

vertex LightVSOut VSPointLight(uint vertexID [[vertex_id]], uint instanceID [[instance_id]],
                               constant KiwiView& viewUB [[buffer(0)]])
{
    uint lightIndex = min(uint(as_type<int>(viewUB.lightHeader.y)) + instanceID, 7u);
    KiwiLight light = viewUB.lights[lightIndex];
    float scale = max(light.dirAndRadius.w, 0.001) / kIcosahedronInradius;
    float3 worldPos = light.dirAndRadius.xyz + kIcosahedron[vertexID % 12] * scale;
    float4 clip = viewUB.viewProjection * float4(worldPos, 1.0);

    LightVSOut out;
    out.clipPos = clip;
    // Depth testing is off, so clamping to the far plane only keeps volumes that cross it from being clipped.
    clip.z = min(clip.z, clip.w);
    out.position = clip;
    out.lightIndex = lightIndex;
    return out;
}

//!FRAGMENT
struct LightVSOut
{
    float4 position [[position]];
    float4 clipPos;
    uint lightIndex [[flat]];
};

struct KiwiLight
{
    float4 colorAndType; // xyz color, w = light type bits
    float4 dirAndRadius; // xyz direction or position, w = radius
};

struct KiwiView
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;
    float4x4 invViewProjection;
    float4 cameraAndMode;
    float4 screenParams;
    float4 lightHeader; // x = numLights, y = numDirectionalLights (int bits)
    KiwiLight lights[8];
};

struct KiwiShadow
{
    float4x4 lightViewProj[4];
    float4 cascadeSplits;
    float4 biasPack;   // bias, normalBias, strength, numCascades bitcast
    float4 mapSizePad; // x = shadow map size
};

float3 DecodeNormal(float4 gbufferA)
{
    float2 oct = gbufferA.rg * 2.0 - 1.0;
    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    if (n.z < 0.0)
    {
        float2 s = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * s;
    }
    return normalize(n);
}

float3 ReconstructWorldPos(float4x4 invViewProj, float2 uv, float depth)
{
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 world = invViewProj * float4(ndc, depth, 1.0);
    return world.xyz / world.w;
}

constant float kPi = 3.14159265359;

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (kPi * d * d);
}

float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
    float a = sqrt(a2);
    float v = NoL * (NoV * (1.0 - a) + a);
    float l = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (v + l);
}

float3 F_Schlick(float3 F0, float VoH)
{
    float Fc = Pow5(1.0 - VoH);
    return saturate(50.0 * F0.g) * Fc + (1.0 - Fc) * F0;
}

float3 Diffuse_Burley(float3 diffuseColor, float roughness, float NoV, float NoL, float VoH)
{
    float FD90 = 0.5 + 2.0 * VoH * VoH * roughness;
    float FdV = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoV);
    float FdL = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoL);
    return diffuseColor * ((1.0 / kPi) * FdV * FdL);
}

float3 SpecularGGX(float roughness, float3 F0, float NoV, float NoL, float NoH, float VoH)
{
    float a2 = max(roughness * roughness, 0.001);
    return D_GGX(a2, NoH) * Vis_SmithJointApprox(a2, NoV, NoL) * F_Schlick(F0, VoH);
}

float SampleShadowAtlas(depth2d<float> atlas, float2 atlasUV, float depth, float bias, float mapSize)
{
    constexpr sampler shadowSampler(filter::nearest, address::clamp_to_edge, compare_func::less_equal);
    float texel = 1.0 / mapSize;
    float ref = depth - bias;
    float shadow = 0.0;
    shadow += atlas.sample_compare(shadowSampler, atlasUV, ref);
    shadow += atlas.sample_compare(shadowSampler, atlasUV + float2(texel, 0.0), ref);
    shadow += atlas.sample_compare(shadowSampler, atlasUV + float2(-texel, 0.0), ref);
    shadow += atlas.sample_compare(shadowSampler, atlasUV + float2(0.0, texel), ref);
    shadow += atlas.sample_compare(shadowSampler, atlasUV + float2(0.0, -texel), ref);
    return shadow / 5.0;
}

float ComputeShadow(constant KiwiShadow& shadow, depth2d<float> atlas, float3 worldPos, float viewZ)
{
    int numCascades = as_type<int>(shadow.biasPack.w);
    if (numCascades <= 0)
        return 1.0;

    int ci = 0;
    float splits[4] = { shadow.cascadeSplits.x, shadow.cascadeSplits.y, shadow.cascadeSplits.z, shadow.cascadeSplits.w };
    for (int i = 0; i < numCascades - 1; ++i)
    {
        if (viewZ > splits[i])
            ci = i + 1;
    }

    float4 sp = shadow.lightViewProj[ci] * float4(worldPos, 1.0);
    float3 sc;
    sc.xy = sp.xy / sp.w * 0.5 + 0.5;
    sc.y = 1.0 - sc.y;
    sc.z = sp.z / sp.w;
    if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z < 0.0 || sc.z > 1.0)
        return 1.0;

    float2 offsets[4] = { float2(0.0, 0.0), float2(0.5, 0.0), float2(0.0, 0.5), float2(0.5, 0.5) };
    float2 atlasUV = sc.xy * 0.5 + offsets[ci];
    float visibility = SampleShadowAtlas(atlas, atlasUV, sc.z, shadow.biasPack.x, shadow.mapSizePad.x);
    return mix(1.0, visibility, shadow.biasPack.z);
}

fragment float4 PSMain(LightVSOut in [[stage_in]],
                       constant KiwiView& viewUB [[buffer(0)]],
                       constant KiwiShadow& shadowUB [[buffer(2)]],
                       texture2d<float> gbufferA [[texture(0)]],
                       texture2d<float> gbufferB [[texture(1)]],
                       texture2d<float> gbufferC [[texture(2)]],
                       depth2d<float> shadowAtlas [[texture(3)]],
                       depth2d<float> depthTex [[texture(7)]],
                       sampler linearSampler [[sampler(0)]])
{
    float2 ndc = in.clipPos.xy / in.clipPos.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    KiwiLight light = viewUB.lights[in.lightIndex];

    float depth = depthTex.sample(linearSampler, uv);
    if (depth >= 1.0)
        return float4(0.0);

    float3 worldPos = ReconstructWorldPos(viewUB.invViewProjection, uv, depth);

    float3 L;
    float attenuation = 1.0;
    int lightType = as_type<int>(light.colorAndType.w);
    if (lightType == 0)
    {
        L = normalize(light.dirAndRadius.xyz);
    }
    else
    {
        float3 toLight = light.dirAndRadius.xyz - worldPos;
        float distSq = dot(toLight, toLight);
        float radius = max(light.dirAndRadius.w, 0.001);
        // The volume is a coarse icosahedron, so reject pixels outside the sphere before reading the G-Buffer.
        if (distSq >= radius * radius)
            return float4(0.0);

        float dist = sqrt(distSq);
        L = toLight / max(dist, 0.0001);
        float invSq = 1.0 / max(distSq, 0.0001);
        float nd = dist / radius;
        float nd4 = nd * nd * nd * nd;
        float window = saturate(1.0 - nd4);
        attenuation = invSq * window * window;
    }

    float4 gA = gbufferA.sample(linearSampler, uv);
    float4 gB = gbufferB.sample(linearSampler, uv);
    float4 gC = gbufferC.sample(linearSampler, uv);
    uint shadingModelId = (uint(gB.a * 255.0 + 0.5)) >> 4;
    if (shadingModelId == 0)
        return float4(0.0);

    float3 N = DecodeNormal(gA);
    float metallic = gB.r;
    float specular = gB.g;
    float roughness = max(gB.b, 0.04);
    float3 baseColor = gC.rgb;
    float ao = gC.a;

    float NoL = saturate(dot(N, L));
    if (NoL <= 0.0)
        return float4(0.0);

    if (lightType == 0)
    {
        float viewZ = (viewUB.view * float4(worldPos, 1.0)).z;
        attenuation *= ComputeShadow(shadowUB, shadowAtlas, worldPos, viewZ);
    }

    float3 V = normalize(viewUB.cameraAndMode.xyz - worldPos);
    float NoV = saturate(abs(dot(N, V)) + 1e-5);

    float3 diffuseColor = baseColor * (1.0 - metallic);
    float3 specularColor = mix(float3(0.08) * specular, baseColor, metallic);

    float3 H = normalize(V + L);
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));
    float3 diffBRDF = Diffuse_Burley(diffuseColor, roughness, NoV, NoL, VoH);
    float3 specBRDF = SpecularGGX(roughness, specularColor, NoV, NoL, NoH, VoH);
    float3 radiance = light.colorAndType.xyz * NoL * attenuation;
    return float4((diffBRDF + specBRDF) * radiance * ao, 0.0);
}
