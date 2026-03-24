import glslangModule from '@webgpu/glslang';
import { writeFileSync } from 'fs';

const glslang = await glslangModule();

// Vertex shader (equivalent to the HLSL one in Shaders.h)
const vertexGLSL = `#version 450

layout(binding = 0) uniform Constants {
    mat4 g_World;
    mat4 g_View;
    mat4 g_Projection;
    vec4 g_ObjectColor;
    float g_Selected;
    float g_Padding1;
    float g_Padding2;
    float g_Padding3;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec3 outPositionWS;
layout(location = 1) out vec3 outNormalWS;
layout(location = 2) out vec4 outColor;

void main() {
    // Note: HLSL uses row-major, GLSL uses column-major
    // Since our CPU data is row-major, we transpose by swapping mul order
    vec4 worldPos = vec4(inPosition, 1.0) * g_World;
    vec4 viewPos = worldPos * g_View;
    vec4 projPos = viewPos * g_Projection;

    gl_Position = projPos;
    outPositionWS = worldPos.xyz;
    outNormalWS = (vec4(inNormal, 0.0) * g_World).xyz;
    outColor = inColor * g_ObjectColor;
}
`;

// Pixel/Fragment shader
const fragmentGLSL = `#version 450

layout(binding = 0) uniform Constants {
    mat4 g_World;
    mat4 g_View;
    mat4 g_Projection;
    vec4 g_ObjectColor;
    float g_Selected;
    float g_Padding1;
    float g_Padding2;
    float g_Padding3;
};

layout(location = 0) in vec3 inPositionWS;
layout(location = 1) in vec3 inNormalWS;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(0.5, 0.7, 0.3));
    vec3 normal = normalize(inNormalWS);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float ambient = 0.15;

    vec3 finalColor = inColor.rgb * (ambient + NdotL * 0.85);

    vec3 viewDir = normalize(vec3(0, 1, -2) - inPositionWS);
    vec3 halfVec = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfVec), 0.0), 32.0);
    finalColor += vec3(0.3, 0.3, 0.3) * spec * 0.5;

    if (g_Selected > 0.5) {
        finalColor = mix(finalColor, vec3(1.0, 0.6, 0.1), 0.25);
    }

    outColor = vec4(finalColor, inColor.a);
}
`;

function spirvToArray(spirv) {
    const bytes = new Uint8Array(spirv.buffer);
    const lines = [];
    for (let i = 0; i < bytes.length; i += 16) {
        const chunk = [];
        for (let j = i; j < Math.min(i + 16, bytes.length); j++) {
            chunk.push('0x' + bytes[j].toString(16).padStart(2, '0'));
        }
        lines.push('    ' + chunk.join(', ') + ',');
    }
    return lines.join('\n');
}

try {
    const vsSPIRV = glslang.compileGLSL(vertexGLSL, 'vertex');
    const fsSPIRV = glslang.compileGLSL(fragmentGLSL, 'fragment');

    console.log(`VS SPIR-V: ${vsSPIRV.length * 4} bytes`);
    console.log(`FS SPIR-V: ${fsSPIRV.length * 4} bytes`);

    const header = `#pragma once

// Auto-generated SPIR-V bytecode for Vulkan shaders
// Equivalent to the HLSL shaders in Shaders.h

namespace Kiwi
{
    // Vertex shader SPIR-V (${vsSPIRV.length * 4} bytes)
    inline const unsigned char g_VulkanVertexShaderSPIRV[] = {
${spirvToArray(vsSPIRV)}
    };
    inline const size_t g_VulkanVertexShaderSPIRVSize = sizeof(g_VulkanVertexShaderSPIRV);

    // Fragment shader SPIR-V (${fsSPIRV.length * 4} bytes)
    inline const unsigned char g_VulkanFragmentShaderSPIRV[] = {
${spirvToArray(fsSPIRV)}
    };
    inline const size_t g_VulkanFragmentShaderSPIRVSize = sizeof(g_VulkanFragmentShaderSPIRV);

} // namespace Kiwi
`;

    const outputPath = '../include/Scene/VulkanShaders.h';
    writeFileSync(outputPath, header);
    console.log(`Written to ${outputPath}`);
} catch (e) {
    console.error('Compilation error:', e.message || e);
    process.exit(1);
}
