// ============================================================
// G-Buffer Geometry Pass Shader (UE5 GBuffer Layout)
//
// Writes to 3 MRT matching UE5 DeferredShadingCommon.ush:
//   GBufferA (R8G8B8A8_UNORM): WorldNormal Octahedron(RG) + Normal.z(B) + PerObjectGBufferData(A)
//   GBufferB (R8G8B8A8_UNORM): Metallic(R) + Specular(G) + Roughness(B) + ShadingModelID(A)
//   GBufferC (R8G8B8A8_UNORM): BaseColor(RGB) + GBufferAO(A)
//
// World position is reconstructed from hardware depth in the lighting pass.
//
// Two vertex shader paths:
//   Default:                   Reads per-object data from ObjectUB (b1) via CB offset binding.
//   USE_GPU_SCENE_INSTANCING:  Reads per-object data from StructuredBuffer (t8) via SV_InstanceID.
//                              Enables true DrawIndexedInstanced for batched objects.
// ============================================================

#include "Common.hlsli"

// Material textures (bound per-object)
Texture2D    g_BaseColorTex : register(t4);  // BaseColor/Albedo texture
Texture2D    g_NormalTex    : register(t5);  // Normal map texture
SamplerState g_LinearWrap   : register(s1);  // Linear wrap sampler

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Tangent  : TANGENT;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD;
    uint   InstanceID : SV_InstanceID;
};

struct VSOutput
{
    float4 PositionCS  : SV_POSITION;
    float3 PositionWS  : TEXCOORD0;
    float3 NormalWS    : TEXCOORD1;
    float3 TangentWS   : TEXCOORD2;
    float3 BitangentWS : TEXCOORD3;
    float4 Color       : COLOR;
    float2 TexCoord    : TEXCOORD4;
    // Per-instance material data (from GPU Scene)
    nointerpolation float  Roughness     : TEXCOORD5;
    nointerpolation float  Metallic      : TEXCOORD6;
    nointerpolation float  HasBaseColorTex : TEXCOORD7;
    nointerpolation float  HasNormalTex    : TEXCOORD8;
    nointerpolation float  Selected       : TEXCOORD9;
};

// UE5-matching G-Buffer MRT output
struct GBufferOutput
{
    float4 GBufferA : SV_TARGET0;  // Normal Oct(RG) + Normal.z(B) + PerObjectData(A)
    float4 GBufferB : SV_TARGET1;  // Metallic(R) + Specular(G) + Roughness(B) + ShadingModelID(A)
    float4 GBufferC : SV_TARGET2;  // BaseColor(RGB) + AO(A)
};

// ---- Octahedron Normal Encoding (UE5 EncodeNormal) ----
float3 EncodeNormal(float3 n)
{
    float3 absN = abs(n);
    float sum = absN.x + absN.y + absN.z;
    float2 oct = n.xy / sum;

    if (n.z < 0.0)
    {
        float2 signNotZero = float2(oct.x >= 0.0 ? 1.0 : -1.0, oct.y >= 0.0 ? 1.0 : -1.0);
        oct = (1.0 - abs(oct.yx)) * signNotZero;
    }

    float2 encoded = oct * 0.5 + 0.5;
    float encodedZ = n.z * 0.5 + 0.5;
    return float3(encoded, encodedZ);
}

// ---- Vertex Shader ----
VSOutput VSMain(VSInput input)
{
    VSOutput output;

#ifdef USE_GPU_SCENE_INSTANCING
    // Instanced path: read per-object data from StructuredBuffer (t8)
    GPUSceneData inst = GetInstanceData(input.InstanceID);
    float4x4 world       = inst.World;
    float4   objColor    = inst.ObjectColor;
    float    roughness   = inst.Roughness;
    float    metallic    = inst.Metallic;
    float    hasBaseTex  = inst.HasBaseColorTex;
    float    hasNormTex  = inst.HasNormalTex;
    float    selected    = inst.Selected;
#else
    // Single draw path: read from ObjectUB (b1) bound via CB offset
    float4x4 world       = g_World;
    float4   objColor    = g_ObjectColor;
    float    roughness   = g_Roughness;
    float    metallic    = g_Metallic;
    float    hasBaseTex  = g_HasBaseColorTex;
    float    hasNormTex  = g_HasNormalTex;
    float    selected    = g_Selected;
#endif

    float4 worldPos = mul(float4(input.Position, 1.0), world);
    float4 viewPos = mul(worldPos, g_View);
    float4 projPos = mul(viewPos, g_Projection);

    output.PositionCS = projPos;
    output.PositionWS = worldPos.xyz;
    output.NormalWS = normalize(mul(input.Normal, (float3x3)world));
    output.Color = input.Color * objColor;
    output.TexCoord = input.TexCoord;

    // Compute TBN from vertex tangent (Gram-Schmidt orthogonalization)
    float3 N = output.NormalWS;
    float3 T = normalize(mul(input.Tangent.xyz, (float3x3)world));

    if (dot(T, T) < 0.001)
    {
        if (abs(N.y) < 0.99)
            T = normalize(cross(float3(0, 1, 0), N));
        else
            T = normalize(cross(float3(1, 0, 0), N));
    }

    T = normalize(T - N * dot(N, T));
    float3 B = cross(N, T) * input.Tangent.w;
    output.TangentWS = T;
    output.BitangentWS = B;

    // Pass material data to PS via interpolants
    output.Roughness       = roughness;
    output.Metallic        = metallic;
    output.HasBaseColorTex = hasBaseTex;
    output.HasNormalTex    = hasNormTex;
    output.Selected        = selected;

    return output;
}

// ---- Encode ShadingModelID ----
float EncodeShadingModelId(uint shadingModelId)
{
    return float(shadingModelId << 4) / 255.0;
}

// ---- Pixel Shader ----
GBufferOutput PSMain(VSOutput input)
{
    GBufferOutput output;

    // ---- BaseColor ----
    float3 baseColor = input.Color.rgb;
    if (input.HasBaseColorTex > 0.5)
    {
        float4 texColor = g_BaseColorTex.Sample(g_LinearWrap, input.TexCoord);
        baseColor = texColor.rgb * input.Color.rgb;
    }

    // ---- Normal ----
    float3 normal = normalize(input.NormalWS);
    if (input.HasNormalTex > 0.5)
    {
        float3 tangentNormal = g_NormalTex.Sample(g_LinearWrap, input.TexCoord).rgb;
        tangentNormal = tangentNormal * 2.0 - 1.0;

        float3 T = normalize(input.TangentWS);
        float3 B = normalize(input.BitangentWS);
        float3 N = normalize(input.NormalWS);
        float3x3 TBN = float3x3(T, B, N);
        normal = normalize(mul(tangentNormal, TBN));
    }

    // ---- Encode GBuffer (UE5 layout) ----
    float3 encodedNormal = EncodeNormal(normal);
    float perObjectData = 0.0;
    output.GBufferA = float4(encodedNormal, perObjectData);

    float specular = 0.5;
    uint shadingModelId = 1; // DefaultLit
    output.GBufferB = float4(input.Metallic, specular, input.Roughness, EncodeShadingModelId(shadingModelId));

    float ao = 1.0;
    output.GBufferC = float4(baseColor, ao);

    return output;
}
