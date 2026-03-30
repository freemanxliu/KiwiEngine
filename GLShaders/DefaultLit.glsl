#version 450 core
// DefaultLit — Phong lighting (GLSL version)
// VS and FS are separated by //!SPLIT marker

#define MAX_LIGHTS 8

struct LightData
{
    vec3 ColorIntensity;
    int  Type;
    vec3 DirectionOrPos;
    float Radius;
};

//!VERTEX
// View UniformBuffer — binding 0
layout(std140, binding = 0) uniform ViewConstants
{
    mat4 g_View;
    mat4 g_Projection;
    mat4 g_InvViewProj;
    vec3 g_CameraPos;
    float g_Time;
    int g_NumLights;
    vec3 g_ViewPad0;
    vec4 g_DiffuseOverride;
    vec4 g_SpecularOverride;
    vec2 g_ScreenSize;
    vec2 g_InvScreenSize;
    vec4 g_ViewPad1;
    LightData g_Lights[MAX_LIGHTS];
};

// Object UniformBuffer — binding 1
layout(std140, binding = 1) uniform ObjectConstants
{
    mat4 g_World;
    vec4 g_ObjectColor;
    float g_Selected;
    float g_Roughness;
    float g_Metallic;
    float g_HasBaseColorTex;
    float g_HasNormalTex;
    vec3 g_ObjPad;
};

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec2 aTexCoord;

out vec3 vPositionWS;
out vec3 vNormalWS;
out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    vec4 worldPos = g_World * vec4(aPosition, 1.0);
    gl_Position = g_Projection * g_View * worldPos;
    vPositionWS = worldPos.xyz;
    vNormalWS = mat3(g_World) * aNormal;
    vColor = aColor * g_ObjectColor;
    vTexCoord = aTexCoord;
}

//!FRAGMENT
// View UniformBuffer — binding 0
layout(std140, binding = 0) uniform ViewConstants
{
    mat4 g_View;
    mat4 g_Projection;
    mat4 g_InvViewProj;
    vec3 g_CameraPos;
    float g_Time;
    int g_NumLights;
    vec3 g_ViewPad0;
    vec4 g_DiffuseOverride;
    vec4 g_SpecularOverride;
    vec2 g_ScreenSize;
    vec2 g_InvScreenSize;
    vec4 g_ViewPad1;
    LightData g_Lights[MAX_LIGHTS];
};

// Object UniformBuffer — binding 1
layout(std140, binding = 1) uniform ObjectConstants
{
    mat4 g_World;
    vec4 g_ObjectColor;
    float g_Selected;
    float g_Roughness;
    float g_Metallic;
    float g_HasBaseColorTex;
    float g_HasNormalTex;
    vec3 g_ObjPad;
};

in vec3 vPositionWS;
in vec3 vNormalWS;
in vec4 vColor;
in vec2 vTexCoord;

out vec4 FragColor;

void main()
{
    if (g_Selected > 1.5)
    {
        FragColor = vec4(vColor.rgb * g_ObjectColor.rgb, vColor.a);
        return;
    }

    vec3 normal = normalize(vNormalWS);
    vec3 viewDir = normalize(g_CameraPos - vPositionWS);
    vec3 albedo = vColor.rgb;

    vec3 ambient = vec3(0.15);
    vec3 totalDiffuse = vec3(0.0);
    vec3 totalSpecular = vec3(0.0);

    // Fallback single directional light
    vec3 lightDir = normalize(vec3(0.5, 0.7, 0.3));
    float NdotL = max(dot(normal, lightDir), 0.0);
    totalDiffuse = vec3(1.0) * NdotL * 0.85;

    vec3 halfVec = normalize(lightDir + viewDir);
    float shininess = mix(256.0, 8.0, g_Roughness);
    float spec = pow(max(dot(normal, halfVec), 0.0), shininess);
    float specIntensity = mix(0.04, 0.8, g_Metallic);
    totalSpecular = vec3(1.0) * spec * specIntensity;

    vec3 finalColor = albedo * (ambient + totalDiffuse) + totalSpecular;
    FragColor = vec4(finalColor, vColor.a);
}
