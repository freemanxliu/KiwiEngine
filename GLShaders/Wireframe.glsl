#version 450 core
// Wireframe — Normal visualization (GLSL version)

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

out vec3 vNormalWS;
out vec4 vColor;

void main()
{
    vec4 worldPos = g_World * vec4(aPosition, 1.0);
    gl_Position = g_Projection * g_View * worldPos;
    vNormalWS = mat3(g_World) * aNormal;
    vColor = aColor * g_ObjectColor;
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

in vec3 vNormalWS;
in vec4 vColor;
out vec4 FragColor;

void main()
{
    vec3 normal = normalize(vNormalWS);
    vec3 color = normal * 0.5 + 0.5;

    if (g_Selected > 1.5)
    {
        FragColor = vec4(vColor.rgb * g_ObjectColor.rgb, vColor.a);
        return;
    }

    FragColor = vec4(color, 1.0);
}
