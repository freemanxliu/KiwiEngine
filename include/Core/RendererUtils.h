#pragma once

#include "Math/Math.h"
#include "RHI/RHI.h"

#include <string>

namespace Kiwi
{

class MeshComponent;

// World-space AABB of a mesh's vertices. Collapses to the mesh position when it has no vertices.
void ComputeWorldAABB(const MeshComponent& mesh, Vec3& outMin, Vec3& outMax);

// Backends with a G-Buffer path. GL/Vulkan only render forward.
bool IsDeferredRHI(RHI_API_TYPE api);

// Built-in vertex shader source used to create the shared input layout.
const char* BuiltinVertexShader(RHI_API_TYPE api);

// Forward/material shaders live in MetalShaders/ or GLShaders/ next to the HLSL Shaders/ folder.
std::string ForwardShaderDirectory(RHI_API_TYPE api, const std::string& hlslDir);

// Reads a shader file and inlines its #include "..." lines (FXC D3DCompile needs expanded source).
// Returns an empty string if the file cannot be opened.
std::string ReadShaderFileWithIncludes(const std::string& filePath);

} // namespace Kiwi
