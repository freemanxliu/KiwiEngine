// ============================================================
// G-Buffer Geometry Pass Shader (UE5-inspired layout)
// Writes to 3 MRT (all R8G8B8A8_UNORM):
//   GBufferA: Normal Octahedron(RG) + Metallic(B) + ShadingModelID(A)
//   GBufferB: BaseColor(RGB) + Roughness(A)
//   GBufferC: Emissive(RGB) + Specular(A)
// World position reconstructed from hardware depth buffer in lighting pass
// ============================================================

#define MAX_LIGHTS 8

struct LightData
{
    float3 ColorIntensity;
    int    Type;
    float3 DirectionOrPos;
    float  Radius;
};

cbuffer Constants : register(b0)
{
    row_major float4x4 g_World;
    row_major float4x4 g_View;
    row_major float4x4 g_Projection;
    float4 g_ObjectColor;
    float  g_Selected;
    int    g_NumLights;
    float2 g_Padding;
    float3 g_CameraPos;
    float  g_Roughness;
    float  g_Metallic;
    float  g_HasBaseColorTex;
    float  g_HasNormalTex;
    float  g_MaterialPadding;
    LightData g_Lights[MAX_LIGHTS];
};

// Material textures (bound per-object)
Texture2D    g_BaseColorTex : register(t4);  // BaseColor/Albedo texture
Texture2D    g_NormalTex    : register(t5);  // Normal map texture
SamplerState g_LinearWrap   : register(s1);  // Linear wrap sampler (from root sig)

struct VSInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float4 Color    : COLOR;
    float2 TexCoord : TEXCOORD;
};

struct VSOutput
{
    float4 PositionCS : SV_POSITION;
    float3 PositionWS : TEXCOORD0;
    float3 NormalWS   : TEXCOORD1;
    float3 TangentWS  : TEXCOORD2;
    float3 BitangentWS: TEXCOORD3;
    float4 Color      : COLOR;
    float2 TexCoord   : TEXCOORD4;
};

// G-Buffer MRT output (UE5-inspired)
struct GBufferOutput
{
    float4 GBufferA : SV_TARGET0;  // Normal Octahedron(RG) + Metallic(B) + ShadingModelID(A)
    float4 GBufferB : SV_TARGET1;  // BaseColor(RGB) + Roughness(A)
    float4 GBufferC : SV_TARGET2;  // Emissive(RGB) + Specular(A)
};

// ---- Octahedron Normal Encoding ----
// Encodes unit normal to 2D octahedron representation in [0,1] range
// Reference: "Survey of Efficient Representations for Independent Unit Vectors" (Cigolle et al. 2014)
float2 OctahedronEncode(float3 n)
{
    // Project onto octahedron
    float3 absN = abs(n);
    float sum = absN.x + absN.y + absN.z;
    float2 oct = n.xy / sum;

    // Reflect bottom hemisphere
    if (n.z < 0.0)
    {
        float2 signNotZero = float2(oct.x >= 0.0 ? 1.0 : -1.0, oct.y >= 0.0 ? 1.0 : -1.0);
        oct = (1.0 - abs(oct.yx)) * signNotZero;
    }

    // Map from [-1,1] to [0,1]
    return oct * 0.5 + 0.5;
}

// ---- Vertex Shader ----
VSOutput VSMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.Position, 1.0), g_World);
    float4 viewPos = mul(worldPos, g_View);
    float4 projPos = mul(viewPos, g_Projection);

    output.PositionCS = projPos;
    output.PositionWS = worldPos.xyz;
    output.NormalWS = normalize(mul(input.Normal, (float3x3)g_World));
    output.Color = input.Color * g_ObjectColor;
    output.TexCoord = input.TexCoord;

    // Generate tangent/bitangent from normal for normal mapping
    // (Approximation when no explicit tangent data is available)
    float3 N = output.NormalWS;
    float3 T;
    if (abs(N.y) < 0.99)
        T = normalize(cross(float3(0, 1, 0), N));
    else
        T = normalize(cross(float3(1, 0, 0), N));
    float3 B = normalize(cross(N, T));
    output.TangentWS = T;
    output.BitangentWS = B;

    return output;
}

// ---- Pixel Shader ----
GBufferOutput PSMain(VSOutput input)
{
    GBufferOutput output;

    // BaseColor: sample texture if available, otherwise use vertex color
    float3 baseColor = input.Color.rgb;
    if (g_HasBaseColorTex > 0.5)
    {
        float4 texColor = g_BaseColorTex.Sample(g_LinearWrap, input.TexCoord);
        baseColor = texColor.rgb * input.Color.rgb;
    }

    // Normal: use normal map if available
    float3 normal = normalize(input.NormalWS);
    if (g_HasNormalTex > 0.5)
    {
        float3 tangentNormal = g_NormalTex.Sample(g_LinearWrap, input.TexCoord).rgb;
        tangentNormal = tangentNormal * 2.0 - 1.0; // [0,1] -> [-1,1]

        float3 T = normalize(input.TangentWS);
        float3 B = normalize(input.BitangentWS);
        float3 N = normalize(input.NormalWS);
        float3x3 TBN = float3x3(T, B, N);
        normal = normalize(mul(tangentNormal, TBN));
    }

    float2 octNormal = OctahedronEncode(normal);

    // Default specular = 0.5 (dielectric Fresnel reflectance ~4%)
    float specular = 0.5;
    // ShadingModelID: 1.0 = DefaultLit (encoded as [0,1] for UNORM)
    float shadingModelID = 1.0 / 255.0; // ID=1

    output.GBufferA = float4(octNormal.x, octNormal.y, g_Metallic, shadingModelID);
    output.GBufferB = float4(baseColor, g_Roughness);
    output.GBufferC = float4(0.0, 0.0, 0.0, specular); // No emissive for now

    return output;
}
