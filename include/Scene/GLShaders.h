#pragma once

namespace Kiwi
{

    // ============================================================
    // GLSL Vertex Shader (Default — used for InputLayout creation + fallback)
    // Matches the new split CB layout: ViewUBO (binding=0) + ObjectUBO (binding=1)
    // ViewConstants UBO must match ViewBufferData (std140 layout, includes LightData[])
    // ============================================================
    inline const char* g_VertexShaderGLSL = R"glsl(
#version 450 core

#define MAX_LIGHTS 8

struct LightData
{
    vec3 ColorIntensity;
    int  Type;
    vec3 DirectionOrPos;
    float Radius;
};

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
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aTexCoord;

out vec3 vPositionWS;
out vec3 vNormalWS;
out vec4 vColor;
out vec2 vTexCoord;

void main()
{
    vec4 worldPos = g_World * vec4(aPosition, 1.0);
    vec4 viewPos = g_View * worldPos;
    vec4 projPos = g_Projection * viewPos;

    gl_Position = projPos;
    vPositionWS = worldPos.xyz;
    vNormalWS = mat3(g_World) * aNormal;
    vColor = aColor * g_ObjectColor;
    vTexCoord = aTexCoord;
}
)glsl";

    // ============================================================
    // GLSL Pixel Shader (Default — simple directional light)
    // ViewConstants UBO must match ViewBufferData (std140 layout)
    // ============================================================
    inline const char* g_PixelShaderGLSL = R"glsl(
#version 450 core

#define MAX_LIGHTS 8

struct LightData
{
    vec3 ColorIntensity;
    int  Type;
    vec3 DirectionOrPos;
    float Radius;
};

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
    // Gizmo / unlit mode
    if (g_Selected > 1.5)
    {
        FragColor = vec4(vColor.rgb * g_ObjectColor.rgb, vColor.a);
        return;
    }

    vec3 normal = normalize(vNormalWS);
    vec3 lightDir = normalize(vec3(0.5, 0.7, 0.3));
    float NdotL = max(dot(normal, lightDir), 0.0);
    float ambient = 0.15;
    vec3 finalColor = vColor.rgb * (ambient + NdotL * 0.85);

    FragColor = vec4(finalColor, vColor.a);
}
)glsl";

    // ============================================================
    // GLSL Post-Process Fullscreen Triangle VS
    // ============================================================
    inline const char* g_PostProcessVS_GLSL = R"glsl(
#version 450 core

out vec2 vTexCoord;

void main()
{
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    vTexCoord = vec2(uv.x, 1.0 - uv.y);
}
)glsl";

    // ============================================================
    // GLSL Post-Process Passthrough PS
    // ============================================================
    inline const char* g_PostProcessPassthroughPS_GLSL = R"glsl(
#version 450 core

uniform sampler2D g_InputTexture;

in vec2 vTexCoord;
out vec4 FragColor;

void main()
{
    FragColor = texture(g_InputTexture, vTexCoord);
}
)glsl";

} // namespace Kiwi
