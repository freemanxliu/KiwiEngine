#version 450 core
// Unlit — pure color output (GLSL version)

//!VERTEX
layout(std140, binding = 0) uniform ViewUB
{
    mat4 g_View;
    mat4 g_Projection;
    mat4 g_ViewProjection;
    mat4 g_InvViewProj;
    vec3 g_CameraPos;
    float g_ViewPadding1;
};

layout(std140, binding = 1) uniform ObjectUB
{
    mat4 g_World;
    vec4 g_ObjectColor;
};

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec4 aColor;
layout(location = 4) in vec2 aTexCoord;

out vec4 vColor;

void main()
{
    vec4 worldPos = g_World * vec4(aPosition, 1.0);
    gl_Position = g_Projection * g_View * worldPos;
    vColor = aColor * g_ObjectColor;
}

//!FRAGMENT
in vec4 vColor;
out vec4 FragColor;

void main()
{
    FragColor = vec4(vColor.rgb, vColor.a);
}
